# Makefile for Compression Block Device Driver (x86_64)
obj-m := main.o
KDIR := /lib/modules/$(shell uname -r)/build
PWD  := $(shell pwd)
ARCH := x86_64
CROSS_COMPILE :=

all:
	$(MAKE) -C $(KDIR) M=$(PWD) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) clean

