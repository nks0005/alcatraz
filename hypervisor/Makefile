#
# Makefile for hypervisor_b kernel module.
# (same layout as Source/hyper_box/Makefile)
#
CONFIG_MODULE_SIG=n

obj-m := hypervisor_b.o

KVERSION := $(shell uname -r)
KDIR := /lib/modules/$(KVERSION)/build
NASM := $(shell command -v nasm 2>/dev/null)

ccflags-y := -fno-toplevel-reorder

ifneq ($(NASM),)
hypervisor_b-objs := hb_probe.o asm_helper.o
%.o: %.asm
	$(NASM) -f elf64 -o $@ $<
	touch "$(CURDIR)/.asm_helper.o.cmd"
else
hypervisor_b-objs := hb_probe.o asm_helper_fallback.o
$(warning nasm not found: install with "sudo apt install nasm", or using asm_helper_fallback.c)
endif

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules

clean:
	rm -f *.o *.mod* *.ko
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean
	rm -f Module.symvers modules.order

.PHONY: all clean
