# SPDX-License-Identifier: GPL-2.0
#
# Makefile for Yandex Station Max (Amlogic S905X3 / SM1) Hardware Drivers
# Compatible with Mainline Linux 7.0+ and Amlogic Vendor 4.9.113
#

obj-m += gowin_led_device.o
obj-m += quasar_led_screen.o
obj-m += quasar_led_ring.o
obj-m += quasar_pcba_ids.o
obj-m += rotary-volume.o
obj-m += gpio_keypad.o
obj-m += zigbee-control.o
obj-m += ltr308als01.o
obj-m += leds-is31fl32xx.o
obj-m += snd-soc-sy6045s.o
obj-m += snd-soc-es8156.o
obj-m += snd-soc-es7210.o
obj-m += snd-soc-tas5825m.o

ccflags-y += -DCONFIG_FB_BACKLIGHT=1 -Wno-error

KDIR ?= $(shell pwd)/../../linux-7.0
PWD := $(shell pwd)
ARCH ?= arm64
CROSS_COMPILE ?= aarch64-linux-gnu-

default:
	$(MAKE) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) -C $(KDIR) M=$(PWD) modules

modules_install:
	$(MAKE) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) -C $(KDIR) M=$(PWD) modules_install

clean:
	$(MAKE) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) -C $(KDIR) M=$(PWD) clean
