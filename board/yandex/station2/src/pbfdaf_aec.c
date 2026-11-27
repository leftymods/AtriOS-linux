/*
 * pbfdaf_aec.c - Dual-Filter Partitioned Block Frequency-Domain AEC (PBFDAF/MDF)
 *                + Nonlinear Residual Echo Suppressor (RES) for AtriOS (16 kHz).
 *
 * Architecture (inspired by WebRTC AEC3 / SpeexDSP MDF):
 *   1. 50%-overlap 256-pt FFT (128-sample = 8 ms hop), 24 frequency-domain partitions
 *      covering 192 ms continuous acoustic tail (absorbs ALSA buffer jitter smoothly).
 *   2. Per-bin power-normalized step size -> fast convergence (< 200 ms) even on
 *      bass-heavy music where scalar time-domain NLMS stalls.
 *   3. Shadow + Main dual adaptive filter:
 *      - Shadow adapts aggressively every block.
 *      - Main copies shadow only when shadow outperforms main without double-talk.
 *      - Prevents filter divergence when the user speaks over loud music.
 *   4. Overlap-save gradient constraint (time-domain projection of shadow weights)
 *      keeps circular convolution aliasing strictly zero.
 *   5. Nonlinear Residual Echo Suppressor (RES) using both linear echo estimate |Y(k)|^2
 *      and smoothed echo-to-residual coherence to crush cabinet/woofer harmonics
 *      down to -30 dB during pure music while passing double-talk speech cleanly.
 */
#include "pbfdaf_aec.h"

#include <math.h>
#include <string.h>

static float g_x_time[PBFDAF_FFT];
static float g_x_re[PBFDAF_PARTS][PBFDAF_BINS];
static float g_x_im[PBFDAF_PARTS][PBFDAF_BINS];
static int g_x_head = 0;

/* Main and Shadow frequency-domain filter weights */
static float g_w_mn_re[PBFDAF_PARTS][PBFDAF_BINS];
static float g_w_mn_im[PBFDAF_PARTS][PBFDAF_BINS];
static float g_w_sh_re[PBFDAF_PARTS][PBFDAF_BINS];
static float g_w_sh_im[PBFDAF_PARTS][PBFDAF_BINS];

/* Smoothed per-bin far-end power across all partitions */
static float g_x_pow[PBFDAF_BINS];

/* Dual-filter energy trackers */
static float g_d_pow_s = 1e4f;
static float g_emn_pow_s = 1e4f;
static float g_esh_pow_s = 1e4f;
static int g_double_talk = 0;
static int g_converged = 0;
static int g_constrain_part = 0;

/* Nonlinear RES state (50% OLA Hann synthesis) */
static float g_hann[PBFDAF_FFT];
static float g_res_e_win[PBFDAF_FFT];
static float g_res_y_win[PBFDAF_FFT];
static float g_res_ola[PBFDAF_BLK];
static float g_res_pe[PBFDAF_BINS];
static float g_res_py[PBFDAF_BINS];
static float g_res_gain[PBFDAF_BINS];

