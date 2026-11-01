#ifndef SOFT_PREP_REL_DEFINE_H
#define SOFT_PREP_REL_DEFINE_H

#include <stdint.h>
#include <stdlib.h>

#include <pthread.h>

/* Support capability */
#define SOFP_PREP_CAP_AEC           (1<<0)              /* support AEC */
#define SOFP_PREP_CAP_WAKEUP        (1<<1)              /* support Wakeup */

/* Audio format */
typedef struct {
    unsigned short NumChannels;                         /* number of channel */
    unsigned short BitsPerSample;                       /* sample bitwidth */
    unsigned long SampleRate;                           /* sample rate */
} WAVE_PARAM;

/* WWE result */
typedef struct {
    long score;                                         /* score for wake word */
    int detected_numb;                                  /* the number of wake word detected */
    long long sample_start;                             /* sample index of start point for wake word in total samples */
    long long sample_end;                               /* sample index of stop point for wake word in total samples */
    int reserved1;                                      /* reserved */
    int reserved2;                                      /* reserved */
} WWE_RESULT;

/* capture device */
typedef enum {
    SOFT_PREP_CAP_PDM,                                  /* PDM device: hw:0,2. EVK board */
    SOFT_PREP_CAP_TDMC,                                 /* TDMC device: hw:0,1. customize design */
} SOFT_PREP_CAP_DEVICE;

/* capture data callback */
/*
 * @param opaque: handle of callback
 * @param buffer: capture data buffer
 * @param dataLen: data length in byte in the capture data buffer
 * @return number of data used
*/
typedef int (soft_prep_capture_CBACK)(void *opaque, char *buffer, int dataLen);

/* mic raw data callback */
/*
 * @param opaque: handle of callback
 * @param buffer: capture data buffer
 * @param dataLen: data length in byte in the capture data buffer
 * @return number of data used
*/
typedef int (soft_prep_micraw_CBACK)(void *opaque, char *buffer, int dataLen);

/* wake up callback */
/*
 * @param opaque: handle of callback
 * @param wwe_result: the information of wake word trigger
 * @return error code
*/
typedef int (soft_prep_wakeup_CBACK)(void *opaque, WWE_RESULT *wwe_result);

/* get cloud state callback */
/*
 * @param opaque: handle of callback
 * @return state of cloud
 * 0 : AudioInputProcessor::ObserverInterface::State::IDLE
 * 1 : AudioInputProcessor::ObserverInterface::State::EXPECTING_SPEECH
 * 2 : AudioInputProcessor::ObserverInterface::State::RECOGNIZING
 * 3 : AudioInputProcessor::ObserverInterface::State::BUSY
*/
typedef int (soft_prep_ObserverState_CBACK)(void *opaque);

/* module init parameter */
typedef struct _SOFT_PREP_INIT {
    soft_prep_capture_CBACK *captureCallback;           /* register the record callback */
    soft_prep_wakeup_CBACK *wakeupCallback;             /* register the wake up callback */
    soft_prep_ObserverState_CBACK *ObserverStateCallback;   /* register the wake up callback */
    soft_prep_micraw_CBACK *micrawCallback;             /* register the mics raw data callback */

    int mic_numb;                                       /* number of mics */
    SOFT_PREP_CAP_DEVICE cap_dev;                       /* capture device of mics */
    const char *configFile;                             /* config file path */

    void *opaque_capture;                               /* record callback handle */
    void *opaque_wakeup;                                /* wakeup callback handle */
    void *opaque_observer;                              /* observer callback handle */
    void *opaque_micraw;                                /* mics raw callback handle */
} SOFT_PREP_INIT, *PSOFT_PREP_INIT;
#endif //SOFT_PREP_REL_DEFINE_H
