/*
 * fymm-render-pie.c - drawing a pie model as a proportional bar chart
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

/*
 * A circle drawn in character cells reads badly and measures worse: a reader
 * cannot compare two angles by eye at this resolution. The same data as a
 * sorted bar chart is proportional, comparable and reads at a glance, so that
 * is what a pie becomes here.
 *
 * Eighth-width block glyphs give a bar eight times the horizontal resolution
 * of a cell, which matters when a slice is a few percent of the whole.
 */
static const uint32_t pie_eighths[8] = {
	0x258f,		/* U+258F left one eighth block */
	0x258e, 0x258d, 0x258c, 0x258b, 0x258a, 0x2589,
	0x2588,		/* U+2588 full block */
};

#define PIE_BAR_CELLS 32

char *fymm_render_pie(const struct fymm_diagram *d, fy_generic model,
		      const struct fymm_render_cfg *cfg)
{
	fy_generic slices, config, slice;
	struct fymm_canvas *cv;
	struct fymm_theme theme;
	const char *title, *label;
	double total, value, share, cells;
	size_t nslices, i;
	int label_w, value_w, w, x, y, top, full, part;
	int width, height, bar_cells;
	bool show_data;
	char buf[64];
	char *out;

	if (fymm_theme_resolve(&theme, cfg, fymm_diagram_builder(d)))
		return NULL;

	config = fy_get(model, "config");
	slices = fy_get(model, "slices");
	title = fy_get(model, "title", (const char *)NULL);
	show_data = fy_get(config, "showData", false);
	if (cfg && fy_is_mapping(cfg->options))
		show_data = fy_get(cfg->options, "showData", show_data);

	nslices = fy_is_sequence(slices) ? fy_len(slices) : 0;
	if (!nslices)
		return strdup("");

	/* the widest label sets the gutter, and the widest value its column */
	label_w = 0;
	value_w = 0;
	total = 0.0;
	for (i = 0; i < nslices; i++) {
		slice = fy_get_at(slices, i);
		w = fymm_rich_measure(fy_get(slice, "label", ""));
		if (w > label_w)
			label_w = w;
		value = fy_number(fy_get(slice, "value"), 0.0);
		total += value;
		snprintf(buf, sizeof(buf), "%g", value);
		w = fymm_text_width(buf);
		if (w > value_w)
			value_w = w;
	}
	if (!show_data)
		value_w = 0;

	/* fit the bar to the terminal, leaving room for the labels and the
	 * ` 100.0%` that trails every row */
	bar_cells = PIE_BAR_CELLS;
	if (cfg && cfg->width > 0) {
		w = cfg->width - label_w - value_w - 12;
		if (w < 8)
			w = 8;
		if (w < bar_cells)
			bar_cells = w;
	}

	top = title ? 2 : 0;
	width = label_w + 2 + bar_cells + 8 + (value_w ? value_w + 2 : 0) + 2;
	height = top + (int)nslices;

	cv = fymm_canvas_create(width, height,
				cfg && cfg->charset != FYMM_CHARSET_AUTO ?
					cfg->charset : FYMM_CHARSET_UNICODE,
				cfg ? cfg->color : FYMM_COLOR_NONE, &theme);
	if (!cv)
		return NULL;

	if (title)
		fymm_rich_text(cv, 0, 0, title, FYMM_PAL_TITLE,
				 FYMM_ATTR_BOLD);

	for (i = 0; i < nslices; i++) {
		slice = fy_get_at(slices, i);
		label = fy_get(slice, "label", "");
		value = fy_number(fy_get(slice, "value"), 0.0);
		share = total > 0.0 ? value / total : 0.0;
		y = top + (int)i;

		/* the label, right aligned into the gutter */
		x = label_w - fymm_rich_measure(label);
		fymm_rich_text(cv, x < 0 ? 0 : x, y, label, FYMM_PAL_LABEL,
				 0);

		/* the bar, to an eighth of a cell */
		cells = share * (double)bar_cells;
		full = (int)cells;
		part = (int)((cells - (double)full) * 8.0);
		x = label_w + 2;
		if (cv->charset == FYMM_CHARSET_ASCII) {
			/* no partial glyphs; round to the nearest cell, and
			 * never lose a slice that has any value at all */
			full = (int)(cells + 0.5);
			if (!full && value > 0.0)
				full = 1;
			for (w = 0; w < full; w++)
				fymm_canvas_put(cv, x + w, y, '#',
						(int)(i % 8), 0);
			x += full;
		} else {
			for (w = 0; w < full; w++)
				fymm_canvas_put(cv, x + w, y, pie_eighths[7],
						(int)(i % 8), 0);
			x += full;
			if (part)
				fymm_canvas_put(cv, x++, y,
						pie_eighths[part - 1],
						(int)(i % 8), 0);
			else if (!full && value > 0.0)
				fymm_canvas_put(cv, x++, y, pie_eighths[0],
						(int)(i % 8), 0);
		}

		/* the share, then the value when showData asked for it */
		x = label_w + 2 + bar_cells + 1;
		snprintf(buf, sizeof(buf), "%5.1f%%", share * 100.0);
		x += fymm_canvas_text(cv, x, y, buf, FYMM_PAL_LABEL, 0);
		if (show_data) {
			snprintf(buf, sizeof(buf), "%g", value);
			w = value_w - fymm_text_width(buf);
			fymm_canvas_text(cv, x + 2 + (w > 0 ? w : 0), y, buf,
					 FYMM_PAL_TAG, 0);
		}
	}

	out = fymm_canvas_emit(cv);
	fymm_canvas_destroy(cv);
	return out;
}
