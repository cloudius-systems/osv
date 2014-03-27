/*
 * libtsm - Screen Management
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
 * Screen Management
 * This provides the abstracted screen management. It does not do any
 * terminal-emulation, instead it provides a resizable table of cells. You can
 * insert, remove and modify the cells freely.
 * A screen has always a fixed, but changeable, width and height. This defines
 * the number of columns and rows. The screen doesn't care for pixels, glyphs or
 * framebuffers. The screen only contains information about each cell.
 *
 * Screens are the logical model behind a real screen of a terminal emulator.
 * Users usually allocate a screen for each terminal-emulator they run. All they
 * have to do is render the screen onto their widget on each change and forward
 * any widget-events to the screen.
 *
 * The screen object already includes scrollback-buffers, selection support and
 * more. This simplifies terminal emulators a lot, but also prevents them from
 * accessing the real screen data. However, terminal emulators should have no
 * reason to access the data directly. The screen API should provide everything
 * they need.
 *
 * AGEING:
 * Each cell, line and screen has an "age" field. This field describes when it
 * was changed the last time. After drawing a screen, the current screen age is
 * returned. This allows users to skip drawing specific cells, if their
 * framebuffer was already drawn with a newer age than a given cell.
 * However, the screen-age might overflow. This is properly detected and causes
 * drawing functions to return "0" as age. Users must reset all their
 * framebuffer ages then. Otherwise, further drawing operations might
 * incorrectly skip cells.
 * Furthermore, if a cell has age "0", it means it _has_ to be drawn. No ageing
 * information is available.
 */

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "libtsm.hh"
#include "libtsm_int.hh"
#include "shl_llog.hh"

#define LLOG_SUBSYSTEM "tsm_screen"

static struct cell *get_cursor_cell(struct tsm_screen *con)
{
	unsigned int cur_x, cur_y;

	cur_x = con->cursor_x;
	if (cur_x >= con->size_x)
		cur_x = con->size_x - 1;

	cur_y = con->cursor_y;
	if (cur_y >= con->size_y)
		cur_y = con->size_y - 1;

	return &con->lines[cur_y]->cells[cur_x];
}

static void move_cursor(struct tsm_screen *con, unsigned int x, unsigned int y)
{
	struct cell *c;

	/* if cursor is hidden, just move it */
	if (con->flags & TSM_SCREEN_HIDE_CURSOR) {
		con->cursor_x = x;
		con->cursor_y = y;
		return;
	}

	/* If cursor is visible, we have to mark the current and the new cell
	 * as changed by resetting their age. We skip it if the cursor-position
	 * didn't actually change. */

	if (con->cursor_x == x && con->cursor_y == y)
		return;

	c = get_cursor_cell(con);
	c->age = con->age_cnt;

	con->cursor_x = x;
	con->cursor_y = y;

	c = get_cursor_cell(con);
	c->age = con->age_cnt;

        con->cursor_dirty = true;
}

void screen_cell_init(struct tsm_screen *con, struct cell *cell)
{
	cell->ch = 0;
	cell->width = 1;
	cell->age = con->age_cnt;
	memcpy(&cell->attr, &con->def_attr, sizeof(cell->attr));
}

static int line_new(struct tsm_screen *con, struct line **out,
		    unsigned int width)
{
	struct line *line;
	unsigned int i;

	if (!width)
		return -EINVAL;

	line = reinterpret_cast<struct line *>(malloc(sizeof(*line)));
	if (!line)
		return -ENOMEM;
	line->next = NULL;
	line->prev = NULL;
	line->size = width;
	line->age = con->age_cnt;

	line->cells = reinterpret_cast<struct cell *>(malloc(sizeof(struct cell) * width));
	if (!line->cells) {
		free(line);
		return -ENOMEM;
	}

	for (i = 0; i < width; ++i)
		screen_cell_init(con, &line->cells[i]);

	*out = line;
	return 0;
}

static void line_free(struct line *line)
{
	free(line->cells);
	free(line);
}

static int line_resize(struct tsm_screen *con, struct line *line,
		       unsigned int width)
{
	struct cell *tmp;

	if (!line || !width)
		return -EINVAL;

	if (line->size < width) {
		tmp = reinterpret_cast<struct cell *>(realloc(line->cells, width * sizeof(struct cell)));
		if (!tmp)
			return -ENOMEM;

		line->cells = tmp;

		while (line->size < width) {
			screen_cell_init(con, &line->cells[line->size]);
			++line->size;
		}
	}

	return 0;
}

