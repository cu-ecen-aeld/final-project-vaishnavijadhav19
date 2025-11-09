obj-m := main.o

KDIR ?= /home/vaishnavi/linux-rpi
ARCH ?= arm64
CROSS_COMPILE ?= aarch64-none-linux-gnu-
PWD := $(shell pwd)

all:
	$(MAKE) -C $(KDIR) M=$(PWD) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

