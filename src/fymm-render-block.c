/*
 * fymm-render-block.c - drawing a block diagram as a grid of boxes
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

#define BK_MIN_W 9

/*
 * A block diagram is a grid: `columns` says how many blocks fit on a row, and
 * a block may span several of them. Blocks nested inside a compound block are
 * drawn as their own row beneath it, indented, since a terminal cannot nest a
 * box inside a box and keep either legible.
 */
struct fymm_canvas *fymm_render_block(const struct fymm_diagram *d,
				      fy_generic model,
				      const struct fymm_render_cfg *cfg)
{
	fy_generic blocks, block, config;
	struct fymm_canvas *cv = NULL;
	struct fymm_theme theme;
	struct fymm_metrics met;
	const char *title, *text, *parent;
	size_t nblocks, i;
	int columns, cell, width, height, top, x, y, col, row, w, color, span;
	int depth;
	bool ascii;

	fymm_metrics_resolve(&met, fymm_diagram_type(d), cfg);

	if (fymm_theme_resolve(&theme, cfg, fymm_diagram_builder(d), model))
		return NULL;

	blocks = fy_get(model, "blocks");
	config = fy_get(model, "config");
	title = fy_get(model, "title", (const char *)NULL);
	nblocks = fy_is_sequence(blocks) ? fy_len(blocks) : 0;
	if (!nblocks)
		return fymm_canvas_empty(cfg);

	columns = (int)fy_number(fy_get(config, "columns"), 0.0);
	if (columns <= 0)
		columns = 3;

	/* every cell is as wide as the widest label needs */
	cell = BK_MIN_W;
	for (i = 0; i < nblocks; i++) {
		block = fy_get_at(blocks, i);
		text = fy_get(block, "label", (const char *)NULL);
		if (!text)
			text = fy_get(block, "id", "");
		w = fymm_rich_measure(text) + 4;
		if (w > cell)
			cell = w;
	}
	if (cfg && cfg->width > 0) {
		w = (cfg->width - 2) / columns - met.col_gap;
		if (w >= 6 && w < cell)
			cell = w;
	}

	/* lay the blocks out row by row, wrapping at the column count */
	top = title ? 2 : 0;
	col = 0;
	row = 0;
	for (i = 0; i < nblocks; i++) {
		block = fy_get_at(blocks, i);
		span = (int)fy_number(fy_get(block, "span"), 1.0);
		if (span < 1)
			span = 1;
		if (span > columns)
			span = columns;
		if (col + span > columns) {
			col = 0;
			row++;
		}
		col += span;
		if (col >= columns) {
			col = 0;
			row++;
		}
	}
	height = top + (row + 1) * 4;
	width = columns * (cell + met.col_gap) + 2;
	if (title && fymm_rich_measure(title) + 2 > width)
		width = fymm_rich_measure(title) + 2;

	cv = fymm_canvas_create_cfg(width, height, cfg, &theme);
	if (!cv)
		return NULL;
	ascii = cv->charset == FYMM_CHARSET_ASCII;

	if (title)
		fymm_rich_text(cv, 0, 0, title, FYMM_PAL_TITLE,
				 FYMM_ATTR_BOLD);

	col = 0;
	row = 0;
	for (i = 0; i < nblocks; i++) {
		block = fy_get_at(blocks, i);
		text = fy_get(block, "label", (const char *)NULL);
		if (!text)
			text = fy_get(block, "id", "");
		span = (int)fy_number(fy_get(block, "span"), 1.0);
		if (span < 1)
			span = 1;
		if (span > columns)
			span = columns;
		if (col + span > columns) {
			col = 0;
			row++;
		}

		parent = fy_get(block, "parent", (const char *)NULL);
		depth = parent ? 1 : 0;
		x = col * (cell + met.col_gap) + depth;
		y = top + row * 4;
		w = span * cell + (span - 1) * met.col_gap - depth;
		color = (int)(i % 8);

		/* a compound block is a label with a rule, not a box: its
		 * children follow it and would be drawn inside one */
		if (!strcmp(fy_get(block, "shape", "rect"), "group")) {
			int tw = fymm_canvas_text(cv, x, y + 1, text, color,
						  FYMM_ATTR_BOLD);

			if (w - tw - 1 > 0)
				fymm_canvas_hline(cv, y + 1, x + tw + 1,
						  x + w - 1, color, false);
		} else {
			int j;

			for (j = 1; j + 1 < w; j++) {
				fymm_canvas_put(cv, x + j, y,
						ascii ? '-' : 0x2500, color, 0);
				fymm_canvas_put(cv, x + j, y + 2,
						ascii ? '-' : 0x2500, color, 0);
			}
			fymm_canvas_put(cv, x, y, ascii ? '+' : 0x250c, color, 0);
			fymm_canvas_put(cv, x + w - 1, y, ascii ? '+' : 0x2510,
					color, 0);
			fymm_canvas_put(cv, x, y + 2, ascii ? '+' : 0x2514,
					color, 0);
			fymm_canvas_put(cv, x + w - 1, y + 2,
					ascii ? '+' : 0x2518, color, 0);
			fymm_canvas_put(cv, x, y + 1, ascii ? '|' : 0x2502,
					color, 0);
			fymm_canvas_put(cv, x + w - 1, y + 1,
					ascii ? '|' : 0x2502, color, 0);
			fymm_rich_text(cv,
				       x + (w - fymm_rich_measure(text)) / 2,
				       y + 1, text, FYMM_COLOR_DEFAULT, 0);
		}

		col += span;
		if (col >= columns) {
			col = 0;
			row++;
		}
	}

	return cv;
}