/* This links the given line into the scrollback-buffer */
static void link_to_scrollback(struct tsm_screen *con, struct line *line)
{
	struct line *tmp;

	/* TODO: more sophisticated ageing */
	con->age = con->age_cnt;

	if (con->sb_max == 0) {
		if (con->sel_active) {
			if (con->sel_start.line == line) {
				con->sel_start.line = NULL;
				con->sel_start.y = SELECTION_TOP;
			}
			if (con->sel_end.line == line) {
				con->sel_end.line = NULL;
				con->sel_end.y = SELECTION_TOP;
			}
		}
		line_free(line);
		return;
	}

	/* Remove a line from the scrollback buffer if it reaches its maximum.
	 * We must take care to correctly keep the current position as the new
	 * line is linked in after we remove the top-most line here.
	 * sb_max == 0 is tested earlier so we can assume sb_max > 0 here. In
	 * other words, buf->sb_first is a valid line if sb_count >= sb_max. */
	if (con->sb_count >= con->sb_max) {
		tmp = con->sb_first;
		con->sb_first = tmp->next;
		if (tmp->next)
			tmp->next->prev = NULL;
		else
			con->sb_last = NULL;
		--con->sb_count;

		/* (position == tmp && !next) means we have sb_max=1 so set
		 * position to the new line. Otherwise, set to new first line.
		 * If position!=tmp and we have a fixed-position then nothing
		 * needs to be done because we can stay at the same line. If we
		 * have no fixed-position, we need to set the position to the
		 * next inserted line, which can be "line", too. */
		if (con->sb_pos) {
			if (con->sb_pos == tmp ||
			    !(con->flags & TSM_SCREEN_FIXED_POS)) {
				if (con->sb_pos->next)
					con->sb_pos = con->sb_pos->next;
				else
					con->sb_pos = line;
			}
		}

		if (con->sel_active) {
			if (con->sel_start.line == tmp) {
				con->sel_start.line = NULL;
				con->sel_start.y = SELECTION_TOP;
			}
			if (con->sel_end.line == tmp) {
				con->sel_end.line = NULL;
				con->sel_end.y = SELECTION_TOP;
			}
		}
		line_free(tmp);
	}

	line->sb_id = ++con->sb_last_id;
	line->next = NULL;
	line->prev = con->sb_last;
	if (con->sb_last)
		con->sb_last->next = line;
	else
		con->sb_first = line;
	con->sb_last = line;
	++con->sb_count;
}

static void screen_scroll_up(struct tsm_screen *con, unsigned int num)
{
	unsigned int i, j, max, pos;
	int ret;

	if (!num)
		return;

	/* TODO: more sophisticated ageing */
	con->age = con->age_cnt;

	max = con->margin_bottom + 1 - con->margin_top;
	if (num > max)
		num = max;

	/* We cache lines on the stack to speed up the scrolling. However, if
	 * num is too big we might get overflows here so use recursion if num
	 * exceeds a hard-coded limit.
	 * 128 seems to be a sane limit that should never be reached but should
	 * also be small enough so we do not get stack overflows. */
	if (num > 128) {
		screen_scroll_up(con, 128);
		return screen_scroll_up(con, num - 128);
	}
	struct line *cache[num];

	for (i = 0; i < num; ++i) {
		pos = con->margin_top + i;
		if (!(con->flags & TSM_SCREEN_ALTERNATE))
			ret = line_new(con, &cache[i], con->size_x);
		else
			ret = -EAGAIN;

		if (!ret) {
			link_to_scrollback(con, con->lines[pos]);
		} else {
			cache[i] = con->lines[pos];
			for (j = 0; j < con->size_x; ++j)
				screen_cell_init(con, &cache[i]->cells[j]);
		}
	}

	if (num < max) {
		memmove(&con->lines[con->margin_top],
			&con->lines[con->margin_top + num],
			(max - num) * sizeof(struct line*));
	}

	memcpy(&con->lines[con->margin_top + (max - num)],
	       cache, num * sizeof(struct line*));

	if (con->sel_active) {
		if (!con->sel_start.line && con->sel_start.y >= 0) {
			con->sel_start.y -= num;
			if (con->sel_start.y < 0) {
				con->sel_start.line = con->sb_last;
				while (con->sel_start.line && ++con->sel_start.y < 0)
					con->sel_start.line = con->sel_start.line->prev;
				con->sel_start.y = SELECTION_TOP;
			}
		}
		if (!con->sel_end.line && con->sel_end.y >= 0) {
			con->sel_end.y -= num;
			if (con->sel_end.y < 0) {
				con->sel_end.line = con->sb_last;
				while (con->sel_end.line && ++con->sel_end.y < 0)
					con->sel_end.line = con->sel_end.line->prev;
				con->sel_end.y = SELECTION_TOP;
			}
		}
	}
}

static void screen_scroll_down(struct tsm_screen *con, unsigned int num)
{
	unsigned int i, j, max;

	if (!num)
		return;

	/* TODO: more sophisticated ageing */
	con->age = con->age_cnt;

	max = con->margin_bottom + 1 - con->margin_top;
	if (num > max)
		num = max;

	/* see screen_scroll_up() for an explanation */
	if (num > 128) {
		screen_scroll_down(con, 128);
		return screen_scroll_down(con, num - 128);
	}
	struct line *cache[num];

	for (i = 0; i < num; ++i) {
		cache[i] = con->lines[con->margin_bottom - i];
		for (j = 0; j < con->size_x; ++j)
			screen_cell_init(con, &cache[i]->cells[j]);
	}

	if (num < max) {
		memmove(&con->lines[con->margin_top + num],
			&con->lines[con->margin_top],
			(max - num) * sizeof(struct line*));
	}

	memcpy(&con->lines[con->margin_top],
	       cache, num * sizeof(struct line*));

	if (con->sel_active) {
		if (!con->sel_start.line && con->sel_start.y >= 0)
			con->sel_start.y += num;
		if (!con->sel_end.line && con->sel_end.y >= 0)
			con->sel_end.y += num;
	}
}

