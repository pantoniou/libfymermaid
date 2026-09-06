/*
 * fymm-render-packet.c - drawing a packet diagram as rows of bits
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

/* how many bits a row of the diagram shows */
#define PK_BITS_PER_ROW 32

/*
 * The classic packet picture: rows of bits, each field a labelled box
 * spanning the bits it covers, with the bit numbers along the top. A field
 * that runs past the end of a row continues on the next.
 */
char *fymm_render_packet(const struct fymm_diagram *d, fy_generic model,
			 const struct fymm_render_cfg *cfg)
{
	fy_generic fields, field;
	struct fymm_canvas *cv;
	struct fymm_theme theme;
	const char *title, *label;
	size_t nfields, i;
	long long total = 0, start, end, b;
	int cellw, width, height, top, rows, x, y, color, w;
	bool ascii;
	char buf[32];
	char *out;

	if (fymm_theme_resolve(&theme, cfg, fymm_diagram_builder(d), model))
		return NULL;

	fields = fy_get(model, "fields");
	title = fy_get(model, "title", (const char *)NULL);
	nfields = fy_is_sequence(fields) ? fy_len(fields) : 0;
	if (!nfields)
		return strdup("");

	for (i = 0; i < nfields; i++) {
		end = (long long)fy_get(fy_get_at(fields, i), "end", 0LL);
		if (end + 1 > total)
			total = end + 1;
	}

	/* two cells a bit leaves room for the bit numbers above */
	cellw = 2;
	if (cfg && cfg->width > 0 &&
	    cfg->width < PK_BITS_PER_ROW * cellw + 4)
		cellw = 1;

	rows = (int)((total + PK_BITS_PER_ROW - 1) / PK_BITS_PER_ROW);
	top = title ? 2 : 0;
	width = PK_BITS_PER_ROW * cellw + 2;
	/* a number row, then a box of three rows for each row of bits */
	height = top + rows * 5;

	cv = fymm_canvas_create_cfg(width, height, cfg, &theme);
	if (!cv)
		return NULL;
	ascii = cv->charset == FYMM_CHARSET_ASCII;

	if (title)
		fymm_rich_text(cv, 0, 0, title, FYMM_PAL_TITLE,
				 FYMM_ATTR_BOLD);

	/* the bit numbers, every eighth bit */
	for (y = 0; y < rows; y++) {
		for (b = 0; b < PK_BITS_PER_ROW; b += 8) {
			snprintf(buf, sizeof(buf), "%lld",
				 (long long)y * PK_BITS_PER_ROW + b);
			fymm_canvas_text(cv, (int)b * cellw, top + y * 5, buf,
					 FYMM_PAL_LABEL, FYMM_ATTR_DIM);
		}
	}

	for (i = 0; i < nfields; i++) {
		field = fy_get_at(fields, i);
		start = (long long)fy_get(field, "start", 0LL);
		end = (long long)fy_get(field, "end", 0LL);
		label = fy_get(field, "label", "");
		color = (int)(i % 8);

		/* a field that crosses a row boundary is drawn once per row */
		for (b = start; b <= end; ) {
			long long row = b / PK_BITS_PER_ROW;
			long long rend = (row + 1) * PK_BITS_PER_ROW - 1;
			long long last = end < rend ? end : rend;
			int j;

			x = (int)(b % PK_BITS_PER_ROW) * cellw;
			y = top + (int)row * 5 + 1;
			w = (int)(last - b + 1) * cellw;

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

			if (fymm_rich_measure(label) + 2 <= w)
				fymm_rich_text(cv,
					x + (w - fymm_rich_measure(label)) / 2,
					y + 1, label, FYMM_COLOR_DEFAULT, 0);
			else if (w > 3)
				fymm_canvas_text(cv, x + 1, y + 1, "...",
						 FYMM_PAL_LABEL, FYMM_ATTR_DIM);

			b = last + 1;
		}
	}

	out = fymm_canvas_emit(cv);
	fymm_canvas_destroy(cv);
	return out;
}
