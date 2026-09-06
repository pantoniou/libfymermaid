/*
 * fymm-render-gantt.c - drawing a gantt chart as bars on a day axis
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

/* struct gt_bar - one task, resolved to a span of days */
struct gt_bar {
	const char *name;
	const char *id;
	fy_generic tags;
	fy_generic raw_start, raw_end;
	double start, end;
	bool placed;
	bool milestone;
	int section;
};

static int gt_find(const struct gt_bar *bar, size_t n, const char *id)
{
	size_t i;

	if (!id || !*id)
		return -1;
	for (i = 0; i < n; i++) {
		if (bar[i].id && !strcmp(bar[i].id, id))
			return (int)i;
	}
	return -1;
}

static bool gt_has_tag(fy_generic tags, const char *want)
{
	fy_generic t;

	if (!fy_is_sequence(tags))
		return false;
	fy_foreach(t, tags) {
		if (fy_equal(t, want))
			return true;
	}
	return false;
}

/*
 * Resolve every task to a start and an end day. A task may be given absolute
 * dates, a duration, or a position relative to another task, so the passes
 * repeat until nothing more can be placed; a task that still cannot be, one
 * that names a task that does not exist or that depends on itself, follows
 * the previous task instead of vanishing.
 */
static void gt_resolve(struct gt_bar *bar, size_t n)
{
	size_t i, pass;
	bool moved = true;
	double cursor = 0.0;
	int ref;

	for (pass = 0; moved && pass <= n; pass++) {
		moved = false;
		for (i = 0; i < n; i++) {
			struct gt_bar *b = &bar[i];
			fy_generic s = b->raw_start, en = b->raw_end;
			bool have_start = false, have_end = false;
			double sv = 0.0, ev = 0.0;

			if (b->placed)
				continue;

			if (fy_equal(fy_get(s, "kind"), "date")) {
				sv = (double)(long long)fy_get(s, "day", 0LL);
				have_start = true;
			} else if (fy_equal(fy_get(s, "kind"), "after")) {
				ref = gt_find(bar, n, fy_get(s, "ref", ""));
				if (ref >= 0 && bar[ref].placed &&
				    (size_t)ref != i) {
					sv = bar[ref].end;
					have_start = true;
				}
			}

			if (fy_equal(fy_get(en, "kind"), "date")) {
				ev = (double)(long long)fy_get(en, "day", 0LL);
				have_end = true;
			} else if (fy_equal(fy_get(en, "kind"), "until")) {
				ref = gt_find(bar, n, fy_get(en, "ref", ""));
				if (ref >= 0 && bar[ref].placed &&
				    (size_t)ref != i) {
					ev = bar[ref].start;
					have_end = true;
				}
			} else if (fy_equal(fy_get(en, "kind"), "duration") &&
				   have_start) {
				ev = sv + fy_number(fy_get(en, "days"), 1.0);
				have_end = true;
			}

			if (have_start && !have_end)
				ev = sv + 1.0;
			if (have_end && !have_start)
				sv = ev - 1.0;
			if (!have_start && !have_end)
				continue;

			b->start = sv;
			b->end = ev;
			b->placed = true;
			moved = true;
		}
	}

	/* whatever is left follows the task before it */
	for (i = 0; i < n; i++) {
		if (bar[i].placed) {
			if (bar[i].end > cursor)
				cursor = bar[i].end;
			continue;
		}
		bar[i].start = cursor;
		bar[i].end = cursor + 1.0;
		bar[i].placed = true;
		cursor = bar[i].end;
	}
}