static void screen_write(struct tsm_screen *con, unsigned int x,
			  unsigned int y, tsm_symbol_t ch, unsigned int len,
			  const struct tsm_screen_attr *attr)
{
	struct line *line;
	unsigned int i;

	if (!len)
		return;

	if (x >= con->size_x || y >= con->size_y) {
		llog_warning(con, "writing beyond buffer boundary");
		return;
	}

	line = con->lines[y];

	if ((con->flags & TSM_SCREEN_INSERT_MODE) &&
	    x < (con->size_x - len)) {
		line->age = con->age_cnt;
		memmove(&line->cells[x + len], &line->cells[x],
			sizeof(struct cell) * (con->size_x - len - x));
	}

	line->cells[x].age = con->age_cnt;
	line->cells[x].ch = ch;
	line->cells[x].width = len;
	memcpy(&line->cells[x].attr, attr, sizeof(*attr));

	for (i = 1; i < len && i + x < con->size_x; ++i) {
		line->cells[x + i].age = con->age_cnt;
		line->cells[x + i].width = 0;
	}
}

static void screen_erase_region(struct tsm_screen *con,
				 unsigned int x_from,
				 unsigned int y_from,
				 unsigned int x_to,
				 unsigned int y_to,
				 bool protect)
{
	unsigned int to;
	struct line *line;

	/* TODO: more sophisticated ageing */
	con->age = con->age_cnt;

	if (y_to >= con->size_y)
		y_to = con->size_y - 1;
	if (x_to >= con->size_x)
		x_to = con->size_x - 1;

	for ( ; y_from <= y_to; ++y_from) {
		line = con->lines[y_from];
		if (!line) {
			x_from = 0;
			continue;
		}

		if (y_from == y_to)
			to = x_to;
		else
			to = con->size_x - 1;
		for ( ; x_from <= to; ++x_from) {
			if (protect && line->cells[x_from].attr.protect)
				continue;

			screen_cell_init(con, &line->cells[x_from]);
		}
		x_from = 0;
	}
}

static inline unsigned int to_abs_x(struct tsm_screen *con, unsigned int x)
{
	return x;
}

static inline unsigned int to_abs_y(struct tsm_screen *con, unsigned int y)
{
	if (!(con->flags & TSM_SCREEN_REL_ORIGIN))
		return y;

	return con->margin_top + y;
}

SHL_EXPORT
int tsm_screen_new(struct tsm_screen **out, tsm_log_t log, void *log_data)
{
	struct tsm_screen *con;
	int ret;
	unsigned int i;

	if (!out)
		return -EINVAL;

	con = reinterpret_cast<struct tsm_screen *>(malloc(sizeof(*con)));
	if (!con)
		return -ENOMEM;

	memset(con, 0, sizeof(*con));
	con->ref = 1;
	con->llog = log;
	con->llog_data = log_data;
	con->age_cnt = 1;
	con->age = con->age_cnt;
        con->cursor_dirty = false;

	ret = tsm_screen_resize(con, 80, 24);
	if (ret)
		goto err_free;

	llog_debug(con, "new screen");
	*out = con;

	return 0;

err_free:
	for (i = 0; i < con->line_num; ++i) {
		line_free(con->main_lines[i]);
		line_free(con->alt_lines[i]);
	}
	free(con->main_lines);
	free(con->alt_lines);
	free(con->tab_ruler);
	free(con);
	return ret;
}

SHL_EXPORT
void tsm_screen_ref(struct tsm_screen *con)
{
	if (!con)
		return;

	++con->ref;
}

SHL_EXPORT
void tsm_screen_unref(struct tsm_screen *con)
{
	unsigned int i;

	if (!con || !con->ref || --con->ref)
		return;

	llog_debug(con, "destroying screen");

	for (i = 0; i < con->line_num; ++i) {
		line_free(con->main_lines[i]);
		line_free(con->alt_lines[i]);
	}
	free(con->main_lines);
	free(con->alt_lines);
	free(con->tab_ruler);
	free(con);
}

void tsm_screen_set_opts(struct tsm_screen *scr, unsigned int opts)
{
	if (!scr || !opts)
		return;

	scr->opts |= opts;
}

void tsm_screen_reset_opts(struct tsm_screen *scr, unsigned int opts)
{
	if (!scr || !opts)
		return;

	scr->opts &= ~opts;
}

unsigned int tsm_screen_get_opts(struct tsm_screen *scr)
{
	if (!scr)
		return 0;

	return scr->opts;
}

SHL_EXPORT
unsigned int tsm_screen_get_width(struct tsm_screen *con)
{
	if (!con)
		return 0;

	return con->size_x;
}

SHL_EXPORT
unsigned int tsm_screen_get_height(struct tsm_screen *con)
{
	if (!con)
		return 0;

	return con->size_y;
}

