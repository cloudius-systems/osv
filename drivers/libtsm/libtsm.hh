/*
 * TSM - Main Header
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

#ifndef TSM_LIBTSM_H
#define TSM_LIBTSM_H

#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @mainpage
 *
 * TSM is a Terminal-emulator State Machine. It implements all common DEC-VT100
 * to DEC-VT520 control codes and features. A state-machine is used to parse TTY
 * input and saved in a virtual screen. TSM does not provide any rendering,
 * glyph/font handling or anything more advanced. TSM is just a simple
 * state-machine for control-codes handling.
 * The main use-case for TSM are terminal-emulators. TSM has no dependencies
 * other than an ISO-C99 compiler and C-library. Any terminal emulator for any
 * window-environment or rendering-pipline can make use of TSM. However, TSM can
 * also be used for control-code validation, TTY-screen-capturing or other
 * advanced users of terminal escape-sequences.
 */

/**
 * @defgroup misc Miscellaneous Definitions
 * Miscellaneous definitions
 *
 * This section contains several miscellaneous definitions of small helpers and
 * constants. These are shared between other parts of the API and have common
 * semantics/syntax.
 *
 * @{
 */

/**
 * Logging Callback
 *
 * @data: user-provided data
 * @file: Source code file where the log message originated or NULL
 * @line: Line number in source code or 0
 * @func: C function name or NULL
 * @subs: Subsystem where the message came from or NULL
 * @sev: Kernel-style severity between 0=FATAL and 7=DEBUG
 * @format: printf-formatted message
 * @args: arguments for printf-style @format
 *
 * This is the type of a logging callback function. You can always pass NULL
 * instead of such a function to disable logging.
 */
typedef void (*tsm_log_t) (void *data,
			   const char *file,
			   int line,
			   const char *func,
			   const char *subs,
			   unsigned int sev,
			   const char *format,
			   va_list args);

/** @} */

/**
 * @defgroup symbols Unicode Helpers
 * Unicode helpers
 *
 * Unicode uses 32bit types to uniquely represent symbols. However, combining
 * characters allow modifications of such symbols but require additional space.
 * To avoid passing around allocated strings, TSM provides a symbol-table which
 * can store combining-characters with their base-symbol to create a new symbol.
 * This way, only the symbol-identifiers have to be passed around (which are
 * simple integers). No string allocation is needed by the API user.
 *
 * The symbol table is currently not exported. Once the API is fixed, we will
 * provide it to outside users.
 *
 * Additionally, this contains some general UTF8/UCS4 helpers.
 *
 * @{
 */

/* UCS4 helpers */

#define TSM_UCS4_MAX (0x7fffffffUL)
#define TSM_UCS4_INVALID (TSM_UCS4_MAX + 1)
#define TSM_UCS4_REPLACEMENT (0xfffdUL)

/* ucs4 to utf8 converter */

unsigned int tsm_ucs4_get_width(uint32_t ucs4);
size_t tsm_ucs4_to_utf8(uint32_t ucs4, char *out);
char *tsm_ucs4_to_utf8_alloc(const uint32_t *ucs4, size_t len, size_t *len_out);

/* symbols */

typedef uint32_t tsm_symbol_t;

/** @} */

/**
 * @defgroup screen Terminal Screens
 * Virtual terminal-screen implementation
 *
 * A TSM screen respresents the real screen of a terminal/application. It does
 * not render anything, but only provides a table of cells. Each cell contains
 * the stored symbol, attributes and more. Applications iterate a screen to
 * render each cell on their framebuffer.
 *
 * Screens provide all features that are expected from terminals. They include
 * scroll-back buffers, alternate screens, cursor positions and selection
 * support. Thus, it needs event-input from applications to drive these
 * features. Most of them are optional, though.
 *
 * @{
 */

struct tsm_screen;
typedef uint_fast32_t tsm_age_t;

#define TSM_SCREEN_INSERT_MODE	0x01
#define TSM_SCREEN_AUTO_WRAP	0x02
#define TSM_SCREEN_REL_ORIGIN	0x04
#define TSM_SCREEN_INVERSE	0x08
#define TSM_SCREEN_HIDE_CURSOR	0x10
#define TSM_SCREEN_FIXED_POS	0x20
#define TSM_SCREEN_ALTERNATE	0x40

