/*
 * fymm-braille.c - a subpixel plotting surface over the cell grid
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

#include <stdlib.h>
#include <string.h>

#include "fymm-braille.h"

struct fymm_braille {
	int w, h;
	uint8_t *dot;		/* one byte of dots per cell */
	int8_t *color;		/* the colour that cell took */
};

/*
 * Which bit each subpixel is, by column then row. The braille block numbers
 * its dots 1-6 down the two columns and then adds 7 and 8 at the bottom, so
 * the fourth row is not where a reader of the block would first look.
 */
static const uint8_t braille_bit[2][4] = {
	{ 0x01, 0x02, 0x04, 0x40 },
	{ 0x08, 0x10, 0x20, 0x80 },
};

struct fymm_braille *fymm_braille_create(int w, int h)
{
	struct fymm_braille *b;

	if (w <= 0 || h <= 0)
		return NULL;
	b = calloc(1, sizeof(*b));
	if (!b)
		return NULL;
	b->w = w;
	b->h = h;
	b->dot = calloc((size_t)w * (size_t)h, sizeof(*b->dot));
	b->color = calloc((size_t)w * (size_t)h, sizeof(*b->color));
	if (!b->dot || !b->color) {
		fymm_braille_destroy(b);
		return NULL;
	}
	memset(b->color, FYMM_COLOR_DEFAULT,
	       (size_t)w * (size_t)h * sizeof(*b->color));
	return b;
}

void fymm_braille_destroy(struct fymm_braille *b)
{
	if (!b)
		return;
	free(b->dot);
	free(b->color);
	free(b);
}

void fymm_braille_point(struct fymm_braille *b, int sx, int sy, int color)
{
	size_t idx;
	int cx, cy;

	if (!b || sx < 0 || sy < 0 || sx >= b->w * 2 || sy >= b->h * 4)
		return;

	cx = sx / 2;
	cy = sy / 4;
	idx = (size_t)cy * (size_t)b->w + (size_t)cx;
	b->dot[idx] |= braille_bit[sx % 2][sy % 4];
	b->color[idx] = (int8_t)color;
}

void fymm_braille_line(struct fymm_braille *b, int sx0, int sy0, int sx1,
		       int sy1, int color)
{
	int dx, dy, sx, sy, err, e2;

	if (!b)
		return;

	/* Bresenham, so a line of any slope lands on the dots it passes */
	dx = sx1 > sx0 ? sx1 - sx0 : sx0 - sx1;
	dy = sy1 > sy0 ? sy1 - sy0 : sy0 - sy1;
	sx = sx0 < sx1 ? 1 : -1;
	sy = sy0 < sy1 ? 1 : -1;
	err = dx - dy;

	for (;;) {
		fymm_braille_point(b, sx0, sy0, color);
		if (sx0 == sx1 && sy0 == sy1)
			break;
		e2 = 2 * err;
		if (e2 > -dy) {
			err -= dy;
			sx0 += sx;
		}
		if (e2 < dx) {
			err += dx;
			sy0 += sy;
		}
	}
}

void fymm_braille_blit(struct fymm_canvas *cv, int x, int y,
		       const struct fymm_braille *b)
{
	size_t idx;
	int cx, cy;

	if (!cv || !b)
		return;
	for (cy = 0; cy < b->h; cy++) {
		for (cx = 0; cx < b->w; cx++) {
			idx = (size_t)cy * (size_t)b->w + (size_t)cx;
			if (!b->dot[idx])
				continue;
			fymm_canvas_put(cv, x + cx, y + cy,
					0x2800 + b->dot[idx],
					b->color[idx], 0);
		}
	}
}
