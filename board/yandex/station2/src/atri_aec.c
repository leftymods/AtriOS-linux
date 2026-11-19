/*
 * atri-aec.c - VQE capture front-end with acoustic echo cancellation for AtriOS.
 *
 * Pipeline (informed by yandexstation_2 CIRC_6_PLUS_1_R425 preset):
 *   arecord (hw:0,3, 48 kHz, 8ch S32_LE PDM mics)
 *     -> channels 0..6 (7 physical mics), 48 kHz LPF + decimate 48k -> 16k
 *     -> 4th-order 192 Hz Butterworth HPF per mic (kills DC/hum/rumble)
 *     -> VQE MicChooser (RmsAlgo alpha=0.90, micSwitchThreshold=0.20) + coherent blend
 *     -> NLMS echo cancellation with double-talk protection
 *     -> 480 ms stationary background noise reducer + smooth speech AGC + soft limiter
 *     -> 16 kHz mono S16_LE on stdout
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define SR        16000
#define CAP_CH    8
#define VQE_MICS  7
#define DECIM     3
#define BLK       128                       /* mic samples (16 kHz) per iteration */
#define IN_BYTES  (BLK * DECIM * CAP_CH * 4)
#define OUT_BLK   512
#define TAPS      2048
#define HN        (TAPS + 8192)
#define ENV_HOP   160                       /* 10 ms */
#define ENV_N     400                       /* 4 s of envelope history */
#define NR_HOPS   48                        /* 480 ms (7680 samples @ 16 kHz) */
#define MAXLAG    60                        /* 600 ms */
#define REF_SOCK  "/var/run/atrios/aec_ref.sock"
#define STAT_FILE "/var/run/atrios/aec.stat"
#define RING      (1 << 17)
#define RMASK     (RING - 1)

/* ---- VQE mic array state (CIRC_6_PLUS_1_R425) ---- */
typedef struct {
    float z1, z2;
} biquad_t;

static inline float biquad_hpf192(biquad_t *q, float x) {
    /* 2nd-order Butterworth HPF @ fc=192 Hz, fs=16000 Hz */
    const float b0 = 0.948213f, b1 = -1.896426f, b2 = 0.948213f;
    const float a1 = -1.893780f, a2 = 0.899072f;
    float y = b0 * x + q->z1;
    q->z1 = b1 * x - a1 * y + q->z2;
    q->z2 = b2 * x - a2 * y;
    return y;
}

static float g_lp48[VQE_MICS];
static biquad_t g_hpf[VQE_MICS][2];
static double g_msq_acc[VQE_MICS];
static float g_m_ema[VQE_MICS];
static float g_m_noise[VQE_MICS];
static int g_best_mic = 0;

/* ---- BNR + speech AGC state ---- */
static float g_nr_hist[NR_HOPS];
static int g_nr_idx = 0, g_nr_fill = 0;
static float g_noise_env = 40.0f;
static double g_post_acc = 0.0;
static float g_sig_env = 0.0f;
static float g_nr_gain = 1.0f;
static float g_agc_gain = 1.0f;

static volatile sig_atomic_t g_stop = 0;
static pid_t g_child = -1;

static void on_sig(int s) {
    (void)s;
    g_stop = 1;
    if (g_child > 0) kill(g_child, SIGTERM);
}

static struct timespec g_t0;
static long now_pos(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    double dt = (double)(t.tv_sec - g_t0.tv_sec) + (double)(t.tv_nsec - g_t0.tv_nsec) * 1e-9;
    return (long)(dt * SR);
}

/* ---- reference timeline ---- */
static int16_t g_ring[RING];
static long g_wp = 0;               /* next write position (absolute samples) */
static float g_racc = 0.f;
static int g_rcnt = 0;

static void ref_drain(int fd) {
    static uint8_t buf[65536];
    for (;;) {
        ssize_t n = recv(fd, buf, sizeof(buf), MSG_DONTWAIT);
        if (n <= 0) break;
        long np = now_pos();
        if (g_wp < np - 8000 || g_wp > np + 8000) {
            long from = g_wp, to = np;
            if (to - from > RING) from = to - RING;
            if (from < 0) from = 0;
            for (long q = from; q < to; q++) g_ring[q & RMASK] = 0;
            g_wp = np;
            g_racc = 0.f;
            g_rcnt = 0;
        }
        const int16_t *s = (const int16_t *)buf;
        size_t frames = (size_t)n / 4;
        for (size_t i = 0; i < frames; i++) {
            g_racc += 0.5f * ((float)s[i * 2] + (float)s[i * 2 + 1]);
            if (++g_rcnt == DECIM) {
                g_ring[g_wp & RMASK] = (int16_t)(g_racc / (float)DECIM);
                g_wp++;
                g_racc = 0.f;
                g_rcnt = 0;
            }
        }
    }
}

