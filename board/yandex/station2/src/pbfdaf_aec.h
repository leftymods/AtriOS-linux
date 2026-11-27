#ifndef ATRIOS_PBFDAF_AEC_H
#define ATRIOS_PBFDAF_AEC_H

/*
 * Dual-Filter Partitioned Block Frequency-Domain Adaptive Filter (PBFDAF / MDF)
 * + Nonlinear Residual Echo Suppressor (WebRTC AEC3-style) for AtriOS.
 *
 * Block size: 128 samples (8 ms @ 16 kHz)
 * FFT size:   256 (50% overlap-save, 129 complex bins)
 * Partitions: 24 (192 ms continuous linear echo tail)
 */

#define PBFDAF_BLK   128
#define PBFDAF_FFT   256
#define PBFDAF_BINS  129
#define PBFDAF_PARTS 24

void pbfdaf_init(void);
void pbfdaf_reset_weights(void);

/*
 * Process one 128-sample (8 ms @ 16 kHz) block.
 * - mic_in[128]:  near-end microphone signal (16 kHz float, S16 scale)
 * - ref_in[128]:  far-end speaker reference signal (16 kHz float, S16 scale)
 * - ref_active:   1 if speaker reference has active audio, 0 if silent
 * - out_e[128]:   echo-cancelled output (linear PBFDAF + nonlinear RES)
 * Returns estimated instantaneous ERLE in dB for telemetry.
 */
float pbfdaf_process_block(const float *mic_in, const float *ref_in, int ref_active, float *out_e);

/* Returns 1 if double-talk (near-end speech over active speaker music) is detected */
int pbfdaf_is_double_talk(void);

#endif /* ATRIOS_PBFDAF_AEC_H */
