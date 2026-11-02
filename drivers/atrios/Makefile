# SPDX-License-Identifier: GPL-2.0
#
# Makefile for AtriOS-aml Station Max (leftymods) (Amlogic S905X3 / SM1) Hardware Drivers
# Compatible with Mainline Linux 7.0+ and Amlogic Vendor 4.9.113
#

obj-$(CONFIG_YANDEX_GOWIN_LED_DEVICE) += gowin_led_device.o
obj-$(CONFIG_YANDEX_QUASAR_LED_SCREEN) += quasar_led_screen.o
obj-$(CONFIG_YANDEX_QUASAR_LED_RING) += quasar_led_ring.o
obj-$(CONFIG_YANDEX_QUASAR_PCBA_IDS) += quasar_pcba_ids.o
obj-$(CONFIG_YANDEX_ROTARY_VOLUME) += rotary-volume.o
obj-$(CONFIG_YANDEX_GPIO_KEYPAD) += gpio_keypad.o
obj-$(CONFIG_YANDEX_ZIGBEE_CONTROL) += zigbee-control.o
obj-$(CONFIG_YANDEX_LTR308ALS01) += ltr308als01.o
obj-$(CONFIG_YANDEX_LEDS_IS31FL32XX) += leds-is31fl32xx.o
obj-$(CONFIG_YANDEX_SND_SOC_SY6045S) += snd-soc-sy6045s.o
obj-$(CONFIG_YANDEX_SND_SOC_ES8156) += snd-soc-es8156.o
obj-$(CONFIG_YANDEX_SND_SOC_ES7210) += snd-soc-es7210.o
obj-$(CONFIG_YANDEX_SND_SOC_TAS5825M) += snd-soc-tas5825m.o

ccflags-y += -Wno-error

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
