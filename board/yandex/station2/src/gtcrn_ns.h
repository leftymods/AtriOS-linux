#ifndef ATRIOS_GTCRN_NS_H
#define ATRIOS_GTCRN_NS_H

#include <stdint.h>

typedef struct {
    int enabled;           /* 1 if GTCRN was requested (not disabled via rollback flag) */
    int active;            /* 1 if GTCRN is currently processing audio */
    char mode[32];         /* "active", "disabled_rollback", "fallback_unavailable", "fallback_overrun" */
    uint64_t hops;         /* total 16 ms hops processed */
    double infer_ms_last;
    double infer_ms_avg;
    double infer_ms_max;
    double rtf_avg;        /* infer_ms_avg / 16.0 */
    double rtf_max;        /* infer_ms_max / 16.0 */
    double queue_ms;       /* stream alignment (24 ms) + last inference latency */
    uint64_t deadline_misses;
    uint64_t fallback_events;
    long rss_kb;           /* process RSS from /proc/self/statm */
} gtcrn_metrics_t;

/* Initialize GTCRN streaming noise suppressor (dlopen libonnxruntime.so + load gtcrn_simple.onnx).
 * Never fails fatally: on missing runtime/model or rollback flag, sets fallback mode and returns 0. */
int gtcrn_ns_init(void);

/* Process 128 mono float samples (S16 scale, 16 kHz) in-place.
 * If initialization failed, leaves samples untouched. Runtime fallback emits
 * the delayed dry samples to preserve the live stream timeline. */
void gtcrn_ns_process_128(float *blk128);

/* Returns nonzero while neural suppression is active (zero on fallback). */
int gtcrn_ns_is_active(void);

/* Snapshot current metrics & status for /var/run/atrios/aec_vqe.json and /var/run/atrios/aec.stat. */
void gtcrn_ns_get_metrics(gtcrn_metrics_t *out);

/* Offline benchmark/filter mode: process 16 kHz mono S16_LE raw PCM file -> out_path
 * and print detailed latency/RTF/p95/CPU/RSS metrics to stderr. Returns 0 on success. */
int gtcrn_ns_process_raw_file(const char *in_path, const char *out_path);

/* Release ONNX Runtime resources cleanly on exit. */
void gtcrn_ns_shutdown(void);

#endif /* ATRIOS_GTCRN_NS_H */
