/*
 * fymm-render-timeline.c - drawing a timeline model down the terminal
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
 * Mermaid lays a timeline out across the page, one column per task. A
 * terminal is tall and narrow, and task text is prose, so the layout is
 * turned on its side here: sections run down the page, each ruled and
 * coloured, with its tasks beneath it and their events under each task.
 *
 * The orientation in the model is recorded but does not change this; there is
 * no useful second axis to swap to.
 */
char *fymm_render_timeline(const struct fymm_diagram *d, fy_generic model,
			   const struct fymm_render_cfg *cfg)
{
	fy_generic sections, section, tasks, task, events, event;
	struct fymm_canvas *cv;
	struct fymm_theme theme;
	const char *title, *name;
	size_t nsections, ntasks, nevents, si, ti, ei;
	int width, height, y, w, rule, color;
	uint32_t bullet, dot;
	char *out;

	if (fymm_theme_resolve(&theme, cfg, fymm_diagram_builder(d), model))
		return NULL;

	sections = fy_get(model, "sections");
	title = fy_get(model, "title", (const char *)NULL);
	nsections = fy_is_sequence(sections) ? fy_len(sections) : 0;
	if (!nsections)
		return strdup("");

	/* measure: the widest line decides the width, and the rows are the
	 * sum of every section, task and event plus a blank between sections */
	width = title ? fymm_rich_measure(title) : 0;
	height = title ? 2 : 0;
	for (si = 0; si < nsections; si++) {
		section = fy_get_at(sections, si);
		name = fy_get(section, "name", (const char *)NULL);
		if (name) {
			w = fymm_rich_measure(name) + 6;
			if (w > width)
				width = w;
			height += 1;
		}
		tasks = fy_get(section, "tasks");
		ntasks = fy_is_sequence(tasks) ? fy_len(tasks) : 0;
		for (ti = 0; ti < ntasks; ti++) {
			task = fy_get_at(tasks, ti);
			w = fymm_rich_measure(fy_get(task, "name", "")) + 4;
			if (w > width)
				width = w;
			height += 1;
			events = fy_get(task, "events");
			nevents = fy_is_sequence(events) ? fy_len(events) : 0;
			for (ei = 0; ei < nevents; ei++) {
				w = fymm_rich_measure(fy_get_at(events, ei, "")) + 8;
				if (w > width)
					width = w;
			}
			height += (int)nevents;
		}
		if (si + 1 < nsections)
			height += 1;
	}
	width += 2;

	cv = fymm_canvas_create_cfg(width, height, cfg, &theme);
	if (!cv)
		return NULL;

	bullet = cv->charset == FYMM_CHARSET_ASCII ? '*' : 0x25cf;
	dot = cv->charset == FYMM_CHARSET_ASCII ? '-' : 0x00b7;

	y = 0;
	if (title) {
		fymm_rich_text(cv, 0, y, title, FYMM_PAL_TITLE,
				 FYMM_ATTR_BOLD);
		y += 2;
	}

	for (si = 0; si < nsections; si++) {
		section = fy_get_at(sections, si);
		color = (int)(si % 8);
		name = fy_get(section, "name", (const char *)NULL);

		/* the section heading, ruled out to the full width */
		if (name) {
			w = fymm_rich_text(cv, 0, y, name, color,
					   FYMM_ATTR_BOLD);
			rule = width - w - 3;
			if (rule > 0)
				fymm_canvas_hline(cv, y, w + 1, w + rule, color,
						  false);
			y++;
		}

		tasks = fy_get(section, "tasks");
		ntasks = fy_is_sequence(tasks) ? fy_len(tasks) : 0;
		for (ti = 0; ti < ntasks; ti++) {
			task = fy_get_at(tasks, ti);
			fymm_canvas_put(cv, 1, y, bullet, color,
					FYMM_ATTR_BOLD);
			fymm_rich_text(cv, 3, y, fy_get(task, "name", ""),
				       FYMM_COLOR_DEFAULT, 0);
			y++;

			events = fy_get(task, "events");
			nevents = fy_is_sequence(events) ? fy_len(events) : 0;
			for (ei = 0; ei < nevents; ei++) {
				event = fy_get_at(events, ei);
				fymm_canvas_put(cv, 5, y, dot, color, 0);
				fymm_rich_text(cv, 7, y, fy_str(event),
					       FYMM_PAL_LABEL, 0);
				y++;
			}
		}
		if (si + 1 < nsections)
			y++;
	}

	out = fymm_canvas_emit(cv);
	fymm_canvas_destroy(cv);
	return out;
}