enum vte_color {
	COLOR_BLACK,
	COLOR_BLUE,
	COLOR_GREEN,
	COLOR_CYAN,
	COLOR_RED,
	COLOR_MAGENTA,
	COLOR_YELLOW,
	COLOR_LIGHT_GREY,
	COLOR_DARK_GREY,
	COLOR_LIGHT_BLUE,
	COLOR_LIGHT_GREEN,
	COLOR_LIGHT_CYAN,
	COLOR_LIGHT_RED,
	COLOR_LIGHT_MAGENTA,
	COLOR_LIGHT_YELLOW,
	COLOR_WHITE,
	COLOR_NUM
};

#define COLOR_FOREGROUND COLOR_LIGHT_GREY
#define COLOR_BACKGROUND COLOR_BLACK

struct tsm_screen_attr {
	int8_t fccode;			/* foreground color code or <0 for rgb */
	int8_t bccode;			/* background color code or <0 for rgb */
	unsigned int bold : 1;		/* bold character */
	unsigned int underline : 1;	/* underlined character */
	unsigned int inverse : 1;	/* inverse colors */
	unsigned int protect : 1;	/* cannot be erased */
	unsigned int blink : 1;		/* blinking character */
};

typedef int (*tsm_screen_draw_cb) (struct tsm_screen *con,
				   uint32_t id,
				   const uint32_t *ch,
				   size_t len,
				   unsigned int width,
				   unsigned int posx,
				   unsigned int posy,
				   const struct tsm_screen_attr *attr,
				   tsm_age_t age,
				   void *data);

typedef int (*tsm_screen_cursor_cb) (struct tsm_screen *con,
				   unsigned int posx,
				   unsigned int posy,
				   void *data);

int tsm_screen_new(struct tsm_screen **out, tsm_log_t log, void *log_data);
void tsm_screen_ref(struct tsm_screen *con);
void tsm_screen_unref(struct tsm_screen *con);

unsigned int tsm_screen_get_width(struct tsm_screen *con);
unsigned int tsm_screen_get_height(struct tsm_screen *con);
int tsm_screen_resize(struct tsm_screen *con, unsigned int x,
		      unsigned int y);
int tsm_screen_set_margins(struct tsm_screen *con,
			   unsigned int top, unsigned int bottom);
void tsm_screen_set_max_sb(struct tsm_screen *con, unsigned int max);
void tsm_screen_clear_sb(struct tsm_screen *con);

void tsm_screen_sb_up(struct tsm_screen *con, unsigned int num);
void tsm_screen_sb_down(struct tsm_screen *con, unsigned int num);
void tsm_screen_sb_page_up(struct tsm_screen *con, unsigned int num);
void tsm_screen_sb_page_down(struct tsm_screen *con, unsigned int num);
void tsm_screen_sb_reset(struct tsm_screen *con);

void tsm_screen_set_def_attr(struct tsm_screen *con,
			     const struct tsm_screen_attr *attr);
void tsm_screen_reset(struct tsm_screen *con);
void tsm_screen_set_flags(struct tsm_screen *con, unsigned int flags);
void tsm_screen_reset_flags(struct tsm_screen *con, unsigned int flags);
unsigned int tsm_screen_get_flags(struct tsm_screen *con);

unsigned int tsm_screen_get_cursor_x(struct tsm_screen *con);
unsigned int tsm_screen_get_cursor_y(struct tsm_screen *con);

void tsm_screen_set_tabstop(struct tsm_screen *con);
void tsm_screen_reset_tabstop(struct tsm_screen *con);
void tsm_screen_reset_all_tabstops(struct tsm_screen *con);

void tsm_screen_write(struct tsm_screen *con, tsm_symbol_t ch,
		      const struct tsm_screen_attr *attr);
