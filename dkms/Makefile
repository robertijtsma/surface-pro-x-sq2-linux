KERNELRELEASE ?= $(shell uname -r)
KDIR ?= /lib/modules/$(KERNELRELEASE)/build

SUBDIRS := \
	spi_hid \
	spx_tcs3430 \
	sq2_s2idle_pm \
	sq2_venus_overlay \
	surface_cam_spmi \
	surface_ldoe14_xob \
	surface_typec \
	tpm_qcom_qsee

.PHONY: all clean $(SUBDIRS)

all:
	@for d in $(SUBDIRS); do \
		echo "=== [DKMS] Building $$d (KDIR=$(KDIR)) ==="; \
		$(MAKE) -C $$d KDIR="$(KDIR)" all || exit $$?; \
	done

clean:
	@for d in $(SUBDIRS); do \
		$(MAKE) -C $$d KDIR="$(KDIR)" clean || true; \
	done
