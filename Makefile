#
# By default, the build is done against the running linux kernel source.
# To build against a different kernel source tree, set KDIR:
#
#    make KDIR=/path/to/kernel/source

KDIR ?= /lib/modules/$(shell uname -r)/build

default: modules
.PHONY: default

obj-m += eid_iomem_pci.o

%::
	$(MAKE) -C $(KDIR) M=$$PWD $@

install: modules_install
.PHONY: install
