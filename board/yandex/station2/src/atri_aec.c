/*
 * atri-aec.c - VQE capture front-end with acoustic echo cancellation for AtriOS.
 *
 * Pipeline (informed by yandexstation_2 CIRC_6_PLUS_1_R425 preset):
 *   arecord (hw:0,3, 48 kHz, 8ch S32_LE PDM mics)
 *     -> channels 0..6 (7 physical mics), 48 kHz LPF + decimate 48k -> 16k
 *     -> 4th-order 192 Hz Butterworth HPF per mic (kills DC/hum/rumble)
 *     -> per-channel VQE MicChooser; single best-SNR capsule during speech
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
#include "gtcrn_ns.h"

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
#define VQE_JSON  "/var/run/atrios/aec_vqe.json"
#define BARGE_FILE "/var/run/atrios/barge_in"
#define FFT_N     256
#define FFT_HALF  129
#define RING      (1 << 17)
#define RMASK     (RING - 1)

/* ---- Frequency-Domain STFT-Kalman Residual Echo Suppressor (256-pt FFT, 50% OLA) ---- */
static float g_noise_env = 40.0f;
static float g_hann[FFT_N];
static float g_e_win[FFT_N];
static float g_r_win[FFT_N];
static float g_ola[BLK];
static float g_srr[FFT_HALF];
static float g_ser_re[FFT_HALF];
static float g_ser_im[FFT_HALF];
static float g_see[FFT_HALF];
static float g_noise_psd[FFT_HALF];
static float g_nr_bin_gain[FFT_HALF];
static int g_noise_psd_ready = 0;
static float g_snr_speech_db = 0.0f;
static int g_barge_streak = 0;
static int g_barge_active = 0;

static void init_stft(void) {
    for (int i = 0; i < FFT_N; i++) {
        g_hann[i] = 0.5f * (1.0f - cosf(2.0f * 3.14159265f * (float)i / (float)FFT_N));
    }
    for (int k = 0; k < FFT_HALF; k++) {
        g_srr[k] = 1e3f;
        g_ser_re[k] = 0.0f;
        g_ser_im[k] = 0.0f;
        g_see[k] = 1e3f;
        g_noise_psd[k] = 1e3f;
        g_nr_bin_gain[k] = 1.0f;
    }
}

static void fft256(float *re, float *im, int inv) {
    int j = 0;
    for (int i = 1; i < FFT_N; i++) {
        int bit = FFT_N >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            float tr = re[i]; re[i] = re[j]; re[j] = tr;
            float ti = im[i]; im[i] = im[j]; im[j] = ti;
        }
    }
    for (int len = 2; len <= FFT_N; len <<= 1) {
        float ang = (inv ? 2.0f : -2.0f) * 3.14159265f / (float)len;
        float wlen_r = cosf(ang), wlen_i = sinf(ang);
        for (int i = 0; i < FFT_N; i += len) {
            float wr = 1.0f, wi = 0.0f;
            int half = len >> 1;
            for (int k = 0; k < half; k++) {
                int u = i + k, v = i + k + half;
                float vr = re[v] * wr - im[v] * wi;
                float vi = re[v] * wi + im[v] * wr;
                re[v] = re[u] - vr; im[v] = im[u] - vi;
                re[u] += vr;        im[u] += vi;
                float nwr = wr * wlen_r - wi * wlen_i;
                wi = wr * wlen_i + wi * wlen_r;
                wr = nwr;
            }
        }
    }
    if (inv) {
        const float inv_n = 1.0f / (float)FFT_N;
        for (int i = 0; i < FFT_N; i++) {
            re[i] *= inv_n;
            im[i] *= inv_n;
        }
    }
}

