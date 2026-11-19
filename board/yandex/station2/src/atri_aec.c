/*
 * atri-aec.c - capture front-end with acoustic echo cancellation for AtriOS.
 *
 * Pipeline:
 *   arecord (hw:0,3, 48 kHz, 8ch S32_LE PDM mics)
 *     -> average 8 channels, decimate 48k -> 16k (box filter)
 *     -> subtract the estimated echo of the speaker output (NLMS adaptive filter)
 *     -> 16 kHz mono S16_LE on stdout
 *
 * Reference signal: bt-speaker-daemon tees every decoded PCM block it plays
 * (48 kHz stereo S16_LE) to the unix datagram socket /var/run/atrios/aec_ref.sock.
 * The reference is placed on an absolute 16 kHz timeline; a coarse bulk delay
 * (ALSA buffer + acoustic path) is estimated from 100 Hz envelopes and then
 * removed, so the NLMS filter (128 ms) only has to model the room response.
 *
 * Without reference data (nothing playing) the filter output is zero and the
 * microphone signal passes through unchanged.
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
#define DECIM     3
#define BLK       128                       /* mic samples (16 kHz) per iteration */
#define IN_BYTES  (BLK * DECIM * CAP_CH * 4)
#define OUT_BLK   512
#define TAPS      2048
#define HN        (TAPS + 8192)
#define RING      (1 << 17)
#define RMASK     (RING - 1)
#define ENV_HOP   160                       /* 10 ms */
#define ENV_N     400                       /* 4 s of envelope history */
#define MAXLAG    60                        /* 600 ms */
#define REF_SOCK  "/var/run/atrios/aec_ref.sock"
#define STAT_FILE "/var/run/atrios/aec.stat"

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

int main(void) {
    signal(SIGTERM, on_sig);
    signal(SIGINT, on_sig);
    signal(SIGPIPE, SIG_DFL);
    clock_gettime(CLOCK_MONOTONIC, &g_t0);

    mkdir("/var/run/atrios", 0755);
    unlink(REF_SOCK);
    int rs = socket(AF_UNIX, SOCK_DGRAM, 0);
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

    int afd = spawn_arecord();
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
            double acc = 0;
            const int32_t *fr = in + i * DECIM * CAP_CH;
            for (int k = 0; k < DECIM * CAP_CH; k++) acc += (double)fr[k];
            float mic = (float)(acc / (DECIM * CAP_CH) * (8.0 / 16384.0));

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
                float g = 0.45f * e / (float)(g_en + (double)TAPS * 20.0);
                for (int t = 0; t < TAPS; t++) g_w[t] += g * x[t];
                ref_active_cnt++;
            }

            float o = e;
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

            g_macc += fabsf(mic);
            g_racc_env += fabsf(rraw);
            if (++g_ecnt == ENV_HOP) {
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
        if (stat_cnt >= SR * 2) {
            double db = 0;
            if (pw_out > 1.0 && pw_in > 1.0) db = 10.0 * log10(pw_in / pw_out);
            write_stat(db, ref_active_cnt > SR);
            pw_in = pw_out = 0;
            stat_cnt = 0;
            ref_active_cnt = 0;
        }
    }

    if (g_child > 0) {
        kill(g_child, SIGTERM);
        waitpid(g_child, NULL, 0);
    }
    unlink(REF_SOCK);
    return 0;
}
