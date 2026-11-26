/*
 * gtcrn_ns.c - Local streaming GTCRN neural noise suppressor for AtriOS (aarch64).
 *
 * Uses official Xiaobin-Rong/gtcrn streaming ONNX model (gtcrn_simple.onnx, MIT license,
 * 16 kHz, 512-pt STFT, 256-sample = 16 ms hop, sqrt-Hann analysis/synthesis window)
 * executed locally via dynamically loaded libonnxruntime.so (zero Python, zero cloud).
 *
 * Fail-safe guarantees:
 *  - Dynamically loads libonnxruntime.so via dlopen(); missing library/model falls back cleanly.
 *  - Instant rollback via /var/lib/atrios/gtcrn_disable or ATRIOS_GTCRN_DISABLE=1.
 *  - Real-time deadline watchdog: 8 consecutive hops > 16 ms engage automatic fallback_overrun.
 *  - Zero per-hop heap allocations in the real-time loop (pre-bound ping-pong OrtValue tensors).
 */
#define _GNU_SOURCE
#include "gtcrn_ns.h"

#include <dlfcn.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "third_party/onnxruntime_c_api.h"

#define GTCRN_FFT_N       512
#define GTCRN_HOP         256
#define GTCRN_BINS        257
#define GTCRN_BLOCK       128
#define GTCRN_DEFAULT_WET 0.72f
#define GTCRN_HOP_US      16000.0
#define GTCRN_MIX_LEN     (GTCRN_BINS * 2)
#define GTCRN_CONV_LEN    (2 * 1 * 16 * 16 * 33)
#define GTCRN_TRA_LEN     (2 * 3 * 1 * 1 * 16)
#define GTCRN_INTER_LEN   (2 * 1 * 33 * 16)
#define GTCRN_MAX_OVERRUN 8

#define DEFAULT_ORT_LIB   "/usr/lib/atrios/libonnxruntime.so"
#define DEFAULT_MODEL     "/usr/share/atrios/models/gtcrn/gtcrn_simple.onnx"
#define ROLLBACK_FLAG     "/var/lib/atrios/gtcrn_disable"

typedef const OrtApiBase *(ORT_API_CALL *ort_get_api_base_fn)(void);

static void *g_dl = NULL;
static const OrtApi *g_ort = NULL;
static OrtEnv *g_env = NULL;
static OrtSessionOptions *g_opts = NULL;
static OrtSession *g_sess = NULL;
static OrtMemoryInfo *g_mem = NULL;

static float g_mix_buf[GTCRN_MIX_LEN];
static float g_enh_buf[GTCRN_MIX_LEN];
static float g_conv_buf[2][GTCRN_CONV_LEN];
static float g_tra_buf[2][GTCRN_TRA_LEN];
static float g_inter_buf[2][GTCRN_INTER_LEN];

static OrtValue *g_mix_val = NULL;
static OrtValue *g_enh_val = NULL;
static OrtValue *g_conv_val[2] = {NULL, NULL};
static OrtValue *g_tra_val[2] = {NULL, NULL};
static OrtValue *g_inter_val[2] = {NULL, NULL};
static int g_ping = 0;

/* STFT / ISTFT state */
static float g_win[GTCRN_FFT_N];
static float g_in_win[GTCRN_FFT_N];
static float g_ola_tail[GTCRN_HOP];
static float g_prev_dry[GTCRN_HOP];
static float g_in_accum[GTCRN_HOP];
static float g_out_ready[GTCRN_HOP];
static int g_in_pos = 0;
static int g_out_pos = 0;
/* Keep the same output timeline after a runtime fallback. */
static int g_stream_started = 0;

/* Gentle level conditioning so whispered speech stays in GTCRN's trained dynamic range */
static float g_pre_gain = 1.0f;
static float g_prev_gain = 1.0f;
static float g_wet_mix = GTCRN_DEFAULT_WET;

