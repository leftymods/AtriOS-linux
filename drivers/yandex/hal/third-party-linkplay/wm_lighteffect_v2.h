#pragma  once

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
extern "C" {
#endif

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;

static int g_i2c_dev;
static int g_light_type;
static int g_color_num;

#define SND_LED_CHIP_I2C_ADDRESS               0x3c
#define IS_LED_CHIP_I2C_ADDRESS                0x50
#define IS_LED_CHIP3206_ADDRESS                0x6c
#define IS_LED_AW9523_I2C_ADDRESS              0x5b     //aw9523b led i2c0 addr:0x5b

#define LED3236_LEVEL_START_REGISTER               0x01
#define LED3206_LEVEL_START_REGISTER               0x04
#define LED9523B_LEVEL_START_REGISTER              0x20 //P1_0~P1_3 P0_0~P0_7 ==>20H~2BH AW9523B

#define LED_LIGHT_VALUE           0x11  //The brightness of the led,00:Imax 4/4, 11:Imax 1/4
#define LED_MODE_SWITCH0          0x12  //p0
#define LED_MODE_SWITCH1          0x13  //p1
#define LED_MAX_BUF  256
#define IIC_PATH    "/dev/i2c-0"
#define IIC_PATH_AW  "/dev/i2c-0"


typedef enum {
    PWM_LIGHT = 0,
    IIC_IS3208 = 1,
    IIC_SN3236 = 2,
    IIC_IS3737 = 3,
    IIC_AW9523 = 5,

    IIC_IS3206 = 4,
    IIC_IS3206_mute,
    IIC_IS3206_LTE,
    IIC_IS3206_POWER,

    MAX_LIGHTP_TYPE,
} light_t;

int light_init(light_t, int, int);
int light_show(int, int, unsigned char *, int);

#ifdef __cplusplus
}
#endif
