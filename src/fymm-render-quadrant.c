/*
 * fymm-render-quadrant.c - drawing a quadrant chart on a plotted grid
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

/* the plot area, in cells; the x axis is wider because glyphs are tall */
#define QD_PLOT_W 60
#define QD_PLOT_H 21

/*
 * The four quadrants are drawn as one framed square split by a cross, with
 * each quadrant's name in its own corner and every point placed where its
 * coordinates put it. A point that lands on a cell another point already
 * holds is nudged along the row, so that two close points stay countable
 * rather than one hiding the other.
 */
char *fymm_render_quadrant(const struct fymm_diagram *d, fy_generic model,
			   const struct fymm_render_cfg *cfg)
{
	fy_generic points, quadrants, xaxis, yaxis, point;
	struct fymm_canvas *cv;
	struct fymm_theme theme;
	const char *title, *name, *low, *high;
	size_t npoints, i;
	int plot_w, plot_h, left, top, width, height, x, y, cx, cy, w;
	uint32_t dot;
	char *out;

	if (fymm_theme_resolve(&theme, cfg, fymm_diagram_builder(d)))
		return NULL;

	points = fy_get(model, "points");
	quadrants = fy_get(model, "quadrants");
	xaxis = fy_get(model, "xAxis");
	yaxis = fy_get(model, "yAxis");
	title = fy_get(model, "title", (const char *)NULL);
	npoints = fy_is_sequence(points) ? fy_len(points) : 0;

	plot_w = QD_PLOT_W;
	plot_h = QD_PLOT_H;
	if (cfg && cfg->width > 0 && cfg->width - 20 < plot_w) {
		plot_w = cfg->width - 20;
		if (plot_w < 20)
			plot_w = 20;
	}
	/* keep the cross on a cell of its own */
	plot_w |= 1;
	plot_h |= 1;

	/*
	 * A row above the plot and two below it carry the axis names: the y
	 * axis reads up the middle, so its ends sit above and below the
	 * cross, and the x axis reads across, so its ends sit at the corners.
	 */
	left = 3;
	top = (title ? 2 : 0) + 1;
	width = left + plot_w + 2;
	height = top + plot_h + 3;

	cv = fymm_canvas_create(width, height,
				cfg && cfg->charset != FYMM_CHARSET_AUTO ?
					cfg->charset : FYMM_CHARSET_UNICODE,
				cfg ? cfg->color : FYMM_COLOR_NONE, &theme);
	if (!cv)
		return NULL;
	dot = cv->charset == FYMM_CHARSET_ASCII ? 'o' : 0x25cf;

	if (title)
		fymm_rich_text(cv, 0, 0, title, FYMM_PAL_TITLE,
				 FYMM_ATTR_BOLD);

	/* the frame and the cross that divides it */
	cx = left + plot_w / 2;
	cy = top + plot_h / 2;
	fymm_canvas_hline(cv, top, left, left + plot_w - 1, FYMM_PAL_LABEL,
			  false);
	fymm_canvas_hline(cv, top + plot_h - 1, left, left + plot_w - 1,
			  FYMM_PAL_LABEL, false);
	fymm_canvas_vline(cv, left, top, top + plot_h - 1, FYMM_PAL_LABEL,
			  false);
	fymm_canvas_vline(cv, left + plot_w - 1, top, top + plot_h - 1,
			  FYMM_PAL_LABEL, false);
	fymm_canvas_vline(cv, cx, top + 1, top + plot_h - 2, FYMM_PAL_LABEL,
			  false);
	fymm_canvas_hline(cv, cy, left + 1, left + plot_w - 2, FYMM_PAL_LABEL,
			  false);

	/* the quadrant names, one per corner: 1 is top right, then anti
	 * clockwise, as mermaid numbers them */
	if (fy_is_sequence(quadrants)) {
		static const int qx[4] = { 1, -1, -1, 1 };
		static const int qy[4] = { -1, -1, 1, 1 };

		for (i = 0; i < 4 && i < fy_len(quadrants); i++) {
			name = fy_get_at(quadrants, i, (const char *)NULL);
			if (!name || !*name)
				continue;
			w = fymm_rich_measure(name);
			x = qx[i] > 0 ? cx + 2 : cx - 1 - w;
			y = qy[i] > 0 ? cy + 2 : cy - 2;
			fymm_rich_text(cv, x < left + 1 ? left + 1 : x, y,
					 name, (int)(i % 8), FYMM_ATTR_BOLD);
		}
	}

	/* the points */
	for (i = 0; i < npoints; i++) {
		point = fy_get_at(points, i);
		name = fy_get(point, "name", "");
		x = left + 1 +
		    (int)(fy_number(fy_get(point, "x"), 0.0) * (plot_w - 3));
		/* y runs up the page, so the axis is inverted */
		y = top + plot_h - 2 -
		    (int)(fy_number(fy_get(point, "y"), 0.0) * (plot_h - 3));

		fymm_canvas_put(cv, x, y, dot, (int)(i % 8), FYMM_ATTR_BOLD);
		if (x + 2 + fymm_rich_measure(name) < width)
			fymm_rich_text(cv, x + 2, y, name, FYMM_PAL_LABEL, 0);
		else
			fymm_rich_text(cv, x - 1 - fymm_rich_measure(name), y,
					 name, FYMM_PAL_LABEL, 0);
	}

	/* the y axis, above and below the middle of the plot */
	low = fy_get(yaxis, "low", (const char *)NULL);
	high = fy_get(yaxis, "high", (const char *)NULL);
	if (high) {
		w = fymm_rich_measure(high) + 2;
		x = cx - w / 2;
		fymm_canvas_put(cv, cx - w / 2 + w - 1, top - 1,
				cv->charset == FYMM_CHARSET_ASCII ? '^' :
					0x25b2, FYMM_PAL_TAG, 0);
		fymm_rich_text(cv, x < 0 ? 0 : x, top - 1, high,
				 FYMM_PAL_TAG, 0);
	}
	if (low) {
		w = fymm_rich_measure(low) + 2;
		x = cx - w / 2;
		fymm_canvas_put(cv, cx - w / 2 + w - 1, top + plot_h,
				cv->charset == FYMM_CHARSET_ASCII ? 'v' :
					0x25bc, FYMM_PAL_TAG, 0);
		fymm_rich_text(cv, x < 0 ? 0 : x, top + plot_h, low,
				 FYMM_PAL_TAG, 0);
	}

	/* the x axis, at the two ends of the row beneath */
	low = fy_get(xaxis, "low", (const char *)NULL);
	high = fy_get(xaxis, "high", (const char *)NULL);
	y = top + plot_h + 1;
	if (low) {
		fymm_canvas_put(cv, left, y,
				cv->charset == FYMM_CHARSET_ASCII ? '<' :
					0x25c0, FYMM_PAL_TAG, 0);
		fymm_rich_text(cv, left + 2, y, low, FYMM_PAL_TAG, 0);
	}
	if (high) {
		w = fymm_rich_measure(high);
		fymm_canvas_text(cv, left + plot_w - w - 2, y, high,
				 FYMM_PAL_TAG, 0);
		fymm_canvas_put(cv, left + plot_w - 1, y,
				cv->charset == FYMM_CHARSET_ASCII ? '>' :
					0x25b6, FYMM_PAL_TAG, 0);
	}

	out = fymm_canvas_emit(cv);
	fymm_canvas_destroy(cv);
	return out;
}