/* Runtime state & metrics */
static int g_enabled = 0;
static int g_active = 0;
static char g_mode[32] = "fallback_unavailable";
static uint64_t g_hops = 0;
static double g_infer_us_last = 0.0;
static double g_infer_us_sum = 0.0;
static double g_infer_us_max = 0.0;
static uint64_t g_deadline_misses = 0;
static uint64_t g_consec_misses = 0;
static uint64_t g_fallback_events = 0;

static inline double now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e6 + (double)ts.tv_nsec * 1e-3;
}

static long read_rss_kb(void) {
    FILE *f = fopen("/proc/self/statm", "r");
    if (!f) return 0;
    long size_pages = 0, rss_pages = 0;
    int n = fscanf(f, "%ld %ld", &size_pages, &rss_pages);
    fclose(f);
    if (n != 2) return 0;
    long page_kb = sysconf(_SC_PAGESIZE) / 1024;
    return rss_pages * (page_kb > 0 ? page_kb : 4);
}

static void init_sqrt_hann(void) {
    const double two_pi = 6.28318530717958647692;
    for (int i = 0; i < GTCRN_FFT_N; i++) {
        double h = 0.5 * (1.0 - cos(two_pi * (double)i / (double)GTCRN_FFT_N));
        g_win[i] = (float)sqrt(h > 0.0 ? h : 0.0);
    }
}

static void fft512(float *re, float *im, int inv) {
    int j = 0;
    for (int i = 1; i < GTCRN_FFT_N; i++) {
        int bit = GTCRN_FFT_N >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            float tr = re[i]; re[i] = re[j]; re[j] = tr;
            float ti = im[i]; im[i] = im[j]; im[j] = ti;
        }
    }
    for (int len = 2; len <= GTCRN_FFT_N; len <<= 1) {
        double ang = (inv ? 2.0 : -2.0) * 3.14159265358979323846 / (double)len;
        float wlen_r = (float)cos(ang);
        float wlen_i = (float)sin(ang);
        int half = len >> 1;
        for (int i = 0; i < GTCRN_FFT_N; i += len) {
            float wr = 1.0f, wi = 0.0f;
            for (int k = 0; k < half; k++) {
                int u = i + k;
                int v = u + half;
                float vr = re[v] * wr - im[v] * wi;
                float vi = re[v] * wi + im[v] * wr;
                re[v] = re[u] - vr;
                im[v] = im[u] - vi;
                re[u] += vr;
                im[u] += vi;
                float nwr = wr * wlen_r - wi * wlen_i;
                wi = wr * wlen_i + wi * wlen_r;
                wr = nwr;
            }
        }
    }
    if (inv) {
        const float inv_n = 1.0f / (float)GTCRN_FFT_N;
        for (int i = 0; i < GTCRN_FFT_N; i++) {
            re[i] *= inv_n;
            im[i] *= inv_n;
        }
    }
}

static int check_ort_status(OrtStatus *st, const char *ctx) {
    if (!st) return 0;
    const char *msg = g_ort ? g_ort->GetErrorMessage(st) : "unknown";
    fprintf(stderr, "[gtcrn] ORT error (%s): %s\n", ctx, msg ? msg : "unknown");
    if (g_ort) g_ort->ReleaseStatus(st);
    return -1;
}

static void reset_streaming_state(void) {
    memset(g_conv_buf, 0, sizeof(g_conv_buf));
    memset(g_tra_buf, 0, sizeof(g_tra_buf));
    memset(g_inter_buf, 0, sizeof(g_inter_buf));
    memset(g_in_win, 0, sizeof(g_in_win));
    memset(g_ola_tail, 0, sizeof(g_ola_tail));
    memset(g_prev_dry, 0, sizeof(g_prev_dry));
    memset(g_in_accum, 0, sizeof(g_in_accum));
    memset(g_out_ready, 0, sizeof(g_out_ready));
    g_ping = 0;
    g_in_pos = 0;
    g_out_pos = GTCRN_BLOCK;
    g_pre_gain = 1.0f;
    g_prev_gain = 1.0f;
}

