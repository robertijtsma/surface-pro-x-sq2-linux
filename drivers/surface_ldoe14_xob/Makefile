obj-m := surface_ldoe14_xob.o

KDIR ?= /lib/modules/$(shell uname -r)/build

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules

install: all
	$(MAKE) -C $(KDIR) M=$(CURDIR) INSTALL_MOD_DIR=extra modules_install
	depmod -a

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean
