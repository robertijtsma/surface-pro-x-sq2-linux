# SPDX-License-Identifier: GPL-2.0-only
obj-m += sq2_s2idle_pm.o

KDIR ?= /lib/modules/$(shell uname -r)/build

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules

install:
	sudo install -Dm644 sq2_s2idle_pm.ko /lib/modules/$(shell uname -r)/updates/sq2_s2idle_pm.ko
	sudo depmod -a

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean
