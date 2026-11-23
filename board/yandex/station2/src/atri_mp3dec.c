#define MINIMP3_IMPLEMENTATION
#include "/mnt/c/Users/ggala/piper_train_work/minimp3.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define INBUF_SZ (64 * 1024)

int main(void) {
    static uint8_t buf[INBUF_SZ];
    static mp3d_sample_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
    mp3dec_t mp3d;
    mp3dec_init(&mp3d);

    size_t filled = 0;
    int eof = 0;
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
            // Need more data or skip 1 garbage byte if buffer is full
            if (filled >= INBUF_SZ) {
                memmove(buf, buf + 1, filled - 1);
                filled--;
            }
            continue;
        }
        if (samples > 0) {
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
    fflush(stdout);
    return 0;
}
