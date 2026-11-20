obj-m += spx_tcs3430.o

KDIR ?= /lib/modules/$(shell uname -r)/build

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

install:
	sudo install -Dm644 spx_tcs3430.ko /lib/modules/$(shell uname -r)/updates/spx_tcs3430.ko
	sudo depmod -a

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
