/*
 * libtsm - Rendering
 *
 * Copyright (c) 2011-2013 David Herrmann <dh.herrmann@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/*
 * Rendering
 * TSM does not depend on any graphics system or rendering libraries. Instead,
 * it provides iterators and ageing support so you can implement renderers
 * yourself.
 */

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "libtsm.hh"
#include "libtsm_int.hh"
#include "shl_llog.hh"

#define LLOG_SUBSYSTEM "tsm_render"

SHL_EXPORT
tsm_age_t tsm_screen_draw(struct tsm_screen *con, tsm_screen_draw_cb draw_cb,
			  tsm_screen_cursor_cb cursor_cb, void *data)
{
	unsigned int cur_x, cur_y;
	unsigned int i, j, k;
	struct line *iter, *line = NULL;
	struct cell *cell, empty;
	struct tsm_screen_attr attr;
	int ret, warned = 0;
	const uint32_t *ch;
	size_t len;
	bool in_sel = false, sel_start = false, sel_end = false;
	bool was_sel = false;
	tsm_age_t age;

	if (!con || !draw_cb)
		return 0;

	screen_cell_init(con, &empty);

	cur_x = con->cursor_x;
	if (con->cursor_x >= con->size_x)
		cur_x = con->size_x - 1;
	cur_y = con->cursor_y;
	if (con->cursor_y >= con->size_y)
		cur_y = con->size_y - 1;

	/* push each character into rendering pipeline */

	iter = con->sb_pos;
	k = 0;

	if (con->sel_active) {
		if (!con->sel_start.line && con->sel_start.y == SELECTION_TOP)
			in_sel = !in_sel;
		if (!con->sel_end.line && con->sel_end.y == SELECTION_TOP)
			in_sel = !in_sel;

		if (con->sel_start.line &&
		    (!iter || con->sel_start.line->sb_id < iter->sb_id))
			in_sel = !in_sel;
		if (con->sel_end.line &&
		    (!iter || con->sel_end.line->sb_id < iter->sb_id))
			in_sel = !in_sel;
	}

	for (i = 0; i < con->size_y; ++i) {
		if (iter) {
			line = iter;
			iter = iter->next;
		} else {
			line = con->lines[k];
			k++;
		}

		if (con->sel_active) {
			if (con->sel_start.line == line ||
			    (!con->sel_start.line &&
			     con->sel_start.y == static_cast<int>(k) - 1))
				sel_start = true;
			else
				sel_start = false;
			if (con->sel_end.line == line ||
			    (!con->sel_end.line &&
			     con->sel_end.y == static_cast<int>(k) - 1))
				sel_end = true;
			else
				sel_end = false;

			was_sel = false;
		}

		for (j = 0; j < con->size_x; ++j) {
			if (j < line->size)
				cell = &line->cells[j];
			else
				cell = &empty;

			memcpy(&attr, &cell->attr, sizeof(attr));

			if (con->sel_active) {
				if (sel_start &&
				    j == con->sel_start.x) {
					was_sel = in_sel;
					in_sel = !in_sel;
				}
				if (sel_end &&
				    j == con->sel_end.x) {
					was_sel = in_sel;
					in_sel = !in_sel;
				}
			}

			if (k == cur_y + 1 && j == cur_x &&
			    !(con->flags & TSM_SCREEN_HIDE_CURSOR))
				attr.inverse = !attr.inverse;

			/* TODO: do some more sophisticated inverse here. When
			 * INVERSE mode is set, we should instead just select
			 * inverse colors instead of switching background and
			 * foreground */
			if (con->flags & TSM_SCREEN_INVERSE)
				attr.inverse = !attr.inverse;

			if (in_sel || was_sel) {
				was_sel = false;
				attr.inverse = !attr.inverse;
			}

			if (con->age_reset) {
				age = 0;
			} else {
				age = cell->age;
				if (line->age > age)
					age = line->age;
				if (con->age > age)
					age = con->age;
			}

                        ch = &cell->ch;
                        len = 1;
			if (cell->ch == ' ' || cell->ch == 0)
				len = 0;
			ret = draw_cb(con, cell->ch, ch, len, cell->width,
				      j, i, &attr, age, data);
			if (ret && warned++ < 3) {
				llog_debug(con,
					   "cannot draw glyph at %ux%u via text-renderer",
					   j, i);
				if (warned == 3)
					llog_debug(con,
						   "suppressing further warnings during this rendering round");
			}
		}
	}

	if (con->cursor_dirty) {
		ret = cursor_cb(con, con->cursor_x, con->cursor_y, data);
		if (ret && warned++ < 3) {
			llog_debug(con,
				   "cannot draw cursor at %ux%u via text-renderer",
				   con->cursor_x, con->cursor_y);
 		}
 		con->cursor_dirty = false;
 	}
	if (con->age_reset) {
		con->age_reset = 0;
		return 0;
	} else {
		return con->age_cnt;
	}
}
