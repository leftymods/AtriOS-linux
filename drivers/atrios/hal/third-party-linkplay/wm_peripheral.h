#pragma once

#include <stddef.h>

#if defined __cplusplus || defined __cplusplus__
extern "C" {
#endif

typedef enum {
    PERI_INPUT_KEY_VOLUP,                       /* volume up */
    PERI_INPUT_KEY_VOLDOWN,                     /* volume down */
    PERI_INPUT_KEY_TALK,                        /* talk */
    PERI_INPUT_KEY_WPS,                         /* wps */
    PERI_INPUT_KEY_MUTE,                        /* mute */
    PERI_INPUT_KEY_UNMUTE,                      /* unmute */
    PERI_INPUT_KEY_BT,                          /* BT MODE */
    PERI_INPUT_KEY_WIFI,                        /* WIFI MODE */
    PERI_INPUT_KEY_AUX,                         /* AUX MODE */
    PERI_INPUT_KEY_PLAY,                        /* play */
    PERI_INPUT_KEY_PUS,                         /* pus */
    PERI_INPUT_KEY_POWEROFF,                    /* power off */
    PERI_INPUT_KEY_RST,                         /* reset */
    PERI_INPUT_STR_CMD, 					    /* String Command (Like: MCU+BAT+001) */
    PERI_INPUT_HEARTBEAT,                       /* heartbeat check */
    PERI_INPUT_UNKNOW,
} PERI_INPUT_CMD;

typedef enum {
    PERI_LED_ALEXA_VOLUME,
    PERI_LED_ALEXA_MUTE,
    PERI_LED_ALEXA_UNMUTE,
    PERI_LED_ALEXA_POWEROFF,
    PERI_LED_ALEXA_FACTORY,
    PERI_LED_ALEXA_REBOOT,
    PERI_LED_UPGRADE_START,
    PERI_LED_UPGRADE_FAIL,
    PERI_LED_UPGRADE_SUCCESS,
    PERI_LED_ALICE_LISTEN,
    PERI_LED_ALICE_THINKING,
    PERI_LED_ALICE_TALKING,
    PERI_LED_ALICE_ERROR,
    PERI_LED_ALICE_IDLE,
    PERI_LED_ALERT_START,
    PERI_LED_ALERT_STOP,
    PERI_LED_NET_CONNECTING,
    PERI_LED_NET_CONNECTFAIL,
    PERI_LED_NET_CONNSUCCESS,
    PERI_LED_BT_PAIRING,
    PERI_LED_BT_PAIRINGFAIL,
    PERI_LED_BT_PAIRED,
    PERI_LED_MUTE,
    PERI_LED_UNMUTE,
    PERI_LED_AUXIN,
    PERI_LED_NETWORK_SETUP,
    PERI_LED_VOLUME1 = 26,
    PERI_LED_VOLUME2,
    PERI_LED_VOLUME3,
    PERI_LED_VOLUME4,
    PERI_LED_VOLUME5,
    PERI_LED_VOLUME6,
    PERI_LED_VOLUME7,
    PERI_LED_VOLUME8,
    PERI_LED_VOLUME9,
    PERI_LED_VOLUME10,
    PERI_LED_CUSTOM_RGB,
    PERI_LED_ALEXA_FACTORY2,
} PERI_LED;

/**
 * @brief opaque - User Data Pointer (opaque from PERINIT_INIT structure)
 * @brief inputCmd - command to execute. It can be button event, or String Command.
 * 					 for String Command PERI_INPUT_STR_CMD should be used
 * @brief cmd - MCU Command string (with '\0'). cmd should be used when inoutCmd == PERI_INPUT_STR_CMD
 * @brief buffSuze - cmd buffer size (basicly it is a string size + '\0')
 */
typedef int (peri_input_CBACK)(void* opaque, PERI_INPUT_CMD inputCmd, const char* cmd, size_t buffSize);

typedef struct _PERI_INIT {
    peri_input_CBACK *inputCallback;
    void *opaque; /* User data pointer that always will be passed as 1t parameter to callback */
} PERI_INIT, *PPERI_INIT;

int Peri_Init(PERI_INIT *init_input);
int Peri_LED_Animation(PERI_LED animation_index);

/**
 * @brief command that should be executed (string should be null-terminated ('\0'))
 * @brief cmd buff size (basicly it is a string size + '\0')
 */
int Peri_MCU_Send(const char *cmd, size_t buffSize);
int Peri_VOL_Sync(int vol);

#if defined __cplusplus || defined __cplusplus__
}
#endif