SHL_EXPORT
int tsm_screen_resize(struct tsm_screen *con, unsigned int x,
		      unsigned int y)
{
	struct line **cache;
	unsigned int i, j, width, diff, start;
	int ret;
	bool *tab_ruler;

	if (!con || !x || !y)
		return -EINVAL;

	if (con->size_x == x && con->size_y == y)
		return 0;

	/* First make sure the line buffer is big enough for our new screen.
	 * That is, allocate all new lines and make sure each line has enough
	 * cells to hold the new screen or the current screen. If we fail, we
	 * can safely return -ENOMEM and the buffer is still valid. We must
	 * allocate the new lines to at least the same size as the current
	 * lines. Otherwise, if this function fails in later turns, we will have
	 * invalid lines in the buffer. */
	if (y > con->line_num) {
		/* resize main buffer */
		cache = reinterpret_cast<struct line **>(realloc(con->main_lines, sizeof(struct line*) * y));
		if (!cache)
			return -ENOMEM;

		if (con->lines == con->main_lines)
			con->lines = cache;
		con->main_lines = cache;

		/* resize alt buffer */
		cache = reinterpret_cast<struct line **>(realloc(con->alt_lines, sizeof(struct line*) * y));
		if (!cache)
			return -ENOMEM;

		if (con->lines == con->alt_lines)
			con->lines = cache;
		con->alt_lines = cache;

		/* allocate new lines */
		if (x > con->size_x)
			width = x;
		else
			width = con->size_x;

		while (con->line_num < y) {
			ret = line_new(con, &con->main_lines[con->line_num],
				       width);
			if (ret)
				return ret;

			ret = line_new(con, &con->alt_lines[con->line_num],
				       width);
			if (ret) {
				line_free(con->main_lines[con->line_num]);
				return ret;
			}

			++con->line_num;
		}
	}

	/* Resize all lines in the buffer if we increase screen width. This
	 * will guarantee that all lines are big enough so we can resize the
	 * buffer without reallocating them later. */
	if (x > con->size_x) {
		tab_ruler = reinterpret_cast<bool *>(realloc(con->tab_ruler, sizeof(bool) * x));
		if (!tab_ruler)
			return -ENOMEM;
		con->tab_ruler = tab_ruler;

		for (i = 0; i < con->line_num; ++i) {
			ret = line_resize(con, con->main_lines[i], x);
			if (ret)
				return ret;

			ret = line_resize(con, con->alt_lines[i], x);
			if (ret)
				return ret;
		}
	}

	screen_inc_age(con);

	/* clear expansion/padding area */
	start = x;
	if (x > con->size_x)
		start = con->size_x;
	for (j = 0; j < con->line_num; ++j) {
		/* main-lines may go into SB, so clear all cells */
		i = 0;
		if (j < con->size_y)
			i = start;

		for ( ; i < con->main_lines[j]->size; ++i)
			screen_cell_init(con, &con->main_lines[j]->cells[i]);

		/* alt-lines never go into SB, only clear visible cells */
		i = 0;
		if (j < con->size_y)
			i = con->size_x;

		for ( ; i < x; ++i)
			screen_cell_init(con, &con->alt_lines[j]->cells[i]);
	}

	/* xterm destroys margins on resize, so do we */
	con->margin_top = 0;
	con->margin_bottom = con->size_y - 1;

	/* reset tabs */
	for (i = 0; i < x; ++i) {
		if (i % 8 == 0)
			con->tab_ruler[i] = true;
		else
			con->tab_ruler[i] = false;
	}

	/* We need to adjust x-size first as screen_scroll_up() and friends may
	 * have to reallocate lines. The y-size is adjusted after them to avoid
	 * missing lines when shrinking y-size.
	 * We need to carefully look for the functions that we call here as they
	 * have stronger invariants as when called normally. */

	con->size_x = x;
	if (con->cursor_x >= con->size_x)
		move_cursor(con, con->size_x - 1, con->cursor_y);

	/* scroll buffer if screen height shrinks */
	if (y < con->size_y) {
		diff = con->size_y - y;
		screen_scroll_up(con, diff);
		if (con->cursor_y > diff)
			move_cursor(con, con->cursor_x, con->cursor_y - diff);
		else
			move_cursor(con, con->cursor_x, 0);
	}

	con->size_y = y;
	con->margin_bottom = con->size_y - 1;
	if (con->cursor_y >= con->size_y)
		move_cursor(con, con->cursor_x, con->size_y - 1);

	return 0;
}

SHL_EXPORT
int tsm_screen_set_margins(struct tsm_screen *con,
			       unsigned int top, unsigned int bottom)
{
	unsigned int upper, lower;

	if (!con)
		return -EINVAL;

	if (!top)
		top = 1;

	if (bottom <= top) {
		upper = 0;
		lower = con->size_y - 1;
	} else if (bottom > con->size_y) {
		upper = 0;
		lower = con->size_y - 1;
	} else {
		upper = top - 1;
		lower = bottom - 1;
	}

	con->margin_top = upper;
	con->margin_bottom = lower;
	return 0;
}

