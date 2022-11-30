DEBFLAGS = -O2
EXTRA_CFLAGS += $(DEBFLAGS)
KBUILD_EXTRA_SYMBOLS := $(PWD)/Module.symvers

ifneq ($(KERNELRELEASE),)
# call from kernel build system
obj-m	:= hm11.o
else

KERNELDIR ?= /lib/modules/$(shell uname -r)/build
PWD       := $(shell pwd)

modules:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

endif

clean:
	rm -rf *.o *~ core .depend .*.cmd *.ko *.mod.c .tmp_versions

