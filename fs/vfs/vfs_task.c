/*-
 * Copyright (c) 2007, Kohsuke Ohtani All rights reserved.
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

/*
 * vfs_task.c - Routines to manage the per task data.
 */


#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#include <osv/prex.h>
#include "vfs.h"

/*
 * Allocate new task.
 */
int
task_alloc(struct task **pt)
{
	struct task *t;

	if (!(t = malloc(sizeof(struct task))))
		return ENOMEM;
	memset(t, 0, sizeof(struct task));
	strlcpy(t->t_cwd, "/", sizeof(t->t_cwd));

	*pt = t;
	return 0;
}

/*
 * Convert to full path from the cwd of task and path.
 * @t:    task structure
 * @path: target path
 * @full: full path to be returned
 * @acc: access mode
 */
int
task_conv(struct task *t, const char *cpath, int acc, char *full)
{
	char path[PATH_MAX];
	char *src, *tgt, *p, *end, *cwd;
	size_t len = 0;

	cwd = t->t_cwd;

	strlcpy(path, cpath, PATH_MAX);
	path[PATH_MAX - 1] = '\0';

	len = strlen(path);
	if (len >= PATH_MAX)
		return ENAMETOOLONG;
	if (strlen(cwd) + len >= PATH_MAX)
		return ENAMETOOLONG;
	src = path;
	tgt = full;
	end = src + len;
	if (path[0] == '/') {
		*tgt++ = *src++;
		len++;
	} else {
		strlcpy(full, cwd, PATH_MAX);
		len = strlen(cwd);
		tgt += len;
		if (len > 1 && path[0] != '.') {
			*tgt = '/';
			tgt++;
			len++;
		}
	}
	while (*src) {
		p = src;
		while (*p != '/' && *p != '\0')
			p++;
		*p = '\0';
		if (!strcmp(src, "..")) {
			if (len >= 2) {
				len -= 2;
				tgt -= 2;	/* skip previous '/' */
				while (*tgt != '/') {
					tgt--;
					len--;
				}
				if (len == 0) {
					tgt++;
					len++;
				}
			}
		} else if (!strcmp(src, ".")) {
			/* Ignore "." */
		} else {
			while (*src != '\0') {
				*tgt++ = *src++;
				len++;
			}
		}
		if (p == end)
			break;
		if (len > 0 && *(tgt - 1) != '/') {
			*tgt++ = '/';
			len++;
		}
		src = p + 1;
	}
	*tgt = '\0';

	/* Check if the client task has required permission */
	return 0; //sec_file_permission(t->t_taskid, full, acc);
}
