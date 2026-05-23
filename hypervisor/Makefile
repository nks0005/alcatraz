#
# Makefile for hypervisor_b kernel module.
# (same layout as Source/hyper_box/Makefile)
#
CONFIG_MODULE_SIG=n

obj-m := hypervisor_b.o

KVERSION := $(shell uname -r)
KDIR := /lib/modules/$(KVERSION)/build
NASM := nasm

ccflags-y := -fno-toplevel-reorder

hypervisor_b-objs := hb_probe.o asm_helper.o

%.o: %.asm
	$(NASM) -f elf64 -o $@ $<
	touch "$(CURDIR)/.asm_helper.o.cmd"

all:
	@command -v $(NASM) >/dev/null 2>&1 || \
		(echo "error: nasm not found (install: sudo apt install nasm)" >&2; exit 1)
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules

clean:
	rm -f *.o *.mod* *.ko
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean
	rm -f Module.symvers modules.order

.PHONY: all clean
