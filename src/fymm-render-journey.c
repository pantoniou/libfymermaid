/*
 * fymm-render-journey.c - drawing a user journey as scored steps
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

/* A journey score runs from one to five. */
#define JOURNEY_MAX_SCORE 5

/*
 * Each task carries how its actors felt about it, from one to five. That is a
 * small ordinal scale, so it is drawn as five pips rather than as a number:
 * the shape of the journey, and where it dips, is then visible down the
 * column without reading a single figure.
 */
struct fymm_canvas *fymm_render_journey(const struct fymm_diagram *d,
					fy_generic model,
					const struct fymm_render_cfg *cfg)
{
	fy_generic sections, section, tasks, task, actors;
	struct fymm_canvas *cv = NULL;
	struct fymm_theme theme;
	const char *title, *name;
	size_t nsections, ntasks, nactors, si, ti, ai;
	int width, height, y, x, w, name_w, color, score, i;
	uint32_t full, empty;

	if (fymm_theme_resolve(&theme, cfg, fymm_diagram_builder(d), model))
		return NULL;

	sections = fy_get(model, "sections");
	title = fy_get(model, "title", (const char *)NULL);
	nsections = fy_is_sequence(sections) ? fy_len(sections) : 0;
	if (!nsections)
		return fymm_canvas_empty(cfg);

	/* the widest task name aligns the pip columns across every section */
	name_w = 0;
	height = title ? 2 : 0;
	for (si = 0; si < nsections; si++) {
		section = fy_get_at(sections, si);
		if (fy_get(section, "name", (const char *)NULL))
			height++;
		tasks = fy_get(section, "tasks");
		ntasks = fy_is_sequence(tasks) ? fy_len(tasks) : 0;
		for (ti = 0; ti < ntasks; ti++) {
			w = fymm_rich_measure(fy_get(fy_get_at(tasks, ti),
						     "name", ""));
			if (w > name_w)
				name_w = w;
		}
		height += (int)ntasks;
		if (si + 1 < nsections)
			height++;
	}

	/* the actor list trails the pips, so measure the widest of those too */
	width = 0;
	for (si = 0; si < nsections; si++) {
		section = fy_get_at(sections, si);
		name = fy_get(section, "name", (const char *)NULL);
		if (name) {
			w = fymm_rich_measure(name) + 4;
			if (w > width)
				width = w;
		}
		tasks = fy_get(section, "tasks");
		ntasks = fy_is_sequence(tasks) ? fy_len(tasks) : 0;
		for (ti = 0; ti < ntasks; ti++) {
			task = fy_get_at(tasks, ti);
			actors = fy_get(task, "actors");
			nactors = fy_is_sequence(actors) ? fy_len(actors) : 0;
			w = 0;
			for (ai = 0; ai < nactors; ai++)
				w += fymm_rich_measure(fy_get_at(actors, ai, "")) + 2;
			w += name_w + JOURNEY_MAX_SCORE + 8;
			if (w > width)
				width = w;
		}
	}
	if (title && fymm_rich_measure(title) + 2 > width)
		width = fymm_rich_measure(title) + 2;

	cv = fymm_canvas_create_cfg(width, height, cfg, &theme);
	if (!cv)
		return NULL;

	full = cv->charset == FYMM_CHARSET_ASCII ? '#' : 0x25cf;
	empty = cv->charset == FYMM_CHARSET_ASCII ? '.' : 0x25cb;

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
		if (name) {
			w = fymm_rich_text(cv, 0, y, name, color,
					   FYMM_ATTR_BOLD);
			if (width - w - 3 > 0)
				fymm_canvas_hline(cv, y, w + 1, width - 2,
						  color, false);
			y++;
		}

		tasks = fy_get(section, "tasks");
		ntasks = fy_is_sequence(tasks) ? fy_len(tasks) : 0;
		for (ti = 0; ti < ntasks; ti++) {
			task = fy_get_at(tasks, ti);
			fymm_rich_text(cv, 2, y, fy_get(task, "name", ""),
				       FYMM_COLOR_DEFAULT, 0);

			/* the pips, filled to the score */
			score = (int)fy_number(fy_get(task, "score"), 0.0);
			x = name_w + 4;
			for (i = 0; i < JOURNEY_MAX_SCORE; i++)
				fymm_canvas_put(cv, x + i, y,
						i < score ? full : empty,
						i < score ? color :
							FYMM_PAL_LABEL, 0);
			x += JOURNEY_MAX_SCORE + 2;

			actors = fy_get(task, "actors");
			nactors = fy_is_sequence(actors) ? fy_len(actors) : 0;
			for (ai = 0; ai < nactors; ai++) {
				x += fymm_rich_text(cv, x, y,
						      fy_get_at(actors, ai, ""),
						      FYMM_PAL_TAG, 0);
				if (ai + 1 < nactors)
					x += fymm_canvas_text(cv, x, y, ", ",
							      FYMM_PAL_LABEL,
							      0);
			}
			y++;
		}
		if (si + 1 < nsections)
			y++;
	}

	return cv;
}
