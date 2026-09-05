/*
 * fymm-render-radar.c - drawing a radar chart as grouped bars per axis
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

#define RD_BAR_CELLS 28

/* Eighth-width blocks, so a short bar still shows a difference. */
static const uint32_t rd_eighths[8] = {
	0x258f, 0x258e, 0x258d, 0x258c, 0x258b, 0x258a, 0x2589, 0x2588,
};

/*
 * A radar plot compares several curves across the same axes. Drawn as a
 * polygon in character cells it is unreadable: the axes land on whatever
 * cells the angles round to, and two curves within a few percent of each
 * other overlap entirely.
 *
 * The same comparison as bars grouped by axis keeps what the reader wants
 * from a radar -- how the curves differ, axis by axis -- and stays legible.
 */

/* The value a curve gives an axis, by position or by name. */
static double rd_value(fy_generic curve, fy_generic axis, size_t idx,
		       bool *havep)
{
	fy_generic values = fy_get(curve, "values");
	fy_generic v;
	size_t i;

	*havep = false;
	if (!fy_is_sequence(values))
		return 0.0;

	if (fy_get(curve, "keyed", false)) {
		const char *want = fy_get(axis, "name", "");

		for (i = 0; i < fy_len(values); i++) {
			v = fy_get_at(values, i);
			if (!strcmp(fy_get(v, "axis", ""), want)) {
				*havep = true;
				return fy_number(fy_get(v, "value"), 0.0);
			}
		}
		return 0.0;
	}
	if (idx >= fy_len(values))
		return 0.0;
	*havep = true;
	return fy_number(fy_get(fy_get_at(values, idx), "value"), 0.0);
}

char *fymm_render_radar(const struct fymm_diagram *d, fy_generic model,
			const struct fymm_render_cfg *cfg)
{
	fy_generic axes, curves, config, axis, curve;
	struct fymm_canvas *cv;
	struct fymm_theme theme;
	const char *title, *name;
	size_t naxes, ncurves, ai, ci;
	int width, height, y, x, w, label_w, bar_cells, full, part;
	double lo, hi, v, span, cells;
	bool have, ascii;
	char buf[64];
	char *out;

	if (fymm_theme_resolve(&theme, cfg, fymm_diagram_builder(d)))
		return NULL;

	axes = fy_get(model, "axes");
	curves = fy_get(model, "curves");
	config = fy_get(model, "config");
	title = fy_get(model, "title", (const char *)NULL);

	naxes = fy_is_sequence(axes) ? fy_len(axes) : 0;
	ncurves = fy_is_sequence(curves) ? fy_len(curves) : 0;
	if (!naxes || !ncurves)
		return strdup("");

	/* the scale: what min and max say, else what the values need */
	lo = fy_number(fy_get(config, "min"), 0.0);
	hi = fy_number(fy_get(config, "max"), 0.0);
	if (hi <= lo) {
		lo = 0.0;
		hi = 0.0;
		fy_foreach(curve, curves) {
			for (ai = 0; ai < naxes; ai++) {
				v = rd_value(curve, fy_get_at(axes, ai), ai,
					     &have);
				if (!have)
					continue;
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

	/* the widest curve name sets the gutter the bars start after */
	label_w = 0;
	fy_foreach(curve, curves) {
		name = fy_get(curve, "label", (const char *)NULL);
		if (!name)
			name = fy_get(curve, "name", "");
		w = fymm_text_width(name);
		if (w > label_w)
			label_w = w;
	}

	bar_cells = RD_BAR_CELLS;
	if (cfg && cfg->width > 0) {
		w = cfg->width - label_w - 14;
		if (w >= 8 && w < bar_cells)
			bar_cells = w;
	}

	width = 2 + label_w + 2 + bar_cells + 10;
	height = (title ? 2 : 0) + (int)naxes * (int)(ncurves + 2);

	cv = fymm_canvas_create(width, height,
				cfg && cfg->charset != FYMM_CHARSET_AUTO ?
					cfg->charset : FYMM_CHARSET_UNICODE,
				cfg ? cfg->color : FYMM_COLOR_NONE, &theme);
	if (!cv)
		return NULL;
	ascii = cv->charset == FYMM_CHARSET_ASCII;

	y = 0;
	if (title) {
		fymm_canvas_text(cv, 0, y, title, FYMM_PAL_TITLE,
				 FYMM_ATTR_BOLD);
		y += 2;
	}

	for (ai = 0; ai < naxes; ai++) {
		axis = fy_get_at(axes, ai);
		name = fy_get(axis, "label", (const char *)NULL);
		if (!name)
			name = fy_get(axis, "name", "");

		w = fymm_canvas_text(cv, 0, y, name, (int)(ai % 8),
				     FYMM_ATTR_BOLD);
		if (width - w - 2 > 0)
			fymm_canvas_hline(cv, y, w + 1, width - 2,
					  (int)(ai % 8), false);
		y++;

		for (ci = 0; ci < ncurves; ci++) {
			curve = fy_get_at(curves, ci);
			name = fy_get(curve, "label", (const char *)NULL);
			if (!name)
				name = fy_get(curve, "name", "");
			v = rd_value(curve, axis, ai, &have);

			x = label_w - fymm_text_width(name) + 2;
			fymm_canvas_text(cv, x < 0 ? 0 : x, y, name,
					 FYMM_PAL_LABEL, 0);
			x = label_w + 4;
			if (!have) {
				fymm_canvas_text(cv, x, y, "-", FYMM_PAL_LABEL,
						 FYMM_ATTR_DIM);
				y++;
				continue;
			}

			cells = (v - lo) / span * (double)bar_cells;
			full = (int)cells;
			part = (int)((cells - (double)full) * 8.0);
			if (ascii) {
				full = (int)(cells + 0.5);
				for (w = 0; w < full; w++)
					fymm_canvas_put(cv, x + w, y, '#',
							(int)(ci % 8), 0);
				x += full;
			} else {
				for (w = 0; w < full; w++)
					fymm_canvas_put(cv, x + w, y,
							rd_eighths[7],
							(int)(ci % 8), 0);
				x += full;
				if (part)
					fymm_canvas_put(cv, x++, y,
							rd_eighths[part - 1],
							(int)(ci % 8), 0);
			}
			snprintf(buf, sizeof(buf), "%g", v);
			fymm_canvas_text(cv, label_w + 5 + bar_cells, y, buf,
					 FYMM_PAL_TAG, 0);
			y++;
		}
		y++;
	}

	out = fymm_canvas_emit(cv);
	fymm_canvas_destroy(cv);
	return out;
}