int gtcrn_ns_init(void) {
    init_sqrt_hann();
    reset_streaming_state();
    g_hops = 0;
    g_infer_us_last = 0.0;
    g_infer_us_sum = 0.0;
    g_infer_us_max = 0.0;
    g_deadline_misses = 0;
    g_consec_misses = 0;
    g_fallback_events = 0;
    g_active = 0;
    g_stream_started = 0;
    g_wet_mix = GTCRN_DEFAULT_WET;

    const char *disable_env = getenv("ATRIOS_GTCRN_DISABLE");
    if ((disable_env && strcmp(disable_env, "1") == 0) || access(ROLLBACK_FLAG, F_OK) == 0) {
        g_enabled = 0;
        snprintf(g_mode, sizeof(g_mode), "%s", "disabled_rollback");
        fprintf(stderr, "[gtcrn] disabled via rollback flag; using existing AEC/BNR\n");
        return 0;
    }

    const char *wet_env = getenv("ATRIOS_GTCRN_WET");
    if (wet_env && wet_env[0]) {
        char *end;
        float w = strtof(wet_env, &end);
        if (end != wet_env && *end == '\0' && w >= 0.0f && w <= 1.0f) g_wet_mix = w;
    }

    g_enabled = 1;
    const char *lib_path = getenv("ATRIOS_ORT_LIB");
    if (!lib_path || !lib_path[0]) lib_path = DEFAULT_ORT_LIB;
    const char *model_path = getenv("ATRIOS_GTCRN_MODEL");
    if (!model_path || !model_path[0]) model_path = DEFAULT_MODEL;

    if (access(lib_path, R_OK) != 0 || access(model_path, R_OK) != 0) {
        snprintf(g_mode, sizeof(g_mode), "%s", "fallback_unavailable");
        fprintf(stderr, "[gtcrn] runtime/model unavailable (lib=%s model=%s); fallback to AEC/BNR\n",
                lib_path, model_path);
        return 0;
    }

    g_dl = dlopen(lib_path, RTLD_NOW | RTLD_LOCAL);
    if (!g_dl) {
        snprintf(g_mode, sizeof(g_mode), "%s", "fallback_unavailable");
        fprintf(stderr, "[gtcrn] dlopen(%s) failed: %s; fallback to AEC/BNR\n", lib_path, dlerror());
        return 0;
    }

    ort_get_api_base_fn get_base = (ort_get_api_base_fn)dlsym(g_dl, "OrtGetApiBase");
    if (!get_base) {
        snprintf(g_mode, sizeof(g_mode), "%s", "fallback_unavailable");
        fprintf(stderr, "[gtcrn] OrtGetApiBase symbol missing; fallback to AEC/BNR\n");
        gtcrn_ns_shutdown();
        return 0;
    }

    const OrtApiBase *base = get_base();
    if (!base) {
        snprintf(g_mode, sizeof(g_mode), "%s", "fallback_unavailable");
        gtcrn_ns_shutdown();
        return 0;
    }

    /* Request ORT API v17 (compatible across ORT 1.17 .. 1.28+) or fall back to header version */
    g_ort = base->GetApi(17);
    if (!g_ort) g_ort = base->GetApi(ORT_API_VERSION);
    if (!g_ort) {
        snprintf(g_mode, sizeof(g_mode), "%s", "fallback_unavailable");
        fprintf(stderr, "[gtcrn] ORT GetApi failed (runtime=%s); fallback to AEC/BNR\n",
                base->GetVersionString ? base->GetVersionString() : "?");
        gtcrn_ns_shutdown();
        return 0;
    }

    int intra_threads = 1;
    const char *thr_env = getenv("ATRIOS_GTCRN_THREADS");
    if (thr_env && thr_env[0]) {
        int t = atoi(thr_env);
        if (t >= 1 && t <= 4) intra_threads = t;
    }

    if (check_ort_status(g_ort->CreateEnv(ORT_LOGGING_LEVEL_ERROR, "atrios_gtcrn", &g_env), "CreateEnv") != 0 ||
        check_ort_status(g_ort->CreateSessionOptions(&g_opts), "CreateSessionOptions") != 0 ||
        check_ort_status(g_ort->SetIntraOpNumThreads(g_opts, intra_threads), "SetIntraOpNumThreads") != 0 ||
        check_ort_status(g_ort->SetInterOpNumThreads(g_opts, 1), "SetInterOpNumThreads") != 0 ||
        check_ort_status(g_ort->SetSessionExecutionMode(g_opts, ORT_SEQUENTIAL), "SetSessionExecutionMode") != 0 ||
        check_ort_status(g_ort->SetSessionGraphOptimizationLevel(g_opts, ORT_ENABLE_ALL), "SetGraphOpt") != 0 ||
        check_ort_status(g_ort->CreateSession(g_env, model_path, g_opts, &g_sess), "CreateSession") != 0 ||
        check_ort_status(g_ort->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &g_mem), "CreateCpuMemoryInfo") != 0) {
        snprintf(g_mode, sizeof(g_mode), "%s", "fallback_unavailable");
        gtcrn_ns_shutdown();
        return 0;
    }

    const int64_t mix_shape[4]   = {1, 257, 1, 2};
    const int64_t conv_shape[5]  = {2, 1, 16, 16, 33};
    const int64_t tra_shape[5]   = {2, 3, 1, 1, 16};
    const int64_t inter_shape[4] = {2, 1, 33, 16};

    if (check_ort_status(g_ort->CreateTensorWithDataAsOrtValue(g_mem, g_mix_buf, sizeof(g_mix_buf), mix_shape, 4, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &g_mix_val), "mix") != 0 ||
        check_ort_status(g_ort->CreateTensorWithDataAsOrtValue(g_mem, g_enh_buf, sizeof(g_enh_buf), mix_shape, 4, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &g_enh_val), "enh") != 0) {
        snprintf(g_mode, sizeof(g_mode), "%s", "fallback_unavailable");
        gtcrn_ns_shutdown();
        return 0;
    }

    for (int b = 0; b < 2; b++) {
        if (check_ort_status(g_ort->CreateTensorWithDataAsOrtValue(g_mem, g_conv_buf[b], sizeof(g_conv_buf[b]), conv_shape, 5, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &g_conv_val[b]), "conv") != 0 ||
            check_ort_status(g_ort->CreateTensorWithDataAsOrtValue(g_mem, g_tra_buf[b], sizeof(g_tra_buf[b]), tra_shape, 5, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &g_tra_val[b]), "tra") != 0 ||
            check_ort_status(g_ort->CreateTensorWithDataAsOrtValue(g_mem, g_inter_buf[b], sizeof(g_inter_buf[b]), inter_shape, 4, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &g_inter_val[b]), "inter") != 0) {
            snprintf(g_mode, sizeof(g_mode), "%s", "fallback_unavailable");
            gtcrn_ns_shutdown();
            return 0;
        }
    }

    g_active = 1;
    g_stream_started = 1;
    snprintf(g_mode, sizeof(g_mode), "%s", "active");
    fprintf(stderr, "[gtcrn] streaming GTCRN active (ort=%s model=%s wet=%.2f rss_kb=%ld)\n",
            base->GetVersionString ? base->GetVersionString() : "?",
            model_path, g_wet_mix, read_rss_kb());
    return 1;
}

