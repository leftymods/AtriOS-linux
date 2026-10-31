obj-m += gowin_led_device.o
obj-m += quasar_led_screen.o
obj-m += quasar_led_ring.o
obj-m += quasar_pcba_ids.o

KDIR ?= /home/lmods/linux-amlogic-4.9
PWD := $(shell pwd)
ARCH ?= arm64
CROSS_COMPILE ?= aarch64-linux-gnu-

default:
	$(MAKE) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) -C $(KDIR) M=$(PWD) clean
