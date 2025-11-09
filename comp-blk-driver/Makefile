# ============================================================
# Block Driver Makefile (Works for Buildroot + Manual)
# ============================================================

# Uncomment for debugging
# DEBUG = y

ifeq ($(DEBUG),y)
  DEBFLAGS = -O -g -DDEBUG
else
  DEBFLAGS = -O2
endif

EXTRA_CFLAGS += $(DEBFLAGS)

# ============================================================
# If building from kernel build system
# ============================================================
ifneq ($(KERNELRELEASE),)
obj-m := main.o

# ============================================================
# Else, invoked manually or from Buildroot package.mk
# ============================================================
else

# Use Buildroot’s kernel directory if available
KDIR ?= $(LINUX_DIR)
# Fallback for manual build
KDIR ?= /home/vaishnavi/linux-rpi

ARCH ?= arm64
CROSS_COMPILE ?= aarch64-none-linux-gnu-
PWD := $(shell pwd)

all:
	$(MAKE) -C $(KDIR) M=$(PWD) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) modules

endif

clean:
	rm -rf *.o *~ core .depend .*.cmd *.ko *.mod.c .tmp_versions