/* ---- NLMS ---- */
static float g_h[HN];
static float g_w[TAPS];
static int g_p = TAPS;
static double g_en = 0.0;
static long g_delay = 0;            /* bulk delay in samples */

static inline float dotp(const float *a, const float *b, int n) {
    float s0 = 0.f, s1 = 0.f, s2 = 0.f, s3 = 0.f;
    for (int i = 0; i < n; i += 4) {
        s0 += a[i] * b[i];
        s1 += a[i + 1] * b[i + 1];
        s2 += a[i + 2] * b[i + 2];
        s3 += a[i + 3] * b[i + 3];
    }
    return (s0 + s1) + (s2 + s3);
}

/* ---- bulk delay estimation from envelopes ---- */
static float g_menv[ENV_N], g_renv[ENV_N];
static int g_ei = 0, g_ecount = 0;
static double g_macc = 0, g_racc_env = 0;
static int g_ecnt = 0;

static void estimate_delay(void) {
    int n = g_ecount < ENV_N ? g_ecount : ENV_N;
    if (n < 200) return;
    static float m[ENV_N], r[ENV_N];
    for (int i = 0; i < n; i++) {
        int idx = (g_ei - n + i + ENV_N * 2) % ENV_N;
        m[i] = g_menv[idx];
        r[i] = g_renv[idx];
    }
    double mm = 0, rm = 0;
    for (int i = 0; i < n; i++) { mm += m[i]; rm += r[i]; }
    mm /= n; rm /= n;
    if (rm < 15.0) return;                   /* reference basically silent */
    double rvar = 0, mvar = 0;
    for (int i = 0; i < n; i++) {
        rvar += (r[i] - rm) * (r[i] - rm);
        mvar += (m[i] - mm) * (m[i] - mm);
    }
    if (rvar < 1e-3 || mvar < 1e-3) return;
    double best = -1.0, second = -1.0;
    int best_lag = 0;
    for (int lag = 0; lag <= MAXLAG; lag++) {
        double s = 0;
        for (int i = lag; i < n; i++) s += (m[i] - mm) * (r[i - lag] - rm);
        double c = s / sqrt(rvar * mvar);
        if (c > best) {
            if (abs(lag - best_lag) > 2) second = best;
            best = c;
            best_lag = lag;
        } else if (abs(lag - best_lag) > 2 && c > second) {
            second = c;
        }
    }
    /* Need a clear, unambiguous peak, confirmed twice in a row. */
    static int pend_lag = -1;
    if (best < 0.40 || best - second < 0.08) { pend_lag = -1; return; }
    if (pend_lag < 0 || abs(pend_lag - best_lag) > 1) { pend_lag = best_lag; return; }
    pend_lag = best_lag;
    long nd = (long)best_lag * ENV_HOP - 320;
    if (nd < 0) nd = 0;
    if (labs(nd - g_delay) > 480) {
        g_delay = nd;
        memset(g_w, 0, sizeof(g_w));
    }
}

static void write_stat(double ratio_db, int active) {
    FILE *f = fopen(STAT_FILE ".tmp", "w");
    if (!f) return;
    fprintf(f, "delay_ms=%ld\nreduction_db=%.1f\nref_active=%d\n",
            g_delay * 1000 / SR, ratio_db, active);
    fclose(f);
    rename(STAT_FILE ".tmp", STAT_FILE);
}

static int spawn_arecord(void) {
    int pfd[2];
    if (pipe(pfd) < 0) return -1;
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        dup2(pfd[1], 1);
        close(pfd[0]);
        close(pfd[1]);
        execlp("arecord", "arecord", "-q", "-D", "hw:0,3", "-t", "raw", "-f", "S32_LE",
               "-r", "48000", "-c", "8", (char *)NULL);
        _exit(127);
    }
    close(pfd[1]);
    g_child = pid;
    return pfd[0];
}

