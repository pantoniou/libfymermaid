/*
 * fymm-legend.h - labels moved out of a drawing that cannot hold them
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

#ifndef FYMM_LEGEND_H
#define FYMM_LEGEND_H

#include "fymm-canvas.h"

/*
 * A legend holds the labels a drawing has no room for. The drawing keeps a
 * short marker in their place and the legend spells them out beneath it, so
 * a narrow terminal loses the text of a label rather than the shape of the
 * diagram or the label itself.
 *
 * This is what the Wardley map has always done with its node names, and what
 * FYMM_FIT_LEGEND asks any diagram to do with a label that will not fit.
 */
struct fymm_legend;

struct fymm_legend *fymm_legend_create(void);
void fymm_legend_destroy(struct fymm_legend *lg);

/*
 * Take @label into the legend and return its index. The same label twice gets
 * the same index. Returns (size_t)-1 only when it cannot allocate.
 */
size_t fymm_legend_add(struct fymm_legend *lg, const char *label);

/* The index of @label if the legend holds it, else (size_t)-1. A drawing pass
 * looks up rather than adds, so it cannot grow a legend already measured. */
size_t fymm_legend_find(const struct fymm_legend *lg, const char *label);

/* The marker to draw in place of entry @idx, and the colour to draw it in.
 *
 * A marker is a number, so the drawing and the legend are tied by the colour
 * as much as by the digit: draw the marker in fymm_legend_color() and the eye
 * finds the entry without counting. */
const char *fymm_legend_marker(const struct fymm_legend *lg, size_t idx);
int fymm_legend_color(const struct fymm_legend *lg, size_t idx);

/* How many entries the legend holds, and the rows drawing it needs. */
size_t fymm_legend_count(const struct fymm_legend *lg);
int fymm_legend_rows(const struct fymm_legend *lg);

/* The cells the widest entry needs. */
int fymm_legend_width(const struct fymm_legend *lg);

/*
 * Draw the legend with its top left at (@x, @y). Returns the rows used.
 */
int fymm_legend_draw(struct fymm_canvas *cv, int x, int y,
		     const struct fymm_legend *lg);

#endif /* FYMM_LEGEND_H */
