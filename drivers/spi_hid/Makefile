CFLAGS_trace.o = -I$(src)
obj-m += spi-hid.o
spi-hid-objs := spi-hid-core.o trace.o

KDIR ?= /lib/modules/$(shell uname -r)/build

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

install:
	sudo install -Dm644 spi-hid.ko /lib/modules/$(shell uname -r)/updates/spi-hid.ko
	sudo depmod -a

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