char *fymm_render_gantt(const struct fymm_diagram *d, fy_generic model,
			const struct fymm_render_cfg *cfg)
{
	fy_generic sections, section, tasks, task;
	struct fymm_canvas *cv;
	struct fymm_theme theme;
	struct gt_bar *bar = NULL;
	const char *title, *name;
	size_t nsections, ntasks, si, ti, n = 0, i;
	int width, height, y, x, w, name_w, plot, color;
	double lo, hi, span;
	bool ascii;
	char *out = NULL;

	if (fymm_theme_resolve(&theme, cfg, fymm_diagram_builder(d), model))
		return NULL;

	sections = fy_get(model, "sections");
	title = fy_get(model, "title", (const char *)NULL);
	nsections = fy_is_sequence(sections) ? fy_len(sections) : 0;

	for (si = 0; si < nsections; si++)
		n += fy_len(fy_get(fy_get_at(sections, si), "tasks"));
	if (!n)
		return strdup("");

	bar = calloc(n, sizeof(*bar));
	if (!bar)
		return NULL;

	i = 0;
	name_w = 0;
	for (si = 0; si < nsections; si++) {
		section = fy_get_at(sections, si);
		tasks = fy_get(section, "tasks");
		ntasks = fy_is_sequence(tasks) ? fy_len(tasks) : 0;
		for (ti = 0; ti < ntasks; ti++, i++) {
			task = fy_get_at(tasks, ti);
			bar[i].name = fy_get(task, "name", "");
			bar[i].id = fy_get(task, "id", (const char *)NULL);
			bar[i].tags = fy_get(task, "tags");
			bar[i].raw_start = fy_get(task, "start");
			bar[i].raw_end = fy_get(task, "end");
			bar[i].milestone = gt_has_tag(bar[i].tags, "milestone");
			bar[i].section = (int)si;
			w = fymm_rich_measure(bar[i].name);
			if (w > name_w)
				name_w = w;
		}
	}

	gt_resolve(bar, n);

	lo = bar[0].start;
	hi = bar[0].end;
	for (i = 0; i < n; i++) {
		if (bar[i].start < lo)
			lo = bar[i].start;
		if (bar[i].end > hi)
			hi = bar[i].end;
	}
	span = hi - lo;
	if (span <= 0.0)
		span = 1.0;

	plot = 40;
	if (cfg && cfg->width > 0) {
		w = cfg->width - name_w - 6;
		if (w >= 10 && w < plot)
			plot = w;
	}

	width = name_w + 3 + plot + 2;
	height = (title ? 2 : 0) + (int)n + (int)nsections;

	cv = fymm_canvas_create(width, height,
				cfg && cfg->charset != FYMM_CHARSET_AUTO ?
					cfg->charset : FYMM_CHARSET_UNICODE,
				cfg ? cfg->color : FYMM_COLOR_NONE, &theme);
	if (!cv)
		goto out;
	ascii = cv->charset == FYMM_CHARSET_ASCII;

	y = 0;
	if (title) {
		fymm_rich_text(cv, 0, y, title, FYMM_PAL_TITLE,
				 FYMM_ATTR_BOLD);
		y += 2;
	}

	i = 0;
	for (si = 0; si < nsections; si++) {
		section = fy_get_at(sections, si);
		name = fy_get(section, "name", (const char *)NULL);
		color = (int)(si % 8);

		if (name && *name) {
			w = fymm_rich_text(cv, 0, y, name, color,
					   FYMM_ATTR_BOLD);
			if (width - w - 2 > 0)
				fymm_canvas_hline(cv, y, w + 1, width - 2,
						  color, false);
		}
		y++;

		ntasks = fy_len(fy_get(section, "tasks"));
		for (ti = 0; ti < ntasks; ti++, i++) {
			int x0 = name_w + 2 +
				 (int)((bar[i].start - lo) / span * plot);
			int x1 = name_w + 2 +
				 (int)((bar[i].end - lo) / span * plot);

			x = name_w - fymm_rich_measure(bar[i].name);
			fymm_rich_text(cv, x < 0 ? 0 : x, y, bar[i].name,
				       FYMM_COLOR_DEFAULT, 0);

			if (x1 <= x0)
				x1 = x0 + 1;
			if (bar[i].milestone) {
				fymm_canvas_put(cv, x0, y,
						ascii ? '<' : 0x25c6, color,
						FYMM_ATTR_BOLD);
			} else {
				for (x = x0; x < x1; x++)
					fymm_canvas_put(cv, x, y,
							ascii ? '=' : 0x2588,
							color,
							gt_has_tag(bar[i].tags,
								   "crit") ?
								FYMM_ATTR_BOLD :
								0);
			}
			y++;
		}
	}

	out = fymm_canvas_emit(cv);
	fymm_canvas_destroy(cv);

out:
	free(bar);
	return out;
}
