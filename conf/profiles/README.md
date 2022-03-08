The two subdirectories `aarch64` and `x64` contain tiny makefile
include files (`*.mk`) which specify which drivers should be linked
and enabled into kernel.

The `base.mk` is the file included last by the main makefile and is
intended to disable all drivers unless enabled specifically in a
given profile file. For example, each driver configuration variable
like `conf_drivers_virtio_fs` is disabled in a line in the `base.mk` like this:
```make
export conf_drivers_virtio_fs?=0
```
but would be enabled by this line in the `virtio-pci.mk`:
```make
conf_drivers_virtio_fs?=1
```
if the profile `virtio-pci` is selected when building the kernel like so:
```bash
./scripts/build fs=rofs conf_hide_symbols=1 image=native-example drivers_profile=virtio-pci
```
The `base.mk` also enforces some dependencies between given driver and other kernel
components. For example this line:
```make
ifeq ($(conf_drivers_hpet),1)
export conf_drivers_acpi?=1
endif
```
enables ACPI support if the hpet driver is selected. There is also another rule that
enables PCI support if ACPI is selected. And so on.

The individual files under given architecture directory other than `base.mk` enable
list of drivers for each profile that typically corresponds to a hypervisor like `vbox.mk`
for Virtual Box or a type of hypervisor like `microvm.mk`. One exception is the `all.mk`
file which enables all drivers and is a default profile.

Please note that one can build custom kernel with specific list of drivers by passing
corresponding `conf_drivers_*` parameters to the build script like so:
```bash
./scripts/build fs=rofs conf_hide_symbols=1 image=native-example drivers_profile=base \
  conf_drivers_acpi=1 conf_drivers_virtio_fs=1 conf_drivers_virtio_net=1 conf_drivers_pvpanic=1
```
The kernel built using the command line above comes with enough drivers to mount virtio-FS filesystem and support networking over virtio-net device.

Lastly if you want to verify which exact drivers were enabled, you can examine content of the generated `drivers-config.h` header:
```c
cat build/release/gen/include/osv/drivers_config.h
/* This file is generated automatically. */
#ifndef OSV_DRIVERS_CONFIG_H
#define OSV_DRIVERS_CONFIG_H

#define CONF_drivers_acpi 1
#define CONF_drivers_ahci 0
#define CONF_drivers_hpet 0
#define CONF_drivers_hyperv 0
#define CONF_drivers_ide 0
#define CONF_drivers_mmio 0
#define CONF_drivers_pci 1
#define CONF_drivers_pvpanic 1
#define CONF_drivers_pvscsi 0
#define CONF_drivers_scsi 0
#define CONF_drivers_vga 0
#define CONF_drivers_virtio 1
#define CONF_drivers_virtio_blk 0
#define CONF_drivers_virtio_fs 1
#define CONF_drivers_virtio_net 1
#define CONF_drivers_virtio_rng 0
#define CONF_drivers_virtio_scsi 0
#define CONF_drivers_vmxnet3 0
#define CONF_drivers_xen 0

#endif
```
