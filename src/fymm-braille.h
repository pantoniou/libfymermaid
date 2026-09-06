/*
 * fymm-braille.h - a subpixel plotting surface over the cell grid
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

#ifndef FYMM_BRAILLE_H
#define FYMM_BRAILLE_H

#include "fymm-canvas.h"

/*
 * A braille glyph carries eight dots in two columns of four, so a grid of
 * them plots at twice the horizontal and four times the vertical resolution
 * of the cells underneath. That is what makes a line chart in a terminal
 * readable rather than a staircase.
 *
 * One colour per cell, not per dot: a braille cell is one character and takes
 * one colour. Where two series meet in a cell, the last one drawn wins, so
 * this suits a single line or a few that do not cross often. A bar chart is
 * better served by the block elements, which are a whole cell each and can be
 * coloured separately.
 *
 * Subpixel coordinates run from (0, 0) at the top left to (2 * w, 4 * h).
 */
struct fymm_braille;

struct fymm_braille *fymm_braille_create(int w, int h);
void fymm_braille_destroy(struct fymm_braille *b);

/* Set the dot at (@sx, @sy), and give its cell @color. Off-surface is a
 * no-op, so a caller may plot without clipping first. */
void fymm_braille_point(struct fymm_braille *b, int sx, int sy, int color);

/* The dots of the straight line between two subpixel points. */
void fymm_braille_line(struct fymm_braille *b, int sx0, int sy0, int sx1,
		       int sy1, int color);

/* Draw what was plotted, with its top left cell at (@x, @y). A cell with no
 * dots is left alone, so the surface does not blank what it sits over. */
void fymm_braille_blit(struct fymm_canvas *cv, int x, int y,
		       const struct fymm_braille *b);

#endif /* FYMM_BRAILLE_H */
