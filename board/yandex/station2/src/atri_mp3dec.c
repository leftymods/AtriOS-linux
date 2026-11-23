#define MINIMP3_IMPLEMENTATION
#include "/mnt/c/Users/ggala/piper_train_work/minimp3.h"
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#define INBUF_SZ (64 * 1024)
#define DUCK_FLAG_FILE "/var/run/atrios/ymusic_duck"
#define AUDIO_ENV_TMP "/var/run/atrios/ymusic_audio.tmp"
#define AUDIO_ENV_FILE "/var/run/atrios/ymusic_audio.json"

int main(void) {
    static uint8_t buf[INBUF_SZ];
    static mp3d_sample_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
    mp3dec_t mp3d;
    mp3dec_init(&mp3d);

    size_t filled = 0;
    int eof = 0;
    float cur_gain = 1.0f;
    unsigned int frame_cnt = 0;
    int ducked = 0;
    float lp_state = 0.0f;
    float smooth_bass = 0.0f;
    float smooth_mid = 0.0f;
    float smooth_eng = 0.0f;

    while (!eof || filled > 0) {
        if (!eof && filled < INBUF_SZ / 2) {
            size_t n = fread(buf + filled, 1, INBUF_SZ - filled, stdin);
            if (n == 0) eof = 1;
            else filled += n;
        }
        if (filled == 0) break;
        mp3dec_frame_info_t info;
        int samples = mp3dec_decode_frame(&mp3d, buf, (int)filled, pcm, &info);
        if (info.frame_bytes > 0) {
            size_t consume = (size_t)info.frame_bytes;
            if (consume >= filled) filled = 0;
            else {
                memmove(buf, buf + consume, filled - consume);
                filled -= consume;
            }
        } else if (eof) {
            break;
        } else {
            if (filled >= INBUF_SZ) {
                memmove(buf, buf + 1, filled - 1);
                filled--;
            }
            continue;
        }
        if (samples > 0) {
            int ch = (info.channels == 1) ? 1 : info.channels;
            int total = samples * ch;

            // Compute real-time bass (low-pass ~220Hz) & voice/mid energy for reactive UI blobs
            double sum_bass = 0.0, sum_mid = 0.0, sum_all = 0.0;
            const float alpha = 0.045f; // ~220Hz 1-pole LPF at 44.1kHz
            for (int i = 0; i < total; i += ch) {
                float x = (float)pcm[i] / 32768.0f;
                lp_state += alpha * (x - lp_state);
                float mid = x - lp_state;
                sum_bass += (double)(lp_state * lp_state);
                sum_mid += (double)(mid * mid);
                sum_all += (double)(x * x);
            }
            float inv_n = (samples > 0) ? (1.0f / (float)samples) : 1.0f;
            float bass = fminf(1.0f, sqrtf((float)sum_bass * inv_n) * 3.8f);
            float mid = fminf(1.0f, sqrtf((float)sum_mid * inv_n) * 3.2f);
            float eng = fminf(1.0f, sqrtf((float)sum_all * inv_n) * 2.8f);
            smooth_bass = smooth_bass * 0.55f + bass * 0.45f;
            smooth_mid = smooth_mid * 0.55f + mid * 0.45f;
            smooth_eng = smooth_eng * 0.55f + eng * 0.45f;

            if ((frame_cnt & 3u) == 0u) {
                FILE *fp = fopen(AUDIO_ENV_TMP, "w");
                if (fp) {
                    fprintf(fp, "{\"b\":%.2f,\"v\":%.2f,\"e\":%.2f}\n", smooth_bass, smooth_mid, smooth_eng);
                    fclose(fp);
                    rename(AUDIO_ENV_TMP, AUDIO_ENV_FILE);
                }
            }

            if ((frame_cnt++ & 1u) == 0u) {
                ducked = (access(DUCK_FLAG_FILE, F_OK) == 0) ? 1 : 0;
            }
            float target = ducked ? 0.35f : 1.0f;
            if (cur_gain != target || cur_gain < 0.999f) {
                float step = (target - cur_gain) / (float)(total > 0 ? total : 1);
                for (int i = 0; i < total; i++) {
                    cur_gain += step;
                    int32_t v = (int32_t)((float)pcm[i] * cur_gain);
                    if (v > 32767) v = 32767;
                    else if (v < -32768) v = -32768;
                    pcm[i] = (int16_t)v;
                }
                cur_gain = target;
            }
            if (info.channels == 1) {
                static int16_t stereo[MINIMP3_MAX_SAMPLES_PER_FRAME * 2];
                for (int i = 0; i < samples; i++) {
                    stereo[2 * i] = pcm[i];
                    stereo[2 * i + 1] = pcm[i];
                }
                fwrite(stereo, sizeof(int16_t), (size_t)samples * 2, stdout);
            } else {
                fwrite(pcm, sizeof(int16_t), (size_t)samples * (size_t)info.channels, stdout);
            }
        }
    }
    unlink(AUDIO_ENV_FILE);
    fflush(stdout);
    return 0;
}