/* set maximum scrollback buffer size */
SHL_EXPORT
void tsm_screen_set_max_sb(struct tsm_screen *con,
			       unsigned int max)
{
	struct line *line;

	if (!con)
		return;

	screen_inc_age(con);
	/* TODO: more sophisticated ageing */
	con->age = con->age_cnt;

	while (con->sb_count > max) {
		line = con->sb_first;
		con->sb_first = line->next;
		if (line->next)
			line->next->prev = NULL;
		else
			con->sb_last = NULL;
		con->sb_count--;

		/* We treat fixed/unfixed position the same here because we
		 * remove lines from the TOP of the scrollback buffer. */
		if (con->sb_pos == line)
			con->sb_pos = con->sb_first;

		if (con->sel_active) {
			if (con->sel_start.line == line) {
				con->sel_start.line = NULL;
				con->sel_start.y = SELECTION_TOP;
			}
			if (con->sel_end.line == line) {
				con->sel_end.line = NULL;
				con->sel_end.y = SELECTION_TOP;
			}
		}
		line_free(line);
	}

	con->sb_max = max;
}

/* clear scrollback buffer */
SHL_EXPORT
void tsm_screen_clear_sb(struct tsm_screen *con)
{
	struct line *iter, *tmp;

	if (!con)
		return;

	screen_inc_age(con);
	/* TODO: more sophisticated ageing */
	con->age = con->age_cnt;

	for (iter = con->sb_first; iter; ) {
		tmp = iter;
		iter = iter->next;
		line_free(tmp);
	}

	con->sb_first = NULL;
	con->sb_last = NULL;
	con->sb_count = 0;
	con->sb_pos = NULL;

	if (con->sel_active) {
		if (con->sel_start.line) {
			con->sel_start.line = NULL;
			con->sel_start.y = SELECTION_TOP;
		}
		if (con->sel_end.line) {
			con->sel_end.line = NULL;
			con->sel_end.y = SELECTION_TOP;
		}
	}
}

SHL_EXPORT
void tsm_screen_sb_up(struct tsm_screen *con, unsigned int num)
{
	if (!con || !num)
		return;

	screen_inc_age(con);
	/* TODO: more sophisticated ageing */
	con->age = con->age_cnt;

	while (num--) {
		if (con->sb_pos) {
			if (!con->sb_pos->prev)
				return;

			con->sb_pos = con->sb_pos->prev;
		} else if (!con->sb_last) {
			return;
		} else {
			con->sb_pos = con->sb_last;
		}
	}
}

SHL_EXPORT
void tsm_screen_sb_down(struct tsm_screen *con, unsigned int num)
{
	if (!con || !num)
		return;

	screen_inc_age(con);
	/* TODO: more sophisticated ageing */
	con->age = con->age_cnt;

	while (num--) {
		if (con->sb_pos)
			con->sb_pos = con->sb_pos->next;
		else
			return;
	}
}

SHL_EXPORT
void tsm_screen_sb_page_up(struct tsm_screen *con, unsigned int num)
{
	if (!con || !num)
		return;

	screen_inc_age(con);
	tsm_screen_sb_up(con, num * con->size_y);
}

SHL_EXPORT
void tsm_screen_sb_page_down(struct tsm_screen *con, unsigned int num)
{
	if (!con || !num)
		return;

	screen_inc_age(con);
	tsm_screen_sb_down(con, num * con->size_y);
}

SHL_EXPORT
void tsm_screen_sb_reset(struct tsm_screen *con)
{
	if (!con)
		return;

	screen_inc_age(con);
	/* TODO: more sophisticated ageing */
	con->age = con->age_cnt;

	con->sb_pos = NULL;
}

SHL_EXPORT
void tsm_screen_set_def_attr(struct tsm_screen *con,
				 const struct tsm_screen_attr *attr)
{
	if (!con || !attr)
		return;

	memcpy(&con->def_attr, attr, sizeof(*attr));
}

SHL_EXPORT
void tsm_screen_reset(struct tsm_screen *con)
{
	unsigned int i;

	if (!con)
		return;

	screen_inc_age(con);
	con->age = con->age_cnt;

	con->flags = 0;
	con->margin_top = 0;
	con->margin_bottom = con->size_y - 1;
	con->lines = con->main_lines;

	for (i = 0; i < con->size_x; ++i) {
		if (i % 8 == 0)
			con->tab_ruler[i] = true;
		else
			con->tab_ruler[i] = false;
	}
}

SHL_EXPORT
void tsm_screen_set_flags(struct tsm_screen *con, unsigned int flags)
{
	unsigned int old;
	struct cell *c;

	if (!con || !flags)
		return;

	screen_inc_age(con);

	old = con->flags;
	con->flags |= flags;

	if (!(old & TSM_SCREEN_ALTERNATE) && (flags & TSM_SCREEN_ALTERNATE)) {
		con->age = con->age_cnt;
		con->lines = con->alt_lines;
	}

	if (!(old & TSM_SCREEN_HIDE_CURSOR) &&
	    (flags & TSM_SCREEN_HIDE_CURSOR)) {
		c = get_cursor_cell(con);
		c->age = con->age_cnt;
	}

	if (!(old & TSM_SCREEN_INVERSE) && (flags & TSM_SCREEN_INVERSE))
		con->age = con->age_cnt;
}

