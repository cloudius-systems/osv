/*
 * Copyright (c) 2005-2008, Kohsuke Ohtani
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <osv/prex.h>

#include <ctype.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>

#include "fatfs.h"


/*
 * Convert file name to 8.3 format
 *  Ex. "foo.bar" => "foo     bar"
 */
void
fat_convert_name(char *org, char *name)
{
	int i;

	memset(name, (int)' ', 11);
	for (i = 0; i <= 11; i++) {
		if (!*org)
			break;
		if (*org == '/')
			break;
		if (*org == '.') {
			i = 7;
			org++;
			continue;
		}
		*(name + i) = *org;
		org++;
	}
}

/*
 * Restore file name to normal format
 *  Ex. "foo     bar" => "foo.bar"
 */
void
fat_restore_name(char *org, char *name)
{
	int i;

	memset(name, 0, 13);
	for (i = 0; i < 8; i++) {
		if (*org != ' ')
			*name++ = *org;
		org++;
	}
	if (*org != ' ')
		*name++ = '.';
	for (i = 0; i < 3; i++) {
		if (*org != ' ')
			*name++ = *org;
		org++;
	}
}

/*
 * Compare 2 file names.
 *
 * Return 0 if it matches.
 */
int
fat_compare_name(char *n1, char *n2)
{
	int i;

	for (i = 0; i < 11; i++, n1++, n2++) {
		if (toupper((int)*n1) != toupper((int)*n2))
			return -1;
	}
	return 0;
}

/*
 * Check specified name is valid as FAT file name.
 * Return true if valid.
 */
int
fat_valid_name(char *name)
{
	static char invalid_char[] = "*?<>|\"+=,;[] \345";
	int len = 0;

	/* . or .. */
	if (*name == '.') {
		name++;
		if (*name == '.')
			name++;
		return (*(name + 1) == '\0') ? 1 : 0;
	}
	/* First char must be alphabet or numeric */
	if (!isalnum((int)*name))
		return 0;
	while (*name != '\0') {
		if (strchr(invalid_char, *name))
			return 0;
		if (*name == '.')
			break;	/* Start of extension */
		if (++len > 8)
			return 0;	/* Too long name */
		name++;
	}
	if (*name == '\0')
		return 1;
	name++;
	if (*name == '\0')	/* Empty extension */
		return 1;
	len = 0;
	while (*name != '\0') {
		if (strchr(invalid_char, *name))
			return 0;
		if (*name == '.')
			return 0;	/* Second extention */
		if (++len > 3)
			return 0;	/* Too long name */
		name++;
	}
	return 1;
}

/*
 * mode -> attribute
 */
void
fat_mode_to_attr(mode_t mode, u_char *attr)
{

	*attr = 0;
	if (!(mode & S_IWRITE))
		*attr |= FA_RDONLY;
	if (!(mode & S_IREAD))
		*attr |= FA_HIDDEN;
	if (S_ISDIR(mode))
		*attr |= FA_SUBDIR;
}

/*
 * attribute -> mode
 */
void
fat_attr_to_mode(u_char attr, mode_t *mode)
{

	if (attr & FA_RDONLY)
		*mode =
		    S_IRUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH |
		    S_IXOTH;
	else
		*mode = S_IRWXU | S_IRWXG | S_IRWXO;

	if (attr & FA_SUBDIR)
		*mode |= S_IFDIR;
	else
		*mode |= S_IFREG;
}
