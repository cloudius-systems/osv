/*
 * Copyright (C) 2017 Sergiy Kibrik <sergiy.kibrik@globallogic.com>
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef XEN_HH
#define XEN_HH
#include <xen/interface/xen.h>

extern "C" shared_info_t *HYPERVISOR_shared_info;

#define is_xen() (HYPERVISOR_shared_info != nullptr)

#endif /* XEN_HH */
