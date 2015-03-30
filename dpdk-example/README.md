DPDK for OSv
=====================================

Build DPDK for OSv VM image
--------------------------------------------

Build VM image:

````
    [user@host osv]$ ./scripts/build image=dpdk-example
````

Run DPDK for OSv VM image
--------------------------------------------

Run VM image:

````
    [user@host ~]$ ./scripts/run.py -n
    OSv v0.18
    eth0: 192.168.122.15
    EAL: Detected lcore 0 as core 0 on socket 0
    EAL: Detected lcore 1 as core 1 on socket 0
    EAL: Support maximum 128 logical core(s) by configuration.
    EAL: Detected 2 lcore(s)
    EAL:    bar2 not available
    EAL:    bar2 not available
    EAL:    bar2 not available
    EAL:    bar0 not available
    EAL:    bar0 not available
    EAL:    bar0 not available
    EAL:    bar0 not available
    EAL: PCI scan found 7 devices
    EAL: Setting up memory...
    EAL: Mapped memory segment 0 @ 0xffff80003de00000: physaddr:0x3de00000, len 33554432
    EAL: Mapped memory segment 0 @ 0xffff80003bc00000: physaddr:0x3bc00000, len 33554432
    EAL: Mapped memory segment 0 @ 0xffff800039a00000: physaddr:0x39a00000, len 33554432
    EAL: Mapped memory segment 0 @ 0xffff800037800000: physaddr:0x37800000, len 33554432
    EAL: TSC frequency is ~438348 KHz
    EAL: Master lcore 0 is ready (tid=57f7040;cpuset=[0])
    PMD: ENICPMD trace: rte_enic_pmd_init
    EAL: PCI device 0000:00:04.0 on NUMA socket -1
    EAL:   probe driver: 1af4:1000 rte_virtio_pmd
    APP: HPET is not enabled, using TSC as default timer
    RTE>>
````

Run another sample applications
--------------------------------------------

You need to modify cmdline field:

````
    [user@host ~]$ ./scripts/imgedit.py setargs build/release/usr.img "--verbose --maxnic=0 /l2fwd --no-shconf -c f -n 2 --log-level 8 -m 768 -- -p 3"
````
Then you need to run VM on libvirt, following next chapter.

Export VM image to libvirt
--------------------------------------------

Packet forwarding application(such as l2fwd or l3fwd) requires multiple vNICs with multiple bridges, but run.py does not have a way to configure such network.

To do so, you can export VM image to libvirt by using virt-install:

