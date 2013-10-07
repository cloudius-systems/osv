/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef INCLUDED_OSV_POWER_H
#define INCLUDED_OSV_POWER_H

/**
 * OSv namespace
 */
namespace osv {

/** @name #include <osv/power.hh>
 * Functions for turning off or rebooting the virtual machine
 */

/**@{*/
/**
 * Abort the system, leaving all vCPUs in hung state.
 *
 * The running system is aborted immediately, without attempting any sort of
 * clean shutdown (e.g., files are not flushed to disk). This makes halt()
 * useful even when OSv's state is known to be broken, e.g., when detecting
 * a bug.
 *
 * After a halt, the virtual machine will be hung (in an endless loop of HLT
 * instructions). If you want to instruct the hypervisor to terminate this
 * guest, use osv::poweroff() instead of halt().
 *
 * \return This function can never return.
 */
void halt() __attribute__((noreturn));

/**
 * Abort the system and terminate the virtual machine.
 *
 * The running system is aborted immediately, without attempting any sort of
 * clean shutdown (e.g., files are not flushed to disk). This makes poweroff()
 * useful even when OSv's state is known to be broken, e.g., when detecting
 * a bug.
 *
 * poweroff() instructs the hypervisor to stop running the virtual machine.
 * If for some reason the hypervisor cannot do this, the VM remains hung
 * as in osv::halt().
 *
 * If you want to leave the virtual machine running after aborting the
 * system, e.g., to attach a debugger, use osv::halt() instead of poweroff().
 *
 * \return This function can never return.
 */
void poweroff() __attribute__((noreturn));

/**
 * Reboot the virtual machine.
 *
 * The system is rebooted immediately, without attempting any sort of
 * clean shutdown (e.g., files are not flushed to disk). This makes reboot()
 * useful even when OSv's state is known to be broken, e.g., when detecting
 * a bug.
 *
 * After the reboot, the system - OSv and its application - normally comes
 * up again.
 *
 * \return Usually, this function does not return. But it may return in the
 * unlikely case that the hypervisor refuses the reboot command.
 */
void reboot();

/**@}*/
}

#endif /* INCLUDED_OSV_POWER_H */