void tsm_screen_newline(struct tsm_screen *con);
void tsm_screen_scroll_up(struct tsm_screen *con, unsigned int num);
void tsm_screen_scroll_down(struct tsm_screen *con, unsigned int num);
void tsm_screen_move_to(struct tsm_screen *con, unsigned int x,
			unsigned int y);
void tsm_screen_move_up(struct tsm_screen *con, unsigned int num,
			bool scroll);
void tsm_screen_move_down(struct tsm_screen *con, unsigned int num,
			  bool scroll);
void tsm_screen_move_left(struct tsm_screen *con, unsigned int num);
void tsm_screen_move_right(struct tsm_screen *con, unsigned int num);
void tsm_screen_move_line_end(struct tsm_screen *con);
void tsm_screen_move_line_home(struct tsm_screen *con);
void tsm_screen_tab_right(struct tsm_screen *con, unsigned int num);
void tsm_screen_tab_left(struct tsm_screen *con, unsigned int num);
void tsm_screen_insert_lines(struct tsm_screen *con, unsigned int num);
void tsm_screen_delete_lines(struct tsm_screen *con, unsigned int num);
void tsm_screen_insert_chars(struct tsm_screen *con, unsigned int num);
void tsm_screen_delete_chars(struct tsm_screen *con, unsigned int num);
void tsm_screen_erase_cursor(struct tsm_screen *con);
void tsm_screen_erase_chars(struct tsm_screen *con, unsigned int num);
void tsm_screen_erase_cursor_to_end(struct tsm_screen *con,
				    bool protect);
void tsm_screen_erase_home_to_cursor(struct tsm_screen *con,
				     bool protect);
void tsm_screen_erase_current_line(struct tsm_screen *con,
				   bool protect);
void tsm_screen_erase_screen_to_cursor(struct tsm_screen *con,
				       bool protect);
void tsm_screen_erase_cursor_to_screen(struct tsm_screen *con,
				       bool protect);
void tsm_screen_erase_screen(struct tsm_screen *con, bool protect);

tsm_age_t tsm_screen_draw(struct tsm_screen *con, tsm_screen_draw_cb draw_cb,
			  tsm_screen_cursor_cb cursor_cb, void *data);

/** @} */

/**
 * @defgroup vte State Machine
 * Virtual terminal emulation with state machine
 *
 * A TSM VTE object provides the terminal state machine. It takes input from the
 * application (which usually comes from a TTY/PTY from a client), parses it,
 * modifies the attach screen or returns data which has to be written back to
 * the client.
 *
 * Furthermore, VTE objects accept keyboard or mouse input from the application
 * which is interpreted compliant to DEV-VTs.
 *
 * @{
 */

/* virtual terminal emulator */

struct tsm_vte;

/* keep in sync with shl_xkb_mods */
enum tsm_vte_modifier {
	TSM_SHIFT_MASK		= (1 << 0),
	TSM_LOCK_MASK		= (1 << 1),
	TSM_CONTROL_MASK	= (1 << 2),
	TSM_ALT_MASK		= (1 << 3),
	TSM_LOGO_MASK		= (1 << 4),
};

/* keep in sync with TSM_INPUT_INVALID */
#define TSM_VTE_INVALID 0xffffffff

typedef void (*tsm_vte_write_cb) (struct tsm_vte *vte,
				  const char *u8,
				  size_t len,
				  void *data);

int tsm_vte_new(struct tsm_vte **out, struct tsm_screen *con,
		tsm_vte_write_cb write_cb, void *data,
		tsm_log_t log, void *log_data);
void tsm_vte_ref(struct tsm_vte *vte);
void tsm_vte_unref(struct tsm_vte *vte);

int tsm_vte_set_palette(struct tsm_vte *vte, const char *palette);
void tsm_vte_get_def_attr(struct tsm_vte *vte, struct tsm_screen_attr *out);

void tsm_vte_reset(struct tsm_vte *vte);
void tsm_vte_hard_reset(struct tsm_vte *vte);
void tsm_vte_input(struct tsm_vte *vte, const char *u8, size_t len);
bool tsm_vte_handle_keyboard(struct tsm_vte *vte, uint32_t keysym,
			     unsigned int mods);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* TSM_LIBTSM_H */
