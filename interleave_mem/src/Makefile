obj-m := interleave_mem.o
KERNEL_DIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

all:
	make -C $(KERNEL_DIR) M=$(PWD) modules

clean:
	rm -f *.o *.ko *.mod.c *.mod *.symvers *.order .*.cmd

.PHONY: clean