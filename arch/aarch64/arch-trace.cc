/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "osv/trace.hh"

/**
 * Empty. No "quick" path on this arch yet.
 */
void tracepoint_base::activate(const tracepoint_id &, void * patch_site, void * slow_path)
{}

void tracepoint_base::deactivate(const tracepoint_id &, void * patch_site, void * slow_path)
{}

