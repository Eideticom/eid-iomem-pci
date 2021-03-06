# Eideticom IOMEM PCIe Linux Driver
## Introduction
This is a kernel PCIe driver for the Eideticom IOMEM device that ties into the
[p2pmem][1] framework. It allows userspace to mmap the space for use with DMA
operations.

## Build and Install
To build the module as a .ko for manual installation run:

```
make KDIR=/path/to/kernel/source
```

Note the resultant kernel module is called eid_iomem_pci.ko. To install
the module in the current kernel's module tree run:

```
make install
```

Note that for this to work you either need to have module signing set
up or turned off on your machine. Also this code will only work for kernels
with the pci_mmap_p2pmem function.

Often you don't want to compile against the kernel installed on your
host so you can use the instructions [here][2] that describe how to
prepare a kernel tree for out-of-tree module compilation. The
following process seems to work well (in the top level kernel source
folder):

1. ```make distclean```.
2. Setup your .config (don't forget you will need p2pdma enabled).
3. ```make modules_prepare```.

You can now go back to your eid-iomem-pci repository and run the make
command as noted previously. Note you *might* get the following
warning when you build the module.

```
WARNING: Symbol version dump ./Module.symvers
is missing; modules will have no dependencies and modversions.
```
This behaviour is [expected][2].

## Usage
Once this module has been inserted you should see a /dev/eid-iomemX for
each of the eid_iomem devices in your system. You can then use mmap() to
obtain virtual address pointers backed by memory on the PCIe BAR(s)
associated with /dev/eid-iomemX. You can then pass these pointers into
library functions like write() and read() *as long as you use
O_DIRECT*.

An example of how to use /dev/eid-iomemX is via [p2pmem-test][3] which
also has more information on setting up p2pdma enabled kernels and a
p2pdma capable system.

[1]: https://www.kernel.org/doc/html/latest/driver-api/pci/p2pdma.html
[2]: https://www.kernel.org/doc/Documentation/kbuild/modules.txt
[3]: https://github.com/sbates130272/p2pmem-test
