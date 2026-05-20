#
# Makefile for hypervisor_b kernel module.
#
CONFIG_MODULE_SIG=n

obj-m := hypervisor_b.o

KVERSION := $(shell uname -r)
KDIR := /lib/modules/$(KVERSION)/build

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean
	rm -f Module.symvers modules.order

.PHONY: all clean
