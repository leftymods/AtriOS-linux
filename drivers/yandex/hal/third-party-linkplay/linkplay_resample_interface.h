#pragma once

#ifndef _AF_LAVCRESAMPLE_
#define _AF_LAVCRESAMPLE_

// Audio data chunk
typedef struct af_data_s {
    void *audio;  // data buffer
    int len;      // buffer length
    int rate; // sample rate
    int nch;  // number of channels
    int format;   // format
    int bps;  // bytes per sample
} af_data_t;

// Linked list of audio filters
typedef struct af_instance_s {
//  const af_info_t* info;
    int (*control)(struct af_instance_s *af, int cmd, void *arg);
    void (*uninit)(struct af_instance_s *af);
    af_data_t *(*play)(struct af_instance_s *af, af_data_t *data);
    void *setup;    // setup data for this specific instance and filter
    af_data_t *data; // configuration for outgoing data stream
    struct af_instance_s *next;
    struct af_instance_s *prev;
    double delay; /* Delay caused by the filter, in units of bytes read without
         * corresponding output */
    double mul; /* length multiplier: how much does this instance change
         the length of the buffer. */
} af_instance_t;
#endif // _AF_LAVCRESAMPLE_

#ifdef  __cplusplus
extern "C" {
#endif
extern af_instance_t *linkplay_resample_instance_create(const char *filter);
extern int linkplay_set_resample_format(af_data_t *in_format, af_instance_t *af);
extern af_data_t *linkPlay_resample(af_data_t *in_format, af_instance_t *af);
void linkplay_resample_uninit(af_instance_t *af);
#ifdef  __cplusplus
}
#endif
