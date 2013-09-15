/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

OUTPUT_FORMAT(binary)

MEMORY
{
	BOOTSECT : ORIGIN = 0, LENGTH = 0x10000
}

SECTIONS
{
	.text 0x7c00 : { *(.text) } > BOOTSECT
}

