/*
 * fymm-render-xychart.c - drawing an xy chart as bars and stepped lines
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

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fymm-braille.h"
#include "fymm-canvas.h"
#include "fymm-internal.h"
#include "fymm-markdown.h"

#define XY_MIN_SLOT 4

/*
 * Eighth-height block glyphs. A bar can then end part way up a cell, which
 * matters when the whole plot is sixteen rows tall and two values differ by
 * a few percent.
 */
static const uint32_t xy_eighths[8] = {
	0x2581, 0x2582, 0x2583, 0x2584, 0x2585, 0x2586, 0x2587, 0x2588,
};

/* The largest number of points any series carries. */
static size_t xy_max_points(fy_generic series)
{
	fy_generic s;
	size_t best = 0;

	fy_foreach(s, series) {
		fy_generic data = fy_get(s, "data");

		if (fy_is_sequence(data) && fy_len(data) > best)
			best = fy_len(data);
	}
	return best;
}

/*
 * Draw a stepped connection between two plotted points: across, down or up,
 * then across again. The two turns carry their own masks, so they read as
 * corners rather than as a line crossing the step.
 */
static void xy_step(struct fymm_canvas *cv, int x0, int y0, int x1, int y1,
		    int color)
{
	int mid = (x0 + x1) / 2;
	bool down = y1 > y0;
	int y;

	fymm_canvas_hline(cv, y0, x0 + 1, mid - 1, color, false);
	if (y0 != y1) {
		fymm_canvas_line(cv, mid, y0,
				 (uint8_t)(FYMM_LN_W |
					   (down ? FYMM_LN_S : FYMM_LN_N)),
				 color, false);
		for (y = (down ? y0 : y1) + 1; y < (down ? y1 : y0); y++)
			fymm_canvas_line(cv, mid, y, FYMM_LN_N | FYMM_LN_S,
					 color, false);
		fymm_canvas_line(cv, mid, y1,
				 (uint8_t)(FYMM_LN_E |
					   (down ? FYMM_LN_N : FYMM_LN_S)),
				 color, false);
	} else {
		fymm_canvas_line(cv, mid, y0, FYMM_LN_W | FYMM_LN_E, color,
				 false);
	}
	fymm_canvas_hline(cv, y1, mid + 1, x1 - 1, color, false);
}