static void stft_kalman_process_block(float *e_blk, const float *r_blk, int ref_active) {
    memmove(g_e_win, g_e_win + BLK, BLK * sizeof(float));
    memcpy(g_e_win + BLK, e_blk, BLK * sizeof(float));
    memmove(g_r_win, g_r_win + BLK, BLK * sizeof(float));
    memcpy(g_r_win + BLK, r_blk, BLK * sizeof(float));

    float ere[FFT_N], eim[FFT_N], rre[FFT_N], rim[FFT_N];
    for (int i = 0; i < FFT_N; i++) {
        ere[i] = g_e_win[i] * g_hann[i];
        eim[i] = 0.0f;
        rre[i] = g_r_win[i] * g_hann[i];
        rim[i] = 0.0f;
    }
    fft256(ere, eim, 0);
    fft256(rre, rim, 0);

    double block_power = 0.0;
    for (int i = 0; i < BLK; i++) block_power += (double)e_blk[i] * e_blk[i];
    float block_rms = sqrtf((float)(block_power / BLK));
    int speech_active = block_rms > fmaxf(g_noise_env * 1.45f + 15.0f, 55.0f);
    if (!speech_active && !g_noise_psd_ready) {
        for (int k = 0; k < FFT_HALF; k++) {
            g_noise_psd[k] = ere[k] * ere[k] + eim[k] * eim[k];
        }
        g_noise_psd_ready = 1;
    }

    /* Bins 5..54 correspond to ~312 Hz .. 3375 Hz (SberDevices speech band 300..3400 Hz) */
    double speech_p = 0.0, resid_p = 0.0;
    for (int k = 0; k < FFT_HALF; k++) {
        float pe = ere[k] * ere[k] + eim[k] * eim[k];
        float pr = rre[k] * rre[k] + rim[k] * rim[k];
        /* Complex cross spectrum E * conj(R); magnitude-only correlation makes
         * coherence approach 1 even for unrelated signals with similar levels. */
        float cross_re = ere[k] * rre[k] + eim[k] * rim[k];
        float cross_im = eim[k] * rre[k] - ere[k] * rim[k];
        g_see[k] = 0.82f * g_see[k] + 0.18f * pe;
        g_srr[k] = 0.82f * g_srr[k] + 0.18f * pr;
        g_ser_re[k] = 0.86f * g_ser_re[k] + 0.14f * cross_re;
        g_ser_im[k] = 0.86f * g_ser_im[k] + 0.14f * cross_im;

        float gain = 1.0f;
        if (ref_active && g_srr[k] > 500.0f) {
            float cross_power = g_ser_re[k] * g_ser_re[k] + g_ser_im[k] * g_ser_im[k];
            float coh = cross_power / (g_see[k] * g_srr[k] + 1.0f);
            if (coh > 1.0f) coh = 1.0f;
            /* Kalman-Wiener residual echo suppressor: preserves speech, crushes coherent echo */
            float echo_est = coh * g_see[k] * 1.15f;
            float clean_est = g_see[k] - echo_est;
            if (clean_est < 0.0f) clean_est = 0.0f;
            gain = (clean_est + 0.08f * g_see[k]) / (g_see[k] + 1.0f);
            if (gain < 0.14f) gain = 0.14f;
            if (gain > 1.0f) gain = 1.0f;
        }
        float clean_power = pe * gain * gain;
        if (!speech_active && g_noise_psd_ready) {
            float alpha = clean_power < g_noise_psd[k] ? 0.08f : 0.003f;
            g_noise_psd[k] += alpha * (clean_power - g_noise_psd[k]);
        }
        float noise_gain = 1.0f;
        if (g_noise_psd_ready && clean_power > 1.0f) {
            /* Conservative spectral BNR: reduce the stationary noise floor
             * while keeping a nonzero floor for low-level speech components. */
            float subtraction = speech_active ? 0.70f : 1.10f;
            float power_gain = 1.0f - subtraction * g_noise_psd[k] / (clean_power + 1.0f);
            if (power_gain < 0.0f) power_gain = 0.0f;
            noise_gain = sqrtf(power_gain);
            if (noise_gain < 0.28f) noise_gain = 0.28f;
        }
        float gain_target = gain * noise_gain;
        float gain_rate = gain_target < g_nr_bin_gain[k] ? 0.45f : 0.18f;
        g_nr_bin_gain[k] += gain_rate * (gain_target - g_nr_bin_gain[k]);
        gain = g_nr_bin_gain[k];
        ere[k] *= gain;
        eim[k] *= gain;
        if (k > 0 && k < FFT_N / 2) {
            ere[FFT_N - k] = ere[k];
            eim[FFT_N - k] = -eim[k];
        }
        if (k >= 5 && k <= 54) {
            speech_p += (double)(ere[k] * ere[k] + eim[k] * eim[k]);
            resid_p += (double)(pr * 0.02f + 1.0f);
        }
    }
    fft256(ere, eim, 1);
    for (int i = 0; i < BLK; i++) {
        e_blk[i] = g_ola[i] + ere[i];
        g_ola[i] = ere[i + BLK];
    }

    /* Speech-band SNR & Barge-In VAD detector */
    float noise_p = (g_noise_env * g_noise_env) * 50.0f * (float)FFT_N + 1e4f;
    float snr_lin = (float)(speech_p / (noise_p + 1.0f));
    float snr_db = 10.0f * log10f(snr_lin + 0.01f);
    if (snr_db < -10.0f) snr_db = -10.0f;
    if (snr_db > 45.0f) snr_db = 45.0f;
    g_snr_speech_db = 0.85f * g_snr_speech_db + 0.15f * snr_db;

    if (ref_active && snr_lin > 4.2f && speech_p > resid_p * 0.25) {
        if (++g_barge_streak >= 4) {
            if (!g_barge_active) {
                g_barge_active = 1;
                FILE *bf = fopen(BARGE_FILE, "w");
                if (bf) { fprintf(bf, "%ld\n", (long)time(NULL)); fclose(bf); }
            }
        }
    } else {
        if (g_barge_streak > 0) g_barge_streak--;
        if (g_barge_streak == 0) g_barge_active = 0;
    }
}

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
static float g_lp34[VQE_MICS];
static double g_msq_acc[VQE_MICS];
static float g_m_ema[VQE_MICS];
static float g_m_noise[VQE_MICS];
static float g_m_weight[VQE_MICS] = {
    1.0f / VQE_MICS, 1.0f / VQE_MICS, 1.0f / VQE_MICS, 1.0f / VQE_MICS,
    1.0f / VQE_MICS, 1.0f / VQE_MICS, 1.0f / VQE_MICS
};
static int g_best_mic = 0;

