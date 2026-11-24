#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define INBUF_SZ (64 * 1024)
#define DUCK_FLAG_FILE "/var/run/atrios/ymusic_duck"
#define AUDIO_ENV_TMP "/var/run/atrios/ymusic_audio.tmp"
#define AUDIO_ENV_FILE "/var/run/atrios/ymusic_audio.json"
#define AEC_REF_SOCK "/var/run/atrios/aec_ref.sock"

static void send_aec_ref(int fd, const struct sockaddr_un *addr, const int16_t *stereo, size_t n_frames) {
    if (fd < 0 || n_frames == 0) return;
    const uint8_t *p = (const uint8_t *)stereo;
    size_t rem = n_frames * 4;
    while (rem > 0) {
        size_t chunk = (rem > 4096) ? 4096 : rem;
        (void)sendto(fd, p, chunk, MSG_DONTWAIT, (const struct sockaddr *)addr, sizeof(*addr));
        p += chunk;
        rem -= chunk;
    }
}

int main(void) {
    static uint8_t buf[INBUF_SZ];
    static mp3d_sample_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
    mp3dec_t mp3d;
    mp3dec_init(&mp3d);

    int aec_fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    struct sockaddr_un aec_addr;
    memset(&aec_addr, 0, sizeof(aec_addr));
    aec_addr.sun_family = AF_UNIX;
    strncpy(aec_addr.sun_path, AEC_REF_SOCK, sizeof(aec_addr.sun_path) - 1);

    size_t filled = 0;
    int eof = 0;
    float cur_gain = 1.0f;
    unsigned int frame_cnt = 0;
    int ducked = 0;
    float lp_bass = 0.0f;
    float lp_mid = 0.0f;
    float prev_x = 0.0f;
    float smooth_bass = 0.0f;
    float smooth_mid = 0.0f;
    float smooth_high = 0.0f;
    float smooth_eng = 0.0f;
    float avg_bass = 0.0f;
    float avg_eng = 0.0f;
    float smooth_beat = 0.0f;

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

            // 4-band full-spectrum + beat transient analyzer for Liquid Plasma blob
            double sum_bass = 0.0, sum_mid = 0.0, sum_high = 0.0, sum_all = 0.0;
            const float alpha_bass = 0.036f; // ~200Hz LPF
            const float alpha_mid = 0.28f;   // ~2.4kHz LPF
            for (int i = 0; i < total; i += ch) {
                float x = (float)pcm[i] / 32768.0f;
                lp_bass += alpha_bass * (x - lp_bass);
                lp_mid += alpha_mid * (x - lp_mid);
                float mid = lp_mid - lp_bass;
                float high = (x - lp_mid) * 0.7f + (x - prev_x) * 0.3f;
                prev_x = x;
                sum_bass += (double)(lp_bass * lp_bass);
                sum_mid += (double)(mid * mid);
                sum_high += (double)(high * high);
                sum_all += (double)(x * x);
            }
            float inv_n = (samples > 0) ? (1.0f / (float)samples) : 1.0f;
            float bass = fminf(1.0f, sqrtf((float)sum_bass * inv_n) * 4.0f);
            float mid = fminf(1.0f, sqrtf((float)sum_mid * inv_n) * 3.8f);
            float high = fminf(1.0f, sqrtf((float)sum_high * inv_n) * 5.2f);
            float eng = fminf(1.0f, sqrtf((float)sum_all * inv_n) * 3.0f);
            smooth_bass = smooth_bass * 0.50f + bass * 0.50f;
            smooth_mid = smooth_mid * 0.52f + mid * 0.48f;
            smooth_high = smooth_high * 0.45f + high * 0.55f;
            smooth_eng = smooth_eng * 0.55f + eng * 0.45f;
            avg_bass = avg_bass * 0.92f + bass * 0.08f;
            avg_eng = avg_eng * 0.92f + eng * 0.08f;
            float flux = fmaxf(0.0f, (bass - avg_bass) * 3.2f + (eng - avg_eng) * 1.8f);
            float beat = fminf(1.0f, flux);
            smooth_beat = (beat > smooth_beat) ? beat : (smooth_beat * 0.78f);

            if ((frame_cnt & 3u) == 0u) {
                FILE *fp = fopen(AUDIO_ENV_TMP, "w");
                if (fp) {
                    fprintf(fp, "{\"b\":%.2f,\"v\":%.2f,\"h\":%.2f,\"e\":%.2f,\"p\":%.2f}\n",
                        smooth_bass, smooth_mid, smooth_high, smooth_eng, smooth_beat);
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
                send_aec_ref(aec_fd, &aec_addr, stereo, (size_t)samples);
                fwrite(stereo, sizeof(int16_t), (size_t)samples * 2, stdout);
            } else {
                send_aec_ref(aec_fd, &aec_addr, pcm, (size_t)samples);
                fwrite(pcm, sizeof(int16_t), (size_t)samples * (size_t)info.channels, stdout);
            }
        }
    }
    unlink(AUDIO_ENV_FILE);
    fflush(stdout);
    return 0;
}