````
    [user@host ~]$ sudo virt-install --import --noreboot --name=osv-dpdk --ram=4096 --vcpus=2 --disk path=/home/user/.capstan/repository/osv-dpdk/osv-dpdk.qemu,bus=virtio --os-variant=none --accelerate --network=network:default,model=virtio --network=network:net2,model=virtio --serial pty --cpu host --rng=/dev/random

    WARNING  Graphics requested but DISPLAY is not set. Not running virt-viewer.
    WARNING  No console to launch for the guest, defaulting to --wait -1

    Starting install...
    Creating domain...                                          |    0 B  00:00
    Domain creation completed. You can restart your domain by running:
      virsh --connect qemu:///system start osv-dpdk

    [user@host ~]$ sudo virsh start osv-dpdk;sudo virsh console osv-dpdkDomain osv-dpdk started

    Connected to domain osv-dpdk
    Escape character is ^]
    OSv v0.18
    EAL: Detected lcore 0 as core 0 on socket 0
    EAL: Detected lcore 1 as core 1 on socket 0
    EAL: Support maximum 128 logical core(s) by configuration.
    EAL: Detected 2 lcore(s)
    EAL:    bar2 not available
    EAL:    bar2 not available
    EAL:    bar2 not available
    EAL:    bar1 not available
    EAL:    bar2 not available
    EAL:    bar1 not available
    EAL:    bar4 not available
    EAL:    bar0 not available
    EAL:    bar1 not available
    EAL:    bar0 not available
    EAL:    bar0 not available
    EAL:    bar0 not available
    EAL:    bar1 not available
    EAL:    bar0 not available
    EAL:    bar0 not available
    EAL:    bar0 not available
    EAL: PCI scan found 16 devices
    EAL: Setting up memory...
    EAL: Mapped memory segment 0 @ 0xffff80013e000000: physaddr:0x13e000000, len 33554432
    EAL: Mapped memory segment 0 @ 0xffff80013be00000: physaddr:0x13be00000, len 33554432
    EAL: Mapped memory segment 0 @ 0xffff800139c00000: physaddr:0x139c00000, len 33554432
    EAL: Mapped memory segment 0 @ 0xffff800137a00000: physaddr:0x137a00000, len 33554432
    EAL: Mapped memory segment 0 @ 0xffff800135800000: physaddr:0x135800000, len 33554432
    EAL: Mapped memory segment 0 @ 0xffff800133600000: physaddr:0x133600000, len 33554432
    EAL: Mapped memory segment 0 @ 0xffff800131400000: physaddr:0x131400000, len 33554432
    EAL: Mapped memory segment 0 @ 0xffff80012f200000: physaddr:0x12f200000, len 33554432
    EAL: Mapped memory segment 0 @ 0xffff80012d000000: physaddr:0x12d000000, len 33554432
    EAL: Mapped memory segment 0 @ 0xffff80012ae00000: physaddr:0x12ae00000, len 33554432
    EAL: Mapped memory segment 0 @ 0xffff800128c00000: physaddr:0x128c00000, len 33554432
    EAL: Mapped memory segment 0 @ 0xffff800126a00000: physaddr:0x126a00000, len 33554432
    EAL: Mapped memory segment 0 @ 0xffff800124800000: physaddr:0x124800000, len 33554432
    EAL: Mapped memory segment 0 @ 0xffff800122600000: physaddr:0x122600000, len 33554432
    EAL: Mapped memory segment 0 @ 0xffff800120400000: physaddr:0x120400000, len 33554432
    EAL: Mapped memory segment 0 @ 0xffff80011e200000: physaddr:0x11e200000, len 33554432
    EAL: Mapped memory segment 0 @ 0xffff80011c000000: physaddr:0x11c000000, len 33554432
    EAL: Mapped memory segment 0 @ 0xffff800119e00000: physaddr:0x119e00000, len 33554432
    EAL: Mapped memory segment 0 @ 0xffff800117c00000: physaddr:0x117c00000, len 33554432
    EAL: Mapped memory segment 0 @ 0xffff800115a00000: physaddr:0x115a00000, len 33554432
    EAL: Mapped memory segment 0 @ 0xffff800113800000: physaddr:0x113800000, len 33554432
    EAL: Mapped memory segment 0 @ 0xffff800111600000: physaddr:0x111600000, len 33554432
    EAL: Mapped memory segment 0 @ 0xffff80010f400000: physaddr:0x10f400000, len 33554432
    EAL: Mapped memory segment 0 @ 0xffff8000bde00000: physaddr:0xbde00000, len 33554432
    EAL: TSC frequency is ~1575941 KHz
    EAL: Master lcore 0 is ready (tid=4b76040;cpuset=[0])
    PMD: ENICPMD trace: rte_enic_pmd_init
    EAL: lcore 1 is ready (tid=52fe040;cpuset=[1])
    EAL: PCI device 0000:00:03.0 on NUMA socket -1
    EAL:   probe driver: 1af4:1000 rte_virtio_pmd
    EAL: PCI device 0000:00:04.0 on NUMA socket -1
    EAL:   probe driver: 1af4:1000 rte_virtio_pmd
    Lcore 0: RX port 0
    Lcore 1: RX port 1
    Initializing port 0... done:
    Port 0, MAC address: 52:54:00:05:59:A9

    Initializing port 1... done:
    Port 1, MAC address: 52:54:00:38:65:DA


    Checking link statusdone
    Port 0 Link Up - speed 10000 Mbps - full-duplex
    Port 1 Link Up - speed 10000 Mbps - full-duplex
    L2FWD: entering main loop on lcore 1
    L2FWD: entering main loop on lcore 0
    L2FWD:  -- lcoreid=1 portid=1
    L2FWD:  -- lcoreid=0 portid=0

    Port statistics ====================================
    Statistics for port 0 ------------------------------
    Packets sent:                        0
    Packets received:                    0
    Packets dropped:                     0
    Statistics for port 1 ------------------------------
    Packets sent:                        0
    Packets received:                    0
    Packets dropped:                     0
    Aggregate statistics ===============================
    Total packets sent:                  0
    Total packets received:              0
    Total packets dropped:               0
    ====================================================
````