SHL_EXPORT
void tsm_screen_reset_flags(struct tsm_screen *con, unsigned int flags)
{
	unsigned int old;
	struct cell *c;

	if (!con || !flags)
		return;

	screen_inc_age(con);

	old = con->flags;
	con->flags &= ~flags;

	if ((old & TSM_SCREEN_ALTERNATE) && (flags & TSM_SCREEN_ALTERNATE)) {
		con->age = con->age_cnt;
		con->lines = con->main_lines;
	}

	if ((old & TSM_SCREEN_HIDE_CURSOR) &&
	    (flags & TSM_SCREEN_HIDE_CURSOR)) {
		c = get_cursor_cell(con);
		c->age = con->age_cnt;
	}

	if ((old & TSM_SCREEN_INVERSE) && (flags & TSM_SCREEN_INVERSE))
		con->age = con->age_cnt;
}

SHL_EXPORT
unsigned int tsm_screen_get_flags(struct tsm_screen *con)
{
	if (!con)
		return 0;

	return con->flags;
}

SHL_EXPORT
unsigned int tsm_screen_get_cursor_x(struct tsm_screen *con)
{
	if (!con)
		return 0;

	return con->cursor_x;
}

SHL_EXPORT
unsigned int tsm_screen_get_cursor_y(struct tsm_screen *con)
{
	if (!con)
		return 0;

	return con->cursor_y;
}

SHL_EXPORT
void tsm_screen_set_tabstop(struct tsm_screen *con)
{
	if (!con || con->cursor_x >= con->size_x)
		return;

	con->tab_ruler[con->cursor_x] = true;
}

SHL_EXPORT
void tsm_screen_reset_tabstop(struct tsm_screen *con)
{
	if (!con || con->cursor_x >= con->size_x)
		return;

	con->tab_ruler[con->cursor_x] = false;
}

SHL_EXPORT
void tsm_screen_reset_all_tabstops(struct tsm_screen *con)
{
	unsigned int i;

	if (!con)
		return;

	for (i = 0; i < con->size_x; ++i)
		con->tab_ruler[i] = false;
}

SHL_EXPORT
void tsm_screen_write(struct tsm_screen *con, tsm_symbol_t ch,
			  const struct tsm_screen_attr *attr)
{
	unsigned int last, len = 1;

	if (!con)
		return;

	screen_inc_age(con);

	if (con->cursor_y <= con->margin_bottom ||
	    con->cursor_y >= con->size_y)
		last = con->margin_bottom;
	else
		last = con->size_y - 1;

	if (con->cursor_x >= con->size_x) {
		if (con->flags & TSM_SCREEN_AUTO_WRAP)
			move_cursor(con, 0, con->cursor_y + 1);
		else
			move_cursor(con, con->size_x - 1, con->cursor_y);
	}

	if (con->cursor_y > last) {
		move_cursor(con, con->cursor_x, last);
		screen_scroll_up(con, 1);
	}

	screen_write(con, con->cursor_x, con->cursor_y, ch, len, attr);
	move_cursor(con, con->cursor_x + len, con->cursor_y);
}

SHL_EXPORT
void tsm_screen_newline(struct tsm_screen *con)
{
	if (!con)
		return;

	screen_inc_age(con);

	tsm_screen_move_down(con, 1, true);
	tsm_screen_move_line_home(con);
}

SHL_EXPORT
void tsm_screen_scroll_up(struct tsm_screen *con, unsigned int num)
{
	if (!con || !num)
		return;

	screen_inc_age(con);

	screen_scroll_up(con, num);
}

SHL_EXPORT
void tsm_screen_scroll_down(struct tsm_screen *con, unsigned int num)
{
	if (!con || !num)
		return;

	screen_inc_age(con);

	screen_scroll_down(con, num);
}

SHL_EXPORT
void tsm_screen_move_to(struct tsm_screen *con, unsigned int x,
			    unsigned int y)
{
	unsigned int last;

	if (!con)
		return;

	screen_inc_age(con);

	if (con->flags & TSM_SCREEN_REL_ORIGIN)
		last = con->margin_bottom;
	else
		last = con->size_y - 1;

	x = to_abs_x(con, x);
	if (x >= con->size_x)
		x = con->size_x - 1;

	y = to_abs_y(con, y);
	if (y > last)
		y = last;

	move_cursor(con, x, y);
}

SHL_EXPORT
void tsm_screen_move_up(struct tsm_screen *con, unsigned int num,
			    bool scroll)
{
	unsigned int diff, size;

	if (!con || !num)
		return;

	screen_inc_age(con);

	if (con->cursor_y >= con->margin_top)
		size = con->margin_top;
	else
		size = 0;

	diff = con->cursor_y - size;
	if (num > diff) {
		num -= diff;
		if (scroll)
			screen_scroll_down(con, num);
		move_cursor(con, con->cursor_x, size);
	} else {
		move_cursor(con, con->cursor_x, con->cursor_y - num);
	}
}

