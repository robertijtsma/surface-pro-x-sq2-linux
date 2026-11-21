obj-m += sq2_venus_overlay.o

KDIR ?= /lib/modules/$(shell uname -r)/build

all: overlay.h
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules

overlay.dtbo: overlay.dts
	dtc -I dts -O dtb -o $@ $<

overlay.h: overlay.dtbo
	python3 -c 'from pathlib import Path; b=Path("overlay.dtbo").read_bytes(); Path("overlay.h").write_text("static unsigned char overlay[] = {" + ",".join(map(str, b)) + "};\n")'

install: all
	install -Dm644 sq2_venus_overlay.ko /lib/modules/$(shell uname -r)/updates/sq2_venus_overlay.ko
	depmod -a

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean
	rm -f overlay.dtbo overlay.h