static int gtcrn_run_hop(const float *hop_in_s16, float *hop_out_s16) {
    /* Compute RMS to gently boost whispered speech into GTCRN's trained level range */
    double sum_sq = 0.0;
    for (int i = 0; i < GTCRN_HOP; i++) {
        sum_sq += (double)hop_in_s16[i] * hop_in_s16[i];
    }
    float rms = sqrtf((float)(sum_sq / (double)GTCRN_HOP));
    float tgt_gain = 1.0f;
    if (rms > 25.0f && rms < 1800.0f) {
        tgt_gain = 1800.0f / rms;
        if (tgt_gain > 4.0f) tgt_gain = 4.0f;
    }
    float alpha = (tgt_gain < g_pre_gain) ? 0.35f : 0.08f;
    g_pre_gain += alpha * (tgt_gain - g_pre_gain);
    const float inv_scale = g_pre_gain / 32768.0f;
    /* The reconstructed hop belongs to the previous input window. */
    const float out_scale = 32768.0f / g_prev_gain;
    g_prev_gain = g_pre_gain;

    /* Shift 512-sample analysis window by 256 samples */
    memmove(g_in_win, g_in_win + GTCRN_HOP, GTCRN_HOP * sizeof(float));
    for (int i = 0; i < GTCRN_HOP; i++) {
        float x = hop_in_s16[i] * inv_scale;
        if (x > 1.0f) x = 1.0f;
        if (x < -1.0f) x = -1.0f;
        g_in_win[GTCRN_HOP + i] = x;
    }

    float re[GTCRN_FFT_N], im[GTCRN_FFT_N];
    for (int i = 0; i < GTCRN_FFT_N; i++) {
        re[i] = g_in_win[i] * g_win[i];
        im[i] = 0.0f;
    }
    fft512(re, im, 0);

    for (int k = 0; k < GTCRN_BINS; k++) {
        g_mix_buf[k * 2]     = re[k];
        g_mix_buf[k * 2 + 1] = im[k];
    }

    static const char *in_names[4]  = {"mix", "conv_cache", "tra_cache", "inter_cache"};
    static const char *out_names[4] = {"enh", "conv_cache_out", "tra_cache_out", "inter_cache_out"};
    const int cur = g_ping;
    const int nxt = cur ^ 1;
    const OrtValue *inputs[4] = {g_mix_val, g_conv_val[cur], g_tra_val[cur], g_inter_val[cur]};
    OrtValue *outputs[4]      = {g_enh_val, g_conv_val[nxt], g_tra_val[nxt], g_inter_val[nxt]};

    double t0 = now_us();
    OrtStatus *st = g_ort->Run(g_sess, NULL, in_names, inputs, 4, out_names, 4, outputs);
    double dt = now_us() - t0;

    if (st != NULL) {
        check_ort_status(st, "Run");
        g_active = 0;
        g_fallback_events++;
        snprintf(g_mode, sizeof(g_mode), "%s", "fallback_runtime_error");
        return 0;
    }
    g_ping = nxt;

    /* Update live real-time metrics */
    g_hops++;
    g_infer_us_last = dt;
    g_infer_us_sum += dt;
    if (dt > g_infer_us_max) g_infer_us_max = dt;
    if (dt > GTCRN_HOP_US) {
        g_deadline_misses++;
        g_consec_misses++;
        if (g_consec_misses >= GTCRN_MAX_OVERRUN) {
            g_active = 0;
            g_fallback_events++;
            snprintf(g_mode, sizeof(g_mode), "%s", "fallback_overrun");
            fprintf(stderr, "[gtcrn] real-time overrun (%llu consecutive hops > 16ms, last=%.2fms); engaging fallback to AEC/BNR\n",
                    (unsigned long long)g_consec_misses, dt / 1000.0);
            return 0;
        }
    } else {
        g_consec_misses = 0;
    }

    /* Reconstruct Hermitian spectrum & run 512-pt IFFT + sqrt-Hann 50% OLA */
    for (int k = 0; k < GTCRN_BINS; k++) {
        re[k] = g_enh_buf[k * 2];
        im[k] = g_enh_buf[k * 2 + 1];
    }
    for (int k = 1; k < GTCRN_FFT_N / 2; k++) {
        re[GTCRN_FFT_N - k] = re[k];
        im[GTCRN_FFT_N - k] = -im[k];
    }
    fft512(re, im, 1);

    for (int i = 0; i < GTCRN_HOP; i++) {
        float y = (g_ola_tail[i] + re[i] * g_win[i]) * out_scale;
        if (!isfinite(y)) {
            g_active = 0;
            g_fallback_events++;
            snprintf(g_mode, sizeof(g_mode), "%s", "fallback_runtime_error");
            fprintf(stderr, "[gtcrn] non-finite output; fallback to AEC/BNR\n");
            return 0;
        }
        g_ola_tail[i] = re[i + GTCRN_HOP] * g_win[i + GTCRN_HOP];
        hop_out_s16[i] = y;
    }
    return 1;
}

static void gtcrn_render_hop(const float *in, float *out) {
    if (g_active && gtcrn_run_hop(in, out)) {
        for (int i = 0; i < GTCRN_HOP; i++) {
            out[i] = g_wet_mix * out[i] + (1.0f - g_wet_mix) * g_prev_dry[i];
        }
    } else {
        memcpy(out, g_prev_dry, sizeof(g_prev_dry));
    }
    memcpy(g_prev_dry, in, sizeof(g_prev_dry));
}

int gtcrn_ns_is_active(void) {
    return g_active;
}

void gtcrn_ns_process_128(float *blk128) {
    if (!g_stream_started || !blk128) return;

    memcpy(g_in_accum + g_in_pos, blk128, GTCRN_BLOCK * sizeof(float));
    g_in_pos += GTCRN_BLOCK;
    if (g_in_pos == GTCRN_HOP) {
        gtcrn_render_hop(g_in_accum, g_out_ready);
        g_in_pos = 0;
        g_out_pos = 0;
    }

    memcpy(blk128, g_out_ready + g_out_pos, GTCRN_BLOCK * sizeof(float));
    g_out_pos += GTCRN_BLOCK;
}

void gtcrn_ns_get_metrics(gtcrn_metrics_t *out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->enabled = g_enabled;
    out->active = g_active;
    snprintf(out->mode, sizeof(out->mode), "%s", g_mode);
    out->hops = g_hops;
    out->infer_ms_last = g_infer_us_last / 1000.0;
    out->infer_ms_avg = (g_hops > 0) ? (g_infer_us_sum / (double)g_hops / 1000.0) : 0.0;
    out->infer_ms_max = g_infer_us_max / 1000.0;
    out->rtf_avg = out->infer_ms_avg / 16.0;
    out->rtf_max = out->infer_ms_max / 16.0;
    out->queue_ms = g_stream_started ? (24.0 + (g_active ? out->infer_ms_last : 0.0)) : 0.0;
    out->deadline_misses = g_deadline_misses;
    out->fallback_events = g_fallback_events;
    out->rss_kb = read_rss_kb();
}

