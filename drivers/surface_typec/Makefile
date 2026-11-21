obj-m += surface_typec.o

KDIR ?= /lib/modules/$(shell uname -r)/build

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules

install:
	sudo install -Dm644 surface_typec.ko /lib/modules/$(shell uname -r)/updates/surface_typec.ko
	sudo depmod -a

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean
