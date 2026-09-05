/*
 * fymm-canvas.h - the character cell canvas the renderers draw on
 *
 * Copyright (c) 2026 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef FYMM_CANVAS_H
#define FYMM_CANVAS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <libfymermaid.h>

/* the four directions a line cell can reach towards */
#define FYMM_LN_N 0x01
#define FYMM_LN_S 0x02
#define FYMM_LN_E 0x04
#define FYMM_LN_W 0x08

/* text attributes */
#define FYMM_ATTR_BOLD 0x01
#define FYMM_ATTR_DIM 0x02

/* the second cell of a double width glyph; never drawn on its own */
#define FYMM_CP_CONT 0xfffffffeu

/* the palette a renderer picks colours out of; index 0..7 are the branch
 * colours mermaid calls git0..git7, and the named entries follow */
enum fymm_palette {
	FYMM_PAL_BRANCH0 = 0,
	FYMM_PAL_BRANCH7 = 7,
	FYMM_PAL_LABEL,
	FYMM_PAL_TAG,
	FYMM_PAL_TITLE,
	FYMM_PAL_COUNT,
};

/* struct fymm_pal_entry - one resolved palette entry
 *
 * @rgb: the colour, or FYMM_RGB_INVALID to leave the terminal's own
 * @attr: a mask of FYMM_ATTR_*, applied wherever the entry is used
 */
struct fymm_pal_entry {
	unsigned int rgb;
	uint8_t attr;
};

/* struct fymm_theme - a resolved palette, by enum fymm_palette index */
struct fymm_theme {
	struct fymm_pal_entry entry[FYMM_PAL_COUNT];
};

/* The key each palette entry is read from in a theme file. */
extern const char *const fymm_palette_keys[FYMM_PAL_COUNT];

/* Fill @theme in from the built-in default. */
void fymm_theme_default(struct fymm_theme *theme);

/* Apply the `colors` mapping of a theme generic over @theme. Returns the
 * number of keys that were not recognised. */
int fymm_theme_apply(struct fymm_theme *theme, fy_generic colors);

/* FYMM_COLOR_DEFAULT - leave the cell in the terminal's own colour */
#define FYMM_COLOR_DEFAULT (-1)

struct fymm_cell {
	uint32_t cp;		/* 0 for a line cell, else a literal codepoint */
	uint8_t lines;		/* a mask of FYMM_LN_*, when cp is 0 */
	bool dashed;
	int8_t color;		/* an enum fymm_palette index, or -1 */
	uint8_t attr;
};

struct fymm_canvas {
	int w, h;
	struct fymm_cell *cells;
	enum fymm_charset charset;
	enum fymm_color_mode color;
	struct fymm_theme theme;
};

struct fymm_canvas *fymm_canvas_create(int w, int h, enum fymm_charset charset,
				       enum fymm_color_mode color,
				       const struct fymm_theme *theme);
void fymm_canvas_destroy(struct fymm_canvas *cv);

void fymm_canvas_put(struct fymm_canvas *cv, int x, int y, uint32_t cp,
		     int color, uint8_t attr);
void fymm_canvas_line(struct fymm_canvas *cv, int x, int y, uint8_t mask,
		      int color, bool dashed);
void fymm_canvas_hline(struct fymm_canvas *cv, int y, int x0, int x1,
		       int color, bool dashed);
void fymm_canvas_vline(struct fymm_canvas *cv, int x, int y0, int y1,
		       int color, bool dashed);
int fymm_canvas_text(struct fymm_canvas *cv, int x, int y, const char *s,
		     int color, uint8_t attr);

/*
 * Route a link that leaves (@sx, @sy) heading down and arrives at (@dx, @dy)
 * from above, turning on row @ymid. Corners carry their own direction masks,
 * because a run of one cell reaches nowhere and would draw nothing.
 */
void fymm_canvas_route_v(struct fymm_canvas *cv, int sx, int sy, int dx,
			 int dy, int ymid, int color, bool dashed);

/* the display width of a UTF-8 string, in terminal columns */
int fymm_text_width(const char *s);

char *fymm_canvas_emit(struct fymm_canvas *cv);

#endif /* FYMM_CANVAS_H */
