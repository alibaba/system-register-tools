
ifneq ($(KERNELRELEASE),)
obj-m  := msr.o
msr-objs := msr_insn.o msr_arm.o msr_smp.o
else
KDIR ?= /lib/modules/`uname -r`/build

all:
	EXTRA_CFLAGS="-O0 -g"
	$(MAKE) -C $(KDIR) M=$(PWD) modules
clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
endif

