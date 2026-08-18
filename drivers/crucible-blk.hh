/*
 * Copyright (C) 2024 Waldemar Kozaczuk
 * Copyright (C) 2024 OSv Contributors
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 *
 * Crucible is Oxide Computer's distributed block storage system.
 * This driver is NOT part of the default build profile because it targets
 * Oxide-specific hardware and requires a running Crucible "downstairs" cluster.
 *
 * To build OSv with Crucible support:
 *   ./scripts/build conf_drivers_profile=crucible
 * or:
 *   make conf_drivers_crucible=1
 *
 * The driver uses blocking TCP I/O in kernel context, which is intentional
 * in OSv's single-address-space model but means that a lost connection will
 * stall the I/O thread until the timeout fires.
 */

#ifndef CRUCIBLE_BLK_HH
#define CRUCIBLE_BLK_HH

#include <osv/device.h>
#include <string>
#include <cstdint>
#include <sys/ioctl.h>

// Crucible-specific ioctl commands
#define CRUCIBLE_IOC_CREATE_SNAPSHOT  _IOW('C', 1, uint64_t)

namespace crucible {

/**
 * Initialize Crucible block device driver.
 *
 * Creates Crucible block device with the provided configuration.
 * If connection to downstairs servers fails, a warning is logged but boot continues.
 * This ensures OSv can boot even when Crucible servers are unavailable.
 *
 * @param targets Comma-separated list of downstairs servers (host1:port1,host2:port2,host3:port3)
 * @param uuid UUID string for the Crucible region
 * @param block_size Block size in bytes (default: 512)
 * @param read_only Mount read-only if true
 * @param device_index Device index for multi-volume support (0-7, default: 0)
 * @return 0 on success, error code on failure (boot continues regardless)
 */
int crucible_init(const std::string& targets, const std::string& uuid,
                  uint32_t block_size = 512, bool read_only = false,
                  int device_index = 0);

} // namespace crucible

#endif // CRUCIBLE_BLK_HH