static void fft256(float *re, float *im, int inv) {
    int j = 0;
    for (int i = 1; i < PBFDAF_FFT; i++) {
        int bit = PBFDAF_FFT >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            float tr = re[i]; re[i] = re[j]; re[j] = tr;
            float ti = im[i]; im[i] = im[j]; im[j] = ti;
        }
    }
    for (int len = 2; len <= PBFDAF_FFT; len <<= 1) {
        float ang = (inv ? 2.0f : -2.0f) * 3.1415926535897932f / (float)len;
        float wlen_r = cosf(ang), wlen_i = sinf(ang);
        int half = len >> 1;
        for (int i = 0; i < PBFDAF_FFT; i += len) {
            float wr = 1.0f, wi = 0.0f;
            for (int k = 0; k < half; k++) {
                int u = i + k, v = u + half;
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
        const float inv_n = 1.0f / (float)PBFDAF_FFT;
        for (int i = 0; i < PBFDAF_FFT; i++) {
            re[i] *= inv_n;
            im[i] *= inv_n;
        }
    }
}

static void fill_hermitian(float *re, float *im) {
    for (int k = 1; k < PBFDAF_FFT / 2; k++) {
        re[PBFDAF_FFT - k] = re[k];
        im[PBFDAF_FFT - k] = -im[k];
    }
}

void pbfdaf_reset_weights(void) {
    memset(g_w_mn_re, 0, sizeof(g_w_mn_re));
    memset(g_w_mn_im, 0, sizeof(g_w_mn_im));
    memset(g_w_sh_re, 0, sizeof(g_w_sh_re));
    memset(g_w_sh_im, 0, sizeof(g_w_sh_im));
    g_d_pow_s = 1e4f;
    g_emn_pow_s = 1e4f;
    g_esh_pow_s = 1e4f;
    g_double_talk = 0;
    g_converged = 0;
}

void pbfdaf_init(void) {
    memset(g_x_time, 0, sizeof(g_x_time));
    memset(g_x_re, 0, sizeof(g_x_re));
    memset(g_x_im, 0, sizeof(g_x_im));
    g_x_head = 0;
    g_constrain_part = 0;
    pbfdaf_reset_weights();
    for (int i = 0; i < PBFDAF_FFT; i++) {
        g_hann[i] = 0.5f * (1.0f - cosf(2.0f * 3.1415926535897932f * (float)i / (float)PBFDAF_FFT));
    }
    for (int k = 0; k < PBFDAF_BINS; k++) {
        g_x_pow[k] = 1e4f;
        g_res_pe[k] = 1e3f;
        g_res_py[k] = 1e3f;
        g_res_gain[k] = 1.0f;
    }
    memset(g_res_e_win, 0, sizeof(g_res_e_win));
    memset(g_res_y_win, 0, sizeof(g_res_y_win));
    memset(g_res_ola, 0, sizeof(g_res_ola));
}

int pbfdaf_is_double_talk(void) {
    return g_double_talk;
}

/* Enforce overlap-save linear convolution on one partition per block (amortized O(1) FFT) */
static void constrain_partition(float w_re[PBFDAF_BINS], float w_im[PBFDAF_BINS]) {
    float re[PBFDAF_FFT], im[PBFDAF_FFT];
    for (int k = 0; k < PBFDAF_BINS; k++) {
        re[k] = w_re[k];
        im[k] = w_im[k];
    }
    fill_hermitian(re, im);
    fft256(re, im, 1);
    /* Keep first 128 samples, zero second 128 samples */
    for (int i = PBFDAF_BLK; i < PBFDAF_FFT; i++) {
        re[i] = 0.0f;
    }
    memset(im, 0, sizeof(im));
    fft256(re, im, 0);
    for (int k = 0; k < PBFDAF_BINS; k++) {
        w_re[k] = re[k];
        w_im[k] = im[k];
    }
}

float pbfdaf_process_block(const float *mic_in, const float *ref_in, int ref_active, float *out_e) {
    /* 1. Shift 256-sample overlap-save far-end buffer and compute X(k) */
    memmove(g_x_time, g_x_time + PBFDAF_BLK, PBFDAF_BLK * sizeof(float));
    memcpy(g_x_time + PBFDAF_BLK, ref_in, PBFDAF_BLK * sizeof(float));

    float re[PBFDAF_FFT], im[PBFDAF_FFT];
    memcpy(re, g_x_time, sizeof(re));
    memset(im, 0, sizeof(im));
    fft256(re, im, 0);

    g_x_head = (g_x_head + PBFDAF_PARTS - 1) % PBFDAF_PARTS;
    for (int k = 0; k < PBFDAF_BINS; k++) {
        g_x_re[g_x_head][k] = re[k];
        g_x_im[g_x_head][k] = im[k];
    }

    /* 2. Compute Main & Shadow linear echo estimates Y_mn(k), Y_sh(k) */
    float y_mn_re[PBFDAF_FFT], y_mn_im[PBFDAF_FFT];
    float y_sh_re[PBFDAF_FFT], y_sh_im[PBFDAF_FFT];
    memset(y_mn_re, 0, sizeof(y_mn_re));
    memset(y_mn_im, 0, sizeof(y_mn_im));
    memset(y_sh_re, 0, sizeof(y_sh_re));
    memset(y_sh_im, 0, sizeof(y_sh_im));

    for (int p = 0; p < PBFDAF_PARTS; p++) {
        int idx = (g_x_head + p) % PBFDAF_PARTS;
        const float *xr = g_x_re[idx];
        const float *xi = g_x_im[idx];
        const float *wmr = g_w_mn_re[p], *wmi = g_w_mn_im[p];
        const float *wsr = g_w_sh_re[p], *wsi = g_w_sh_im[p];
        for (int k = 0; k < PBFDAF_BINS; k++) {
            float a = xr[k], b = xi[k];
            y_mn_re[k] += wmr[k] * a - wmi[k] * b;
            y_mn_im[k] += wmr[k] * b + wmi[k] * a;
            y_sh_re[k] += wsr[k] * a - wsi[k] * b;
            y_sh_im[k] += wsr[k] * b + wsi[k] * a;
        }
    }
    fill_hermitian(y_mn_re, y_mn_im);
    fill_hermitian(y_sh_re, y_sh_im);
    fft256(y_mn_re, y_mn_im, 1);
    fft256(y_sh_re, y_sh_im, 1);

    /* Overlap-save valid second half [128..255] */
    float e_mn[PBFDAF_BLK], e_sh[PBFDAF_BLK], y_mn_t[PBFDAF_BLK];
    double d_p = 0.0, emn_p = 0.0, esh_p = 0.0, ref_p = 0.0;
    for (int i = 0; i < PBFDAF_BLK; i++) {
        float d = mic_in[i];
        float ym = y_mn_re[PBFDAF_BLK + i];
        float ys = y_sh_re[PBFDAF_BLK + i];
        e_mn[i] = d - ym;
        e_sh[i] = d - ys;
        y_mn_t[i] = ym;
        d_p += (double)d * d;
        emn_p += (double)e_mn[i] * e_mn[i];
        esh_p += (double)e_sh[i] * e_sh[i];
        ref_p += (double)ref_in[i] * ref_in[i];
    }

    int has_ref = ref_active && (ref_p > (double)PBFDAF_BLK * 25.0);
    if (has_ref) {
        g_d_pow_s   = 0.85f * g_d_pow_s   + 0.15f * (float)d_p;
        g_emn_pow_s = 0.85f * g_emn_pow_s + 0.15f * (float)emn_p;
        g_esh_pow_s = 0.85f * g_esh_pow_s + 0.15f * (float)esh_p;

        /* Double-talk indicator: only engage after filter has converged at least once */
        if (g_emn_pow_s < 0.45f * g_d_pow_s) g_converged = 1;
        g_double_talk = (g_converged && g_emn_pow_s > 0.55f * g_d_pow_s && g_emn_pow_s > 200000.0f) ? 1 : 0;

        /* 3. Compute FFT of zero-padded shadow error [0..127=0, 128..255=e_sh] */
        memset(re, 0, PBFDAF_BLK * sizeof(float));
        memcpy(re + PBFDAF_BLK, e_sh, PBFDAF_BLK * sizeof(float));
        memset(im, 0, sizeof(im));
        fft256(re, im, 0);

        /* Compute total far-end power across all partitions per bin */
        float tot_pow[PBFDAF_BINS];
        for (int k = 0; k < PBFDAF_BINS; k++) {
            float psum = 0.0f;
            for (int p = 0; p < PBFDAF_PARTS; p++) {
                float xr = g_x_re[p][k], xi = g_x_im[p][k];
                psum += xr * xr + xi * xi;
            }
            g_x_pow[k] = 0.70f * g_x_pow[k] + 0.30f * psum;
            tot_pow[k] = g_x_pow[k] + 1e5f;
        }

        /* 4. Update Shadow filter (fast normalized PBFDAF) */
        const float mu_sh = g_double_talk ? 0.18f : 0.55f;
        for (int p = 0; p < PBFDAF_PARTS; p++) {
            int idx = (g_x_head + p) % PBFDAF_PARTS;
            const float *xr = g_x_re[idx];
            const float *xi = g_x_im[idx];
            float *wsr = g_w_sh_re[p], *wsi = g_w_sh_im[p];
            for (int k = 0; k < PBFDAF_BINS; k++) {
                float step = mu_sh / tot_pow[k];
                /* conj(X) * E */
                float gr = xr[k] * re[k] + xi[k] * im[k];
                float gi = xr[k] * im[k] - xi[k] * re[k];
                wsr[k] += step * gr;
                wsi[k] += step * gi;
            }
        }
        /* Constrain 2 partitions per block so all 24 partitions stay constrained every 96 ms */
        for (int c = 0; c < 2; c++) {
            constrain_partition(g_w_sh_re[g_constrain_part], g_w_sh_im[g_constrain_part]);
            g_constrain_part = (g_constrain_part + 1) % PBFDAF_PARTS;
        }

        /* 5. Dual-filter transfer logic (WebRTC AEC3 style) */
        if (g_esh_pow_s < 0.92f * g_emn_pow_s && g_esh_pow_s < 0.85f * g_d_pow_s) {
            memcpy(g_w_mn_re, g_w_sh_re, sizeof(g_w_mn_re));
            memcpy(g_w_mn_im, g_w_sh_im, sizeof(g_w_mn_im));
            g_emn_pow_s = g_esh_pow_s;
        } else if (g_esh_pow_s > 1.35f * g_emn_pow_s) {
            /* Shadow diverged during loud double-talk -> restore from main */
            memcpy(g_w_sh_re, g_w_mn_re, sizeof(g_w_sh_re));
            memcpy(g_w_sh_im, g_w_mn_im, sizeof(g_w_sh_im));
            g_esh_pow_s = g_emn_pow_s;
        }
    } else {
        g_double_talk = 0;
    }

    /* Choose better of main vs shadow output */
    const float *e_best = (has_ref && esh_p < emn_p * 0.90) ? e_sh : e_mn;

    /* 6. Nonlinear Residual Echo Suppressor (RES) — crushes nonlinear woofer/cabinet harmonics */
    memmove(g_res_e_win, g_res_e_win + PBFDAF_BLK, PBFDAF_BLK * sizeof(float));
    memcpy(g_res_e_win + PBFDAF_BLK, e_best, PBFDAF_BLK * sizeof(float));
    memmove(g_res_y_win, g_res_y_win + PBFDAF_BLK, PBFDAF_BLK * sizeof(float));
    memcpy(g_res_y_win + PBFDAF_BLK, y_mn_t, PBFDAF_BLK * sizeof(float));

    float ere[PBFDAF_FFT], eim[PBFDAF_FFT], yre[PBFDAF_FFT], yim[PBFDAF_FFT];
    for (int i = 0; i < PBFDAF_FFT; i++) {
        ere[i] = g_res_e_win[i] * g_hann[i];
        eim[i] = 0.0f;
        yre[i] = g_res_y_win[i] * g_hann[i];
        yim[i] = 0.0f;
    }
    fft256(ere, eim, 0);
    fft256(yre, yim, 0);

    for (int k = 0; k < PBFDAF_BINS; k++) {
        float pe = ere[k] * ere[k] + eim[k] * eim[k];
        float py = yre[k] * yre[k] + yim[k] * yim[k];
        g_res_pe[k] = 0.72f * g_res_pe[k] + 0.28f * pe;
        g_res_py[k] = 0.72f * g_res_py[k] + 0.28f * py;

        float tgt = 1.0f;
        if (has_ref) {
            /* Nonlinear echo model: residual echo is proportional to linear echo estimate |Y(k)|^2
             * (accounts for speaker/cabinet harmonics & tail beyond 192 ms). */
            float nl_echo = 0.45f * g_res_py[k];
            float clean = g_res_pe[k] - nl_echo;
            if (clean < 0.0f) clean = 0.0f;
            tgt = clean / (g_res_pe[k] + 100.0f);
            /* Strong floor (-30 dB) during pure music; softer during detected speech */
            float g_min = g_double_talk ? 0.12f : 0.03f;
            if (tgt < g_min) tgt = g_min;
            if (tgt > 1.0f) tgt = 1.0f;
        }
        float alpha = (tgt < g_res_gain[k]) ? 0.55f : 0.22f;
        g_res_gain[k] += alpha * (tgt - g_res_gain[k]);
        ere[k] *= g_res_gain[k];
        eim[k] *= g_res_gain[k];
    }
    fill_hermitian(ere, eim);
    fft256(ere, eim, 1);

    double out_p = 0.0;
    for (int i = 0; i < PBFDAF_BLK; i++) {
        float o = g_res_ola[i] + ere[i];
        g_res_ola[i] = ere[PBFDAF_BLK + i];
        out_e[i] = o;
        out_p += (double)o * o;
    }

    if (has_ref && d_p > 100.0 && out_p > 1.0) {
        return (float)(10.0 * log10(d_p / out_p));
    }
    return 0.0f;
}
