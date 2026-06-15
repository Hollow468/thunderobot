# SPDX-License-Identifier: GPL-2.0
#
# Makefile for Thunderobot platform kernel modules
#

KDIR ?= /lib/modules/$(shell uname -r)/build

# Build all modules
obj-m += thunderobot-core.o
obj-m += thunderobot-gpu.o
obj-m += thunderobot-led.o

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

install: all
	$(MAKE) -C $(KDIR) M=$(PWD) modules_install
	depmod -a

uninstall:
	rm -f /lib/modules/$(shell uname -r)/extra/thunderobot-core.ko
	rm -f /lib/modules/$(shell uname -r)/extra/thunderobot-gpu.ko
	rm -f /lib/modules/$(shell uname -r)/extra/thunderobot-led.ko
	depmod -a

.PHONY: all clean install uninstall