int main(int argc, char **argv) {
    int use_stdin = (argc > 1 && strcmp(argv[1], "--stdin") == 0);
    signal(SIGTERM, on_sig);
    signal(SIGINT, on_sig);
    signal(SIGPIPE, SIG_DFL);
    clock_gettime(CLOCK_MONOTONIC, &g_t0);

    for (int m = 0; m < VQE_MICS; m++) g_m_noise[m] = 30.0f;
    for (int i = 0; i < NR_HOPS; i++) g_nr_hist[i] = 40.0f;

    int rs = -1;
    if (!use_stdin) {
        mkdir("/var/run/atrios", 0755);
        unlink(REF_SOCK);
        rs = socket(AF_UNIX, SOCK_DGRAM, 0);
        if (rs >= 0) {
            struct sockaddr_un a;
            memset(&a, 0, sizeof(a));
            a.sun_family = AF_UNIX;
            snprintf(a.sun_path, sizeof(a.sun_path), "%s", REF_SOCK);
            int rb = 1 << 20;
            setsockopt(rs, SOL_SOCKET, SO_RCVBUF, &rb, sizeof(rb));
            if (bind(rs, (struct sockaddr *)&a, sizeof(a)) < 0) {
                close(rs);
                rs = -1;
            } else {
                chmod(REF_SOCK, 0666);
            }
        }
    }

    int afd = use_stdin ? 0 : spawn_arecord();
    if (afd < 0) return 1;

    g_wp = now_pos();
    long mp = now_pos();
    static uint8_t inbuf[IN_BYTES];
    static int16_t outbuf[OUT_BLK];
    int outn = 0;
    double pw_in = 0, pw_out = 0;
    int stat_cnt = 0, ref_active_cnt = 0;

    while (!g_stop) {
        size_t got = 0;
        while (got < IN_BYTES) {
            ssize_t n = read(afd, inbuf + got, IN_BYTES - got);
            if (n <= 0) { g_stop = 1; break; }
            got += (size_t)n;
        }
        if (got < IN_BYTES) break;
        if (rs >= 0) ref_drain(rs);

        long np = now_pos();
        if (labs(mp - np) > 3200) mp = np;

        const int32_t *in = (const int32_t *)inbuf;
        for (int i = 0; i < BLK; i++) {
            const int32_t *fr = in + i * DECIM * CAP_CH;
            float s16[VQE_MICS];
            float h_sum = 0.0f;
            int h_cnt = 0;
            for (int m = 0; m < VQE_MICS; m++) {
                float d_acc = 0.0f;
                for (int d = 0; d < DECIM; d++) {
                    float raw_s = (float)fr[d * CAP_CH + m] * (10.0f / 16384.0f);
                    g_lp48[m] += 0.55f * (raw_s - g_lp48[m]);
                    d_acc += g_lp48[m];
                }
                float x16 = biquad_hpf192(&g_hpf[m][0], d_acc / (float)DECIM);
                x16 = biquad_hpf192(&g_hpf[m][1], x16);
                s16[m] = x16;
                g_msq_acc[m] += (double)x16 * x16;
                if (g_m_noise[m] < 1950.0f && (g_m_ema[m] >= 1.0f || g_ecount < 5)) {
                    h_sum += x16;
                    h_cnt++;
                }
            }
            float avg_h = (h_cnt > 0) ? (h_sum / (float)h_cnt) : s16[g_best_mic];
            float mic = 0.72f * s16[g_best_mic] + 0.28f * avg_h;

            long q = mp - g_delay;
            float r = 0.f;
            if (q >= 0 && q < g_wp && q > g_wp - RING) r = (float)g_ring[q & RMASK];
            float rraw = 0.f;
            if (mp >= 0 && mp < g_wp && mp > g_wp - RING) rraw = (float)g_ring[mp & RMASK];

            if (g_p >= HN) {
                memmove(g_h, g_h + g_p - TAPS, TAPS * sizeof(float));
                g_p = TAPS;
            }
            float old = g_h[g_p - TAPS];
            g_h[g_p++] = r;
            g_en += (double)r * r - (double)old * old;
            if (g_en < 0) g_en = 0;
            const float *x = g_h + g_p - TAPS;
            float y = dotp(g_w, x, TAPS);
            float e = mic - y;

            int active = g_en > (double)TAPS * 40.0;
            if (active) {
                float ref_rms = sqrtf((float)(g_en / TAPS) + 1.0f);
                float mu = (fabsf(e) > 2.2f * ref_rms) ? 0.08f : 0.45f;
                float g = mu * e / (float)(g_en + (double)TAPS * 20.0);
                for (int t = 0; t < TAPS; t++) g_w[t] += g * x[t];
                ref_active_cnt++;
            }

            /* VQE Background Noise Reducer (480 ms history) + smooth speech AGC */
            float ae = fabsf(e);
            g_sig_env = (ae > g_sig_env) ? (g_sig_env * 0.85f + ae * 0.15f) : (g_sig_env * 0.992f + ae * 0.008f);
            float nr_tgt = 1.0f;
            if (g_sig_env < g_noise_env * 1.85f) {
                nr_tgt = 0.80f;
            } else if (g_sig_env < g_noise_env * 2.50f) {
                float frac = (g_sig_env - g_noise_env * 1.85f) / (g_noise_env * 0.65f + 1.0f);
                nr_tgt = 0.80f + 0.20f * frac;
            }
            g_nr_gain += ((nr_tgt > g_nr_gain) ? 0.15f : 0.01f) * (nr_tgt - g_nr_gain);

            if (g_sig_env > g_noise_env * 2.60f && g_sig_env > 180.0f) {
                float agc_tgt = 2800.0f / g_sig_env;
                if (agc_tgt < 1.0f) agc_tgt = 1.0f;
                if (agc_tgt > 2.6f) agc_tgt = 2.6f;
                float rate = (agc_tgt < g_agc_gain) ? 0.04f : 0.008f;
                g_agc_gain += rate * (agc_tgt - g_agc_gain);
            } else {
                g_agc_gain += 0.01f * (1.0f - g_agc_gain);
            }

            float o = e * g_nr_gain * g_agc_gain;
            if (fabsf(o) > 26000.f) o = 26000.f * tanhf(o / 26000.f);
            if (o > 32767.f) o = 32767.f;
            if (o < -32768.f) o = -32768.f;
            outbuf[outn++] = (int16_t)o;
            if (outn == OUT_BLK) {
                fwrite(outbuf, 2, OUT_BLK, stdout);
                fflush(stdout);
                outn = 0;
            }

            pw_in += (double)mic * mic;
            pw_out += (double)o * o;
            g_post_acc += (double)ae;

            g_macc += fabsf(mic);
            g_racc_env += fabsf(rraw);
            if (++g_ecnt == ENV_HOP) {
                /* Per-mic RMSChooser (alpha=0.90, micSwitchThreshold=0.20) */
                int cand_m = g_best_mic;
                float best_snr = -1.0f;
                for (int m = 0; m < VQE_MICS; m++) {
                    float mr = sqrtf((float)(g_msq_acc[m] / ENV_HOP));
                    g_msq_acc[m] = 0.0;
                    g_m_ema[m] = 0.90f * g_m_ema[m] + 0.10f * mr;
                    if (mr < g_m_noise[m]) {
                        g_m_noise[m] = 0.85f * g_m_noise[m] + 0.15f * mr;
                    } else {
                        g_m_noise[m] = 0.998f * g_m_noise[m] + 0.002f * mr;
                    }
                    if (g_m_noise[m] < 10.0f) g_m_noise[m] = 10.0f;
                    if (g_m_noise[m] > 2000.0f) g_m_noise[m] = 2000.0f;
                    if (g_m_ema[m] >= 1.0f && g_m_noise[m] < 1950.0f) {
                        float snr = g_m_ema[m] / (g_m_noise[m] + 1.0f);
                        if (snr > best_snr) {
                            best_snr = snr;
                            cand_m = m;
                        }
                    }
                }
                if (g_m_ema[g_best_mic] < 1.0f || g_m_ema[cand_m] > g_m_ema[g_best_mic] * 1.20f) {
                    g_best_mic = cand_m;
                }

                /* Update 480 ms stationary noise floor */
                float hop_env = (float)(g_post_acc / ENV_HOP);
                g_post_acc = 0.0;
                g_nr_hist[g_nr_idx] = hop_env;
                g_nr_idx = (g_nr_idx + 1) % NR_HOPS;
                if (g_nr_fill < NR_HOPS) g_nr_fill++;
                float min_h = g_nr_hist[0];
                for (int k = 1; k < g_nr_fill; k++) {
                    if (g_nr_hist[k] < min_h) min_h = g_nr_hist[k];
                }
                g_noise_env = (min_h < 15.0f) ? 15.0f : min_h;

                g_menv[g_ei] = (float)(g_macc / ENV_HOP);
                g_renv[g_ei] = (float)(g_racc_env / ENV_HOP);
                g_ei = (g_ei + 1) % ENV_N;
                g_ecount++;
                g_macc = g_racc_env = 0;
                g_ecnt = 0;
                if (g_ecount % 100 == 0) estimate_delay();
            }
            mp++;
        }

        stat_cnt += BLK;
        if (!use_stdin && stat_cnt >= SR * 2) {
            double db = 0;
            if (pw_out > 1.0 && pw_in > 1.0) db = 10.0 * log10(pw_in / pw_out);
            write_stat(db, ref_active_cnt > SR);
            pw_in = pw_out = 0;
            stat_cnt = 0;
            ref_active_cnt = 0;
        }
    }

    if (outn > 0) {
        fwrite(outbuf, 2, (size_t)outn, stdout);
        fflush(stdout);
    }
    if (g_child > 0) {
        kill(g_child, SIGTERM);
        waitpid(g_child, NULL, 0);
    }
    if (!use_stdin) unlink(REF_SOCK);
    return 0;
}