static int cmp_double(const void *a, const void *b) {
    double da = *(const double *)a;
    double db = *(const double *)b;
    return (da > db) - (da < db);
}

int gtcrn_ns_process_raw_file(const char *in_path, const char *out_path) {
    if (!gtcrn_ns_init() || !g_active) {
        fprintf(stderr, "[gtcrn-bench] GTCRN inactive (mode=%s)\n", g_mode);
        return 1;
    }
    FILE *fi = fopen(in_path, "rb");
    if (!fi) { perror("fopen input"); return 2; }
    FILE *fo = fopen(out_path, "wb");
    if (!fo) { perror("fopen output"); fclose(fi); return 3; }

    size_t cap = 4096, cnt = 0;
    double *hop_ms = (double *)malloc(cap * sizeof(double));
    int16_t s16_in[GTCRN_HOP], s16_out[GTCRN_HOP];
    float f_in[GTCRN_HOP], f_out[GTCRN_HOP];

    struct timespec cpu0, cpu1;
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &cpu0);

    size_t nread, pending = 0;
    /* Skip the leading STFT delay and flush the last input hop at EOF. */
    do {
        nread = fread(s16_in, sizeof(int16_t), GTCRN_HOP, fi);
        if (!nread && !pending) break;
        for (size_t i = 0; i < nread; i++) f_in[i] = (float)s16_in[i];
        for (size_t i = nread; i < GTCRN_HOP; i++) f_in[i] = 0.0f;
        const uint64_t hops_before = g_hops;
        gtcrn_render_hop(f_in, f_out);
        if (hop_ms && g_hops != hops_before) {
            if (cnt == cap) {
                cap *= 2;
                double *nb = (double *)realloc(hop_ms, cap * sizeof(double));
                if (nb) hop_ms = nb;
            }
            if (cnt < cap) hop_ms[cnt++] = g_infer_us_last / 1000.0;
        }
        for (size_t i = 0; i < pending; i++) {
            float v = f_out[i];
            if (v > 32767.0f) v = 32767.0f;
            if (v < -32768.0f) v = -32768.0f;
            s16_out[i] = (int16_t)v;
        }
        fwrite(s16_out, sizeof(int16_t), pending, fo);
        pending = nread;
    } while (nread);

    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &cpu1);
    double cpu_ms = ((double)(cpu1.tv_sec - cpu0.tv_sec) * 1e3) +
                    ((double)(cpu1.tv_nsec - cpu0.tv_nsec) * 1e-6);

    fclose(fi);
    fclose(fo);

    double p50 = 0.0, p95 = 0.0;
    if (hop_ms && cnt > 0) {
        qsort(hop_ms, cnt, sizeof(double), cmp_double);
        p50 = hop_ms[cnt / 2];
        p95 = hop_ms[(size_t)((cnt - 1) * 0.95)];
    }
    free(hop_ms);

    gtcrn_metrics_t m;
    gtcrn_ns_get_metrics(&m);
    double audio_ms = (double)m.hops * 16.0;
    double cpu_pct = (audio_ms > 0.0) ? (cpu_ms / audio_ms * 100.0) : 0.0;
    fprintf(stderr,
            "{\"mode\":\"%s\",\"hops\":%llu,\"audio_ms\":%.1f,\"infer_ms_avg\":%.3f,\"infer_ms_p50\":%.3f,\"infer_ms_p95\":%.3f,\"infer_ms_max\":%.3f,\"rtf_avg\":%.4f,\"rtf_max\":%.4f,\"queue_ms\":%.2f,\"deadline_misses\":%llu,\"cpu_ms\":%.1f,\"cpu_pct_1core\":%.2f,\"rss_kb\":%ld}\n",
            m.mode, (unsigned long long)m.hops, audio_ms, m.infer_ms_avg, p50, p95, m.infer_ms_max,
            m.rtf_avg, m.rtf_max, m.queue_ms, (unsigned long long)m.deadline_misses, cpu_ms, cpu_pct, m.rss_kb);
    gtcrn_ns_shutdown();
    return 0;
}

