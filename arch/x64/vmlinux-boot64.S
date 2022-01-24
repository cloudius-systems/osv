# Copyright (C) 2019 Waldemar Kozaczuk
#
# This work is open source software, licensed under the terms of the
# BSD license as described in the LICENSE file in the top-level directory.

.text
.code64
.global vmlinux_entry64
.hidden vmlinux_entry64
vmlinux_entry64:
    # The address of the boot_params structure is passed in the RSI
    # register so store it in RDI register so that it can be received
    # by the extract_linux_boot_params fuction later
    mov %rsi, %rdi

    # Load the 64-bit version of the GDT
    # Because the memory is mapped 1:1 at this point, we have to manualy
    # subtract OSV_KERNEL_VM_SHIFT from the gdt address
    lgdt gdt64_desc-OSV_KERNEL_VM_SHIFT

    # Setup the stack to switch back to 32-bit mode in order
    # to converge with the code that sets up transiton to 64-bit mode later.
    # Switching back to 32-bit when we are already in 64-bit seems
    # counter intuitive but in fact it allows us to remove code duplication
    # around setting up 64-bit mode of CPU the OSv-way. Besides setting up
    # paging and other control registers this also makes sure that the segment
    # registers are setup correctly as well.
    #
    # The transition from 64-bit back to 32-bit mode is unfortunately not very
    # well documented. For details please read
    # http://blog.dolezel.info/2017/02/running-32-bit-code-in-64-bit-linux.html
    # In short we need to push the 32-bit code segment descriptor GDT offset (0x18)
    # and the address of the instruction (start32_from_64) we want to jump to
    # The lret instruction pops the address and the segment descriptor and jumpt
    # to start32_from_64 which is where the boot process converges.
    subq $8, %rsp
    movl $0x18, 4(%rsp)
    movl $start32_from_64-OSV_KERNEL_VM_SHIFT, %eax # Because memory is mapped 1:1 subtract OSV_KERNEL_VM_SHIFT
    movl %eax, (%rsp)
    lretl
