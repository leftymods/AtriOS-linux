#ifndef __LINKPLAY_NVRAM_H__
#define __LINKPLAY_NVRAM_H__

/* Definitions for Linkplay NVRAM */

#include <sys/ioctl.h>

#define ENV_BLK_SIZE 0x1000

#define MAX_NAME_LEN  128
#define MAX_VALUE_LEN (ENV_BLK_SIZE * 5)

#define NVRAM_DEVNAME "/dev/nvram"

#define FB_FACTORY_BLOCK_INDEX 0
#define FB_CONFIG_BLOCK_INDEX  1

typedef struct {
    int index;
    int ret;
    char name[MAX_NAME_LEN];
    char value[MAX_VALUE_LEN];
}__attribute__ ((aligned(1), packed)) nvram_ioctl_t;

/**
 * IOCTL definitions
 */
#define LINKPLAY_NVRAM_IOCTL_GET       0x01
#define LINKPLAY_NVRAM_IOCTL_GETALL    0x02
#define LINKPLAY_NVRAM_IOCTL_SET       0x03
#define LINKPLAY_NVRAM_IOCTL_COMMIT    0x04
#define LINKPLAY_NVRAM_IOCTL_CLEAR     0x05
#define LINKPLAY_NVRAM_IOCTL_GET_INDEX 0x06

#endif /* !__LINKPLAY_NVRAM_H__ */