void gtcrn_ns_shutdown(void) {
    if (g_ort) {
        if (g_mix_val) { g_ort->ReleaseValue(g_mix_val); g_mix_val = NULL; }
        if (g_enh_val) { g_ort->ReleaseValue(g_enh_val); g_enh_val = NULL; }
        for (int b = 0; b < 2; b++) {
            if (g_conv_val[b])  { g_ort->ReleaseValue(g_conv_val[b]);  g_conv_val[b] = NULL; }
            if (g_tra_val[b])   { g_ort->ReleaseValue(g_tra_val[b]);   g_tra_val[b] = NULL; }
            if (g_inter_val[b]) { g_ort->ReleaseValue(g_inter_val[b]); g_inter_val[b] = NULL; }
        }
        if (g_mem)  { g_ort->ReleaseMemoryInfo(g_mem); g_mem = NULL; }
        if (g_sess) { g_ort->ReleaseSession(g_sess); g_sess = NULL; }
        if (g_opts) { g_ort->ReleaseSessionOptions(g_opts); g_opts = NULL; }
        if (g_env)  { g_ort->ReleaseEnv(g_env); g_env = NULL; }
        g_ort = NULL;
    }
    if (g_dl) {
        dlclose(g_dl);
        g_dl = NULL;
    }
    g_active = 0;
    g_stream_started = 0;
}