SHL_EXPORT
void tsm_screen_move_down(struct tsm_screen *con, unsigned int num,
			      bool scroll)
{
	unsigned int diff, size;

	if (!con || !num)
		return;

	screen_inc_age(con);

	if (con->cursor_y <= con->margin_bottom)
		size = con->margin_bottom + 1;
	else
		size = con->size_y;

	diff = size - con->cursor_y - 1;
	if (num > diff) {
		num -= diff;
		if (scroll)
			screen_scroll_up(con, num);
		move_cursor(con, con->cursor_x, size - 1);
	} else {
		move_cursor(con, con->cursor_x, con->cursor_y + num);
	}
}

SHL_EXPORT
void tsm_screen_move_left(struct tsm_screen *con, unsigned int num)
{
	unsigned int x;

	if (!con || !num)
		return;

	screen_inc_age(con);

	if (num > con->size_x)
		num = con->size_x;

	x = con->cursor_x;
	if (x >= con->size_x)
		x = con->size_x - 1;

	if (num > x)
		move_cursor(con, 0, con->cursor_y);
	else
		move_cursor(con, x - num, con->cursor_y);
}

SHL_EXPORT
void tsm_screen_move_right(struct tsm_screen *con, unsigned int num)
{
	if (!con || !num)
		return;

	screen_inc_age(con);

	if (num > con->size_x)
		num = con->size_x;

	if (num + con->cursor_x >= con->size_x)
		move_cursor(con, con->size_x - 1, con->cursor_y);
	else
		move_cursor(con, con->cursor_x + num, con->cursor_y);
}

SHL_EXPORT
void tsm_screen_move_line_end(struct tsm_screen *con)
{
	if (!con)
		return;

	screen_inc_age(con);

	move_cursor(con, con->size_x - 1, con->cursor_y);
}

SHL_EXPORT
void tsm_screen_move_line_home(struct tsm_screen *con)
{
	if (!con)
		return;

	screen_inc_age(con);

	move_cursor(con, 0, con->cursor_y);
}

SHL_EXPORT
void tsm_screen_tab_right(struct tsm_screen *con, unsigned int num)
{
	unsigned int i, j, x;

	if (!con || !num)
		return;

	screen_inc_age(con);

	x = con->cursor_x;
	for (i = 0; i < num; ++i) {
		for (j = x + 1; j < con->size_x; ++j) {
			if (con->tab_ruler[j])
				break;
		}

		x = j;
		if (x + 1 >= con->size_x)
			break;
	}

	/* tabs never cause pending new-lines */
	if (x >= con->size_x)
		x = con->size_x - 1;

	move_cursor(con, x, con->cursor_y);
}

SHL_EXPORT
void tsm_screen_tab_left(struct tsm_screen *con, unsigned int num)
{
	unsigned int i, x;
	int j;

	if (!con || !num)
		return;

	screen_inc_age(con);

	x = con->cursor_x;
	for (i = 0; i < num; ++i) {
		for (j = x - 1; j > 0; --j) {
			if (con->tab_ruler[j])
				break;
		}

		if (j <= 0) {
			x = 0;
			break;
		}
		x = j;
	}

	move_cursor(con, x, con->cursor_y);
}

SHL_EXPORT
void tsm_screen_insert_lines(struct tsm_screen *con, unsigned int num)
{
	unsigned int i, j, max;

	if (!con || !num)
		return;

	if (con->cursor_y < con->margin_top ||
	    con->cursor_y > con->margin_bottom)
		return;

	screen_inc_age(con);
	/* TODO: more sophisticated ageing */
	con->age = con->age_cnt;

	max = con->margin_bottom - con->cursor_y + 1;
	if (num > max)
		num = max;

	struct line *cache[num];

	for (i = 0; i < num; ++i) {
		cache[i] = con->lines[con->margin_bottom - i];
		for (j = 0; j < con->size_x; ++j)
			screen_cell_init(con, &cache[i]->cells[j]);
	}

	if (num < max) {
		memmove(&con->lines[con->cursor_y + num],
			&con->lines[con->cursor_y],
			(max - num) * sizeof(struct line*));

		memcpy(&con->lines[con->cursor_y],
		       cache, num * sizeof(struct line*));
	}

	con->cursor_x = 0;
}

SHL_EXPORT
void tsm_screen_delete_lines(struct tsm_screen *con, unsigned int num)
{
	unsigned int i, j, max;

	if (!con || !num)
		return;

	if (con->cursor_y < con->margin_top ||
	    con->cursor_y > con->margin_bottom)
		return;

	screen_inc_age(con);
	/* TODO: more sophisticated ageing */
	con->age = con->age_cnt;

	max = con->margin_bottom - con->cursor_y + 1;
	if (num > max)
		num = max;

	struct line *cache[num];

	for (i = 0; i < num; ++i) {
		cache[i] = con->lines[con->cursor_y + i];
		for (j = 0; j < con->size_x; ++j)
			screen_cell_init(con, &cache[i]->cells[j]);
	}

	if (num < max) {
		memmove(&con->lines[con->cursor_y],
			&con->lines[con->cursor_y + num],
			(max - num) * sizeof(struct line*));

		memcpy(&con->lines[con->cursor_y + (max - num)],
		       cache, num * sizeof(struct line*));
	}

	con->cursor_x = 0;
}