struct fymm_canvas *fymm_render_xychart(const struct fymm_diagram *d,
					fy_generic model,
					const struct fymm_render_cfg *cfg)
{
	fy_generic series, xaxis, yaxis, s, data, point, categories;
	struct fymm_canvas *cv = NULL;
	struct fymm_theme theme;
	struct fymm_metrics met;
	const char *title, *name;
	size_t nseries, npoints, i, j;
	int plot_h, slot, left, top, width, height, x, y, base, color;
	int px, py, prev_x = 0, prev_y = 0;
	double lo, hi, v, span;
	bool ascii, first;
	struct fymm_braille *br = NULL;
	char buf[64];

	fymm_metrics_resolve(&met, fymm_diagram_type(d), cfg);

	if (fymm_theme_resolve(&theme, cfg, fymm_diagram_builder(d), model))
		return NULL;

	series = fy_get(model, "series");
	xaxis = fy_get(model, "xAxis");
	yaxis = fy_get(model, "yAxis");
	title = fy_get(model, "title", (const char *)NULL);
	categories = fy_get(xaxis, "categories");

	nseries = fy_is_sequence(series) ? fy_len(series) : 0;
	npoints = xy_max_points(series);
	if (!nseries || !npoints)
		return fymm_canvas_empty(cfg);

	/* the value range: what the y axis says, else what the data needs,
	 * always including zero so that a bar has a baseline to stand on */
	lo = fy_number(fy_get(yaxis, "min"), NAN);
	hi = fy_number(fy_get(yaxis, "max"), NAN);
	if (isnan(lo) || isnan(hi)) {
		lo = 0.0;
		hi = 0.0;
		fy_foreach(s, series) {
			fy_foreach(point, fy_get(s, "data")) {
				v = fy_number(fy_get(point, "value"), 0.0);
				if (v < lo)
					lo = v;
				if (v > hi)
					hi = v;
			}
		}
	}
	if (hi <= lo)
		hi = lo + 1.0;
	span = hi - lo;

	/* the widest y label sets the gutter */
	snprintf(buf, sizeof(buf), "%g", hi);
	left = fymm_text_width(buf);
	snprintf(buf, sizeof(buf), "%g", lo);
	if (fymm_text_width(buf) > left)
		left = fymm_text_width(buf);
	left += 2;

	slot = XY_MIN_SLOT;
	if (fy_is_sequence(categories)) {
		for (i = 0; i < fy_len(categories); i++) {
			int w = fymm_rich_measure(fy_get_at(categories, i, "")) + 2;

			if (w > slot)
				slot = w;
		}
	}
	if (cfg && cfg->width > 0) {
		int avail = (cfg->width - left - 2) / (int)npoints;

		if (avail >= 3 && avail < slot)
			slot = avail;
	}

	plot_h = met.plot_height;
	top = title ? 2 : 0;
	width = left + (int)npoints * slot + 2;
	height = top + plot_h + 3;

	cv = fymm_canvas_create_cfg(width, height, cfg, &theme);
	if (!cv)
		return NULL;
	ascii = fymm_charset_ascii(cv->charset);

	/* a line series is plotted on a braille surface when the charset
	 * allows it, and stepped through the cells when it does not */
	if (fymm_charset_rich(cv->charset))
		br = fymm_braille_create(width - left, plot_h);

	if (title)
		fymm_rich_text(cv, 0, 0, title, FYMM_PAL_TITLE,
				 FYMM_ATTR_BOLD);

	/* the axes */
	fymm_canvas_vline(cv, left - 1, top, top + plot_h - 1, FYMM_PAL_LABEL,
			  false);
	fymm_canvas_hline(cv, top + plot_h, left - 1,
			  left + (int)npoints * slot - 1, FYMM_PAL_LABEL,
			  false);
	snprintf(buf, sizeof(buf), "%g", hi);
	fymm_canvas_text(cv, left - 2 - fymm_text_width(buf), top, buf,
			 FYMM_PAL_LABEL, 0);
	snprintf(buf, sizeof(buf), "%g", lo);
	fymm_canvas_text(cv, left - 2 - fymm_text_width(buf), top + plot_h - 1,
			 buf, FYMM_PAL_LABEL, 0);

	/* where zero sits, so a bar knows its baseline */
	base = top + plot_h - 1 - (int)((0.0 - lo) / span * (plot_h - 1));
	if (base < top)
		base = top;
	if (base > top + plot_h - 1)
		base = top + plot_h - 1;

	for (i = 0; i < nseries; i++) {
		s = fy_get_at(series, i);
		data = fy_get(s, "data");
		color = (int)(i % 8);
		first = true;

		for (j = 0; j < fy_len(data); j++) {
			point = fy_get_at(data, j);
			v = fy_number(fy_get(point, "value"), 0.0);
			px = left + (int)j * slot + slot / 2;
			py = top + plot_h - 1 -
			     (int)((v - lo) / span * (plot_h - 1));

			if (!strcmp(fy_get(s, "kind", "bar"), "bar")) {
				int bw = slot - 2 > 1 ? slot - 2 : 1;
				int bx = px - bw / 2;
				int y0 = py < base ? py : base;
				int y1 = py < base ? base : py;
				int part, k;

				/* how far into the top cell the value
				 * reaches, in eighths */
				part = (int)(fabs(v - 0.0) / span *
					     (plot_h - 1) * 8.0) % 8;

				for (y = y0; y <= y1; y++) {
					uint32_t g = ascii ? '#' :
						xy_eighths[7];

					/* only a bar that grows upwards can
					 * show a partial cap */
					if (!ascii && y == y0 && py < base &&
					    part)
						g = xy_eighths[part - 1];
					for (k = 0; k < bw; k++)
						fymm_canvas_put(cv, bx + k, y,
								g, color, 0);
				}
			} else if (br) {
				/*
				 * On a braille surface the point keeps its
				 * place within the cell, so the line between
				 * two points is a line rather than a stair.
				 */
				int bx = (px - left) * 2;
				int by = (int)((double)(v - lo) / span *
					       (plot_h * 4 - 1));

				by = plot_h * 4 - 1 - by;
				if (!first)
					fymm_braille_line(br, prev_x, prev_y,
							  bx, by, color);
				fymm_braille_point(br, bx, by, color);
				prev_x = bx;
				prev_y = by;
				first = false;
			} else {
				if (!first)
					xy_step(cv, prev_x, prev_y, px, py,
						color);
				fymm_canvas_put(cv, px, py,
						ascii ? 'o' : 0x25cf, color,
						FYMM_ATTR_BOLD);
				prev_x = px;
				prev_y = py;
				first = false;
			}
		}
	}

	/* the category labels, or the point numbers when there are none */
	for (j = 0; j < npoints; j++) {
		x = left + (int)j * slot;
		if (fy_is_sequence(categories) && j < fy_len(categories))
			name = fy_get_at(categories, j, "");
		else {
			snprintf(buf, sizeof(buf), "%zu", j + 1);
			name = buf;
		}
		if (fymm_rich_measure(name) > slot)
			continue;
		fymm_rich_text(cv, x + (slot - fymm_rich_measure(name)) / 2,
				 top + plot_h + 1, name, FYMM_PAL_LABEL, 0);
	}

	/* a legend, when the series carry names */
	x = left;
	y = top + plot_h + 2;
	for (i = 0; i < nseries; i++) {
		name = fy_get(fy_get_at(series, i), "name", (const char *)NULL);
		if (!name)
			continue;
		fymm_canvas_put(cv, x, y,
				!strcmp(fy_get(fy_get_at(series, i), "kind",
					       "bar"), "bar") ?
					(ascii ? '#' : xy_eighths[7]) :
					(ascii ? 'o' : 0x25cf),
				(int)(i % 8), 0);
		x += 2 + fymm_rich_text(cv, x + 2, y, name, FYMM_PAL_LABEL,
					  0) + 2;
	}

	if (br) {
		fymm_braille_blit(cv, left, top, br);
		fymm_braille_destroy(br);
	}

	return cv;
}
