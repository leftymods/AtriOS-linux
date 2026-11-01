#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#ifndef I2C_FUNC_I2C
#include <linux/i2c.h>
#endif
#include <string.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef unsigned char   uint8_t;
typedef unsigned short  uint16_t;
typedef unsigned int    uint32_t;

static int g_i2c_dev;
static int g_light_type;
static int g_color_num;

#define LED_CHIP_I2C_ADDRESS                0x3C
#define LED_LEVEL_START_REGISTER            0x01
#define MAX_BUF  256
#define IIC_PATH    "/dev/i2c-0"

typedef enum {
    PWM_LIGHT = 0,
    IIC_IS3208 = 1,
    IIC_SN3236 = 2,
    MAX_LIGHTP_TYPE,
}light_t;

int light_init(light_t, int, int);
int light_show(unsigned char *, int);

#ifdef __cplusplus
}
#endif