SHL_EXPORT
void tsm_screen_insert_chars(struct tsm_screen *con, unsigned int num)
{
	struct cell *cells;
	unsigned int max, mv, i;

	if (!con || !num || !con->size_y || !con->size_x)
		return;

	screen_inc_age(con);
	/* TODO: more sophisticated ageing */
	con->age = con->age_cnt;

	if (con->cursor_x >= con->size_x)
		con->cursor_x = con->size_x - 1;
	if (con->cursor_y >= con->size_y)
		con->cursor_y = con->size_y - 1;

	max = con->size_x - con->cursor_x;
	if (num > max)
		num = max;
	mv = max - num;

	cells = con->lines[con->cursor_y]->cells;
	if (mv)
		memmove(&cells[con->cursor_x + num],
			&cells[con->cursor_x],
			mv * sizeof(*cells));

	for (i = 0; i < num; ++i)
		screen_cell_init(con, &cells[con->cursor_x + i]);
}

SHL_EXPORT
void tsm_screen_delete_chars(struct tsm_screen *con, unsigned int num)
{
	struct cell *cells;
	unsigned int max, mv, i;

	if (!con || !num || !con->size_y || !con->size_x)
		return;

	screen_inc_age(con);
	/* TODO: more sophisticated ageing */
	con->age = con->age_cnt;

	if (con->cursor_x >= con->size_x)
		con->cursor_x = con->size_x - 1;
	if (con->cursor_y >= con->size_y)
		con->cursor_y = con->size_y - 1;

	max = con->size_x - con->cursor_x;
	if (num > max)
		num = max;
	mv = max - num;

	cells = con->lines[con->cursor_y]->cells;
	if (mv)
		memmove(&cells[con->cursor_x],
			&cells[con->cursor_x + num],
			mv * sizeof(*cells));

	for (i = 0; i < num; ++i)
		screen_cell_init(con, &cells[con->cursor_x + mv + i]);
}

SHL_EXPORT
void tsm_screen_erase_cursor(struct tsm_screen *con)
{
	unsigned int x;

	if (!con)
		return;

	screen_inc_age(con);

	if (con->cursor_x >= con->size_x)
		x = con->size_x - 1;
	else
		x = con->cursor_x;

	screen_erase_region(con, x, con->cursor_y, x, con->cursor_y, false);
}

SHL_EXPORT
void tsm_screen_erase_chars(struct tsm_screen *con, unsigned int num)
{
	unsigned int x;

	if (!con || !num)
		return;

	screen_inc_age(con);

	if (con->cursor_x >= con->size_x)
		x = con->size_x - 1;
	else
		x = con->cursor_x;

	screen_erase_region(con, x, con->cursor_y, x + num - 1, con->cursor_y,
			     false);
}

SHL_EXPORT
void tsm_screen_erase_cursor_to_end(struct tsm_screen *con,
				        bool protect)
{
	unsigned int x;

	if (!con)
		return;

	screen_inc_age(con);

	if (con->cursor_x >= con->size_x)
		x = con->size_x - 1;
	else
		x = con->cursor_x;

	screen_erase_region(con, x, con->cursor_y, con->size_x - 1,
			     con->cursor_y, protect);
}

SHL_EXPORT
void tsm_screen_erase_home_to_cursor(struct tsm_screen *con,
					 bool protect)
{
	if (!con)
		return;

	screen_inc_age(con);

	screen_erase_region(con, 0, con->cursor_y, con->cursor_x,
			     con->cursor_y, protect);
}

SHL_EXPORT
void tsm_screen_erase_current_line(struct tsm_screen *con,
				       bool protect)
{
	if (!con)
		return;

	screen_inc_age(con);

	screen_erase_region(con, 0, con->cursor_y, con->size_x - 1,
			     con->cursor_y, protect);
}

SHL_EXPORT
void tsm_screen_erase_screen_to_cursor(struct tsm_screen *con,
					   bool protect)
{
	if (!con)
		return;

	screen_inc_age(con);

	screen_erase_region(con, 0, 0, con->cursor_x, con->cursor_y, protect);
}

SHL_EXPORT
void tsm_screen_erase_cursor_to_screen(struct tsm_screen *con,
					   bool protect)
{
	unsigned int x;

	if (!con)
		return;

	screen_inc_age(con);

	if (con->cursor_x >= con->size_x)
		x = con->size_x - 1;
	else
		x = con->cursor_x;

	screen_erase_region(con, x, con->cursor_y, con->size_x - 1,
			     con->size_y - 1, protect);
}

SHL_EXPORT
void tsm_screen_erase_screen(struct tsm_screen *con, bool protect)
{
	if (!con)
		return;

	screen_inc_age(con);

	screen_erase_region(con, 0, 0, con->size_x - 1, con->size_y - 1,
			     protect);
}