/* ---- BNR + speech AGC state ---- */
static float g_nr_hist[NR_HOPS];
static int g_nr_idx = 0, g_nr_fill = 0;
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
    long lag_ms = g_delay * 1000 / SR;
    gtcrn_metrics_t gm;
    gtcrn_ns_get_metrics(&gm);
    FILE *f = fopen(STAT_FILE ".tmp", "w");
    if (f) {
        fprintf(f,
                "delay_ms=%ld\nreduction_db=%.1f\nref_active=%d\n"
                "gtcrn_enabled=%d\ngtcrn_active=%d\ngtcrn_mode=%s\n"
                "gtcrn_hops=%llu\ngtcrn_infer_ms_avg=%.3f\ngtcrn_infer_ms_max=%.3f\n"
                "gtcrn_rtf_avg=%.4f\ngtcrn_queue_ms=%.2f\ngtcrn_deadline_misses=%llu\ngtcrn_rss_kb=%ld\n",
                lag_ms, ratio_db, active,
                gm.enabled, gm.active, gm.mode,
                (unsigned long long)gm.hops, gm.infer_ms_avg, gm.infer_ms_max,
                gm.rtf_avg, gm.queue_ms, (unsigned long long)gm.deadline_misses, gm.rss_kb);
        fclose(f);
        rename(STAT_FILE ".tmp", STAT_FILE);
    }
    FILE *jf = fopen(VQE_JSON ".tmp", "w");
    if (jf) {
        fprintf(jf,
                "{\"erle_db\":%.1f,\"snr_db\":%.1f,\"lag_ms\":%ld,\"best_mic\":%d,\"barge_in\":%s,\"ref_active\":%s,"
                "\"gtcrn\":{\"enabled\":%s,\"active\":%s,\"mode\":\"%s\",\"hops\":%llu,"
                "\"infer_ms_last\":%.3f,\"infer_ms_avg\":%.3f,\"infer_ms_max\":%.3f,"
                "\"rtf_avg\":%.4f,\"rtf_max\":%.4f,\"queue_ms\":%.2f,\"deadline_misses\":%llu,\"rss_kb\":%ld}}\n",
                ratio_db, g_snr_speech_db, lag_ms, g_best_mic,
                g_barge_active ? "true" : "false",
                active ? "true" : "false",
                gm.enabled ? "true" : "false",
                gm.active ? "true" : "false",
                gm.mode,
                (unsigned long long)gm.hops,
                gm.infer_ms_last, gm.infer_ms_avg, gm.infer_ms_max,
                gm.rtf_avg, gm.rtf_max, gm.queue_ms,
                (unsigned long long)gm.deadline_misses, gm.rss_kb);
        fclose(jf);
        rename(VQE_JSON ".tmp", VQE_JSON);
    }
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
    if (argc == 4 && strcmp(argv[1], "--gtcrn-file") == 0) {
        return gtcrn_ns_process_raw_file(argv[2], argv[3]);
    }
    int use_stdin = (argc > 1 && strcmp(argv[1], "--stdin") == 0);
    init_stft();
    gtcrn_ns_init();
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
                chmod(REF_SOCK, 0600);
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
        float blk_e[BLK], blk_r[BLK], blk_mic[BLK];
        int blk_ref_active = 0;
        for (int i = 0; i < BLK; i++) {
            const int32_t *fr = in + i * DECIM * CAP_CH;
            float s16[VQE_MICS];
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
                /* Speech-band (~192..3400 Hz) energy per mic for proximity speech detection */
                g_lp34[m] += 0.58f * (x16 - g_lp34[m]);
                g_msq_acc[m] += (double)g_lp34[m] * g_lp34[m];
            }
            /* Listen across the array while idle; active speech selection below
             * switches to one capsule to avoid uncalibrated phase cancellation. */
            float mic = 0.0f;
            for (int m = 0; m < VQE_MICS; m++) {
                mic += g_m_weight[m] * s16[m];
            }

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
                blk_ref_active = 1;
            }
            blk_e[i] = e;
            blk_r[i] = r;
            blk_mic[i] = mic;

            g_macc += fabsf(mic);
            g_racc_env += fabsf(rraw);
            if (++g_ecnt == ENV_HOP) {
                /* Continuous per-channel SNR monitoring; speech uses the best capsule. */
                int cand1 = g_best_mic;
                float best_snr = -1.0f;
                int healthy[VQE_MICS];
                int healthy_cnt = 0;
                for (int m = 0; m < VQE_MICS; m++) {
                    float mr = sqrtf((float)(g_msq_acc[m] / ENV_HOP));
                    g_msq_acc[m] = 0.0;
                    g_m_ema[m] = 0.85f * g_m_ema[m] + 0.15f * mr;
                    if (mr < g_m_noise[m]) {
                        g_m_noise[m] = 0.85f * g_m_noise[m] + 0.15f * mr;
                    } else {
                        g_m_noise[m] = 0.998f * g_m_noise[m] + 0.002f * mr;
                    }
                    if (g_m_noise[m] < 10.0f) g_m_noise[m] = 10.0f;
                    if (g_m_noise[m] > 2000.0f) g_m_noise[m] = 2000.0f;
                    healthy[m] = (g_m_noise[m] < 1950.0f && (g_m_ema[m] >= 1.0f || g_ecount < 5)) ? 1 : 0;
                    if (healthy[m]) {
                        healthy_cnt++;
                        float snr = g_m_ema[m] / (g_m_noise[m] + 1.0f);
                        if (snr > best_snr) {
                            best_snr = snr;
                            cand1 = m;
                        }
                    }
                }
                int speech_now = (best_snr >= 2.2f && g_m_ema[cand1] >= 45.0f);
                if (speech_now) {
                    if (g_m_ema[g_best_mic] < 1.0f || g_m_ema[cand1] > g_m_ema[g_best_mic] * 1.15f) {
                        g_best_mic = cand1;
                    }
                }
                float w_sum = 0.0f;
                for (int m = 0; m < VQE_MICS; m++) {
                    float tgt = 0.0f;
                    if (speech_now) {
                        /* Do not sum spatially separated capsules without delay
                         * calibration: phase cancellation can erase speech bands. */
                        if (m == g_best_mic) tgt = 1.0f;
                    } else if (healthy_cnt > 0 && healthy[m]) {
                        tgt = 1.0f / (float)healthy_cnt;
                    } else if (m == g_best_mic) {
                        tgt = 1.0f;
                    }
                    float alpha = speech_now ? 1.0f : 0.05f;
                    g_m_weight[m] += alpha * (tgt - g_m_weight[m]);
                    w_sum += g_m_weight[m];
                }
                if (w_sum > 0.001f) {
                    for (int m = 0; m < VQE_MICS; m++) g_m_weight[m] /= w_sum;
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

        /* Apply Frequency-Domain STFT-Kalman Residual Echo Suppressor + Barge-In VAD */
        stft_kalman_process_block(blk_e, blk_r, blk_ref_active);

        /* Apply local streaming GTCRN neural noise suppressor (16 kHz, 16 ms hop).
         * Fail-safe: no-op if disabled, unavailable, or real-time deadline overrun occurs. */
        gtcrn_ns_process_128(blk_e);

        for (int i = 0; i < BLK; i++) {
            float e = blk_e[i];
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

            /* Keep whispered speech above the quantization/noise floor after
             * AEC. A lower noise-relative gate and extra headroom help VAD/STT
             * without applying gain to near-silence. */
            float agc_gate = fmaxf(g_noise_env * 1.55f, 70.0f);
            if (g_sig_env > agc_gate) {
                float agc_tgt = 2800.0f / g_sig_env;
                if (agc_tgt < 1.0f) agc_tgt = 1.0f;
                if (agc_tgt > 4.0f) agc_tgt = 4.0f;
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
            pw_in += (double)blk_mic[i] * blk_mic[i];
            pw_out += (double)o * o;
            g_post_acc += (double)ae;
        }

        stat_cnt += BLK;
        if (!use_stdin && stat_cnt >= SR / 2) {
            double db = 0;
            if (pw_out > 1.0 && pw_in > 1.0) db = 10.0 * log10(pw_in / pw_out);
            write_stat(db, ref_active_cnt > (SR / 8));
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
    gtcrn_ns_shutdown();
    return 0;
}
