/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2015-2021 Amazon.com, Inc. or its affiliates.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in
 * the documentation and/or other materials provided with the
 * distribution.
 * * Neither the name of copyright holder nor the names of its
 * contributors may be used to endorse or promote products derived
 * from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef ENA_FBSD_LOG_H
#define ENA_FBSD_LOG_H

enum ena_log_t {
	ENA_ERR = 0,
	ENA_WARN,
	ENA_INFO,
	ENA_DBG,
};

extern int ena_log_level;

#define ena_log_unused(dev, level, fmt, args...)		\
	do {							\
	} while (0)

#ifdef ENA_LOG_ENABLE
#define ena_log(dev, level, fmt, args...)			\
	do {							\
		if (ENA_ ## level <= ena_log_level)		\
			tprintf("ena", logger_debug, fmt, ##args);\
	} while (0)

#define ena_log_raw(level, fmt, args...)			\
	do {							\
		if (ENA_ ## level <= ena_log_level)		\
			printf(fmt, ##args);			\
	} while (0)
#else
#define ena_log(dev, level, fmt, args...)			\
	ena_log_unused((dev), level, fmt, ##args)

#define ena_log_raw(level, fmt, args...)			\
	ena_log_unused((dev), level, fmt, ##args)
#endif

#ifdef ENA_LOG_IO_ENABLE
#define ena_log_io(dev, level, fmt, args...)			\
	ena_log((dev), level, fmt, ##args)
#else
#define ena_log_io(dev, level, fmt, args...)			\
	ena_log_unused((dev), level, fmt, ##args)
#endif

#define ena_log_nm(dev, level, fmt, args...)			\
	ena_log((dev), level, "[nm] " fmt, ##args)

#endif /* !(ENA_FBSD_LOG_H) */
