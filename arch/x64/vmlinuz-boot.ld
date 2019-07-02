/*
 * Copyright (C) 2019 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

OUTPUT_FORMAT(binary)

SECTIONS
{
/* Layout:
   1K of header
   1K of code
   ------------
   - header's pref_address points to second 1K
   - the code in second 1K (first instruction) jumps to start32 version
*/
    .data : { *(.data) }
    .text 0x400 : { *(.text) }
}
