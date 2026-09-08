/*
 * fymm-render-kanban.c - drawing a kanban board as columns of cards
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fymm-canvas.h"
#include "fymm-internal.h"
#include "fymm-markdown.h"

#define KB_MIN_COL 12

/*
 * A board is columns side by side, each headed by its name and holding its
 * cards in order. That is what a kanban is for, and it is one of the few
 * mermaid layouts a terminal keeps unchanged.
 */
struct fymm_canvas *fymm_render_kanban(const struct fymm_diagram *d,
				       fy_generic model,
				       const struct fymm_render_cfg *cfg)
{
	fy_generic sections, section, items, item;
	struct fymm_canvas *cv = NULL;
	struct fymm_theme theme;
	struct fymm_metrics met;
	int *colw = NULL, *colx = NULL;
	const char *title, *text;
	size_t nsections, nitems, si, ii, tallest = 0;
	int width, height, top, x, y, w, color;
	bool ascii;

	fymm_metrics_resolve(&met, fymm_diagram_type(d), cfg);

	if (fymm_theme_resolve(&theme, cfg, fymm_diagram_builder(d), model))
		return NULL;

	sections = fy_get(model, "sections");
	title = fy_get(model, "title", (const char *)NULL);
	nsections = fy_is_sequence(sections) ? fy_len(sections) : 0;
	if (!nsections)
		return fymm_canvas_empty(cfg);

	colw = calloc(nsections, sizeof(*colw));
	colx = calloc(nsections, sizeof(*colx));
	if (!colw || !colx)
		goto out;

	x = 0;
	for (si = 0; si < nsections; si++) {
		section = fy_get_at(sections, si);
		w = fymm_rich_measure(fy_get(section, "text", ""));
		items = fy_get(section, "items");
		nitems = fy_is_sequence(items) ? fy_len(items) : 0;
		for (ii = 0; ii < nitems; ii++) {
			int iw = fymm_rich_measure(fy_get(fy_get_at(items, ii),
							  "text", ""));

			if (iw > w)
				w = iw;
		}
		colw[si] = w + 4;
		if (colw[si] < KB_MIN_COL)
			colw[si] = KB_MIN_COL;
		colx[si] = x;
		x += colw[si] + met.col_gap;
		if (nitems > tallest)
			tallest = nitems;
	}

	top = title ? 2 : 0;
	width = x;
	/* a heading, a rule, then three rows for every card */
	height = top + 2 + (int)tallest * 3;

	cv = fymm_canvas_create_cfg(width, height, cfg, &theme);
	if (!cv)
		goto out;
	ascii = cv->charset == FYMM_CHARSET_ASCII;

	if (title)
		fymm_rich_text(cv, 0, 0, title, FYMM_PAL_TITLE,
				 FYMM_ATTR_BOLD);

	for (si = 0; si < nsections; si++) {
		section = fy_get_at(sections, si);
		color = (int)(si % 8);
		text = fy_get(section, "text", "");

		fymm_rich_text(cv, colx[si] + 1, top, text, color,
			       FYMM_ATTR_BOLD);
		fymm_canvas_hline(cv, top + 1, colx[si],
				  colx[si] + colw[si] - 1, color, false);

		items = fy_get(section, "items");
		nitems = fy_is_sequence(items) ? fy_len(items) : 0;
		for (ii = 0; ii < nitems; ii++) {
			item = fy_get_at(items, ii);
			y = top + 2 + (int)ii * 3;
			x = colx[si];
			w = colw[si];

			/* each card is a small box of its own */
			for (int j = 1; j + 1 < w; j++) {
				fymm_canvas_put(cv, x + j, y,
						ascii ? '-' : 0x2500, color, 0);
				fymm_canvas_put(cv, x + j, y + 2,
						ascii ? '-' : 0x2500, color, 0);
			}
			fymm_canvas_put(cv, x, y, ascii ? '+' : 0x256d, color, 0);
			fymm_canvas_put(cv, x + w - 1, y, ascii ? '+' : 0x256e,
					color, 0);
			fymm_canvas_put(cv, x, y + 2, ascii ? '+' : 0x2570,
					color, 0);
			fymm_canvas_put(cv, x + w - 1, y + 2,
					ascii ? '+' : 0x256f, color, 0);
			fymm_canvas_put(cv, x, y + 1, ascii ? '|' : 0x2502,
					color, 0);
			fymm_canvas_put(cv, x + w - 1, y + 1,
					ascii ? '|' : 0x2502, color, 0);
			fymm_rich_text(cv, x + 2, y + 1,
				       fy_get(item, "text", ""),
				       FYMM_COLOR_DEFAULT, 0);
		}
	}

out:
	free(colw);
	free(colx);
	return cv;
}
