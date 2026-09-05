/*
 * fymm-render-class.c - drawing a class diagram as boxed compartments
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

#define CD_RANK_GAP 3
#define CD_COL_GAP 3

/* struct cd_box - one class, measured and placed */
struct cd_box {
	const char *id;
	const char *name;
	const char *generic;
	const char *annotation;
	fy_generic members;
	int rank;
	int x, y, w, h;
	int used;		/* the width its whole rank occupies */
};

static size_t cd_find(const struct cd_box *box, size_t n, const char *name)
{
	size_t i;

	for (i = 0; i < n; i++) {
		if (!strcmp(box[i].id, name))
			return i;
	}
	return (size_t)-1;
}

/* The glyph that marks one end of a relation. */
static uint32_t cd_end_glyph(enum fymm_charset cs, const char *end, bool down)
{
	if (!strcmp(end, "extension"))
		return cs == FYMM_CHARSET_ASCII ? (down ? 'V' : '^') :
			(down ? 0x25bd : 0x25b3);	/* hollow triangle */
	if (!strcmp(end, "composition"))
		return cs == FYMM_CHARSET_ASCII ? '*' : 0x25c6;	/* filled */
	if (!strcmp(end, "aggregation"))
		return cs == FYMM_CHARSET_ASCII ? 'o' : 0x25c7;	/* hollow */
	if (!strcmp(end, "lollipop"))
		return cs == FYMM_CHARSET_ASCII ? 'O' : 0x25cb;
	if (!strcmp(end, "arrow"))
		return cs == FYMM_CHARSET_ASCII ? (down ? 'v' : '^') :
			(down ? 0x25bc : 0x25b2);
	return 0;
}

/*
 * Rank the classes so that a relation points down the page. A class diagram
 * is usually shallow, so a longest-path pass over the relations that do not
 * close a cycle is enough; a relation that would push a class below itself is
 * left flat and drawn as a sideways link.
 */
static void cd_rank(struct cd_box *box, size_t n, fy_generic relations,
		    bool *flat)
{
	fy_generic rel;
	size_t i, from, to, passes, nrel;
	bool moved = true;

	nrel = fy_is_sequence(relations) ? fy_len(relations) : 0;
	for (passes = 0; moved && passes <= n; passes++) {
		moved = false;
		for (i = 0; i < nrel; i++) {
			if (flat[i])
				continue;
			rel = fy_get_at(relations, i);
			from = cd_find(box, n, fy_get(rel, "from", ""));
			to = cd_find(box, n, fy_get(rel, "to", ""));
			if (from == (size_t)-1 || to == (size_t)-1 || from == to)
				continue;
			if (box[to].rank <= box[from].rank) {
				if (passes == n) {
					flat[i] = true;
					continue;
				}
				box[to].rank = box[from].rank + 1;
				moved = true;
			}
		}
	}
}

char *fymm_render_class(const struct fymm_diagram *d, fy_generic model,
			const struct fymm_render_cfg *cfg)
{
	fy_generic classes, relations, cls, rel, member;
	struct fymm_canvas *cv;
	struct fymm_theme theme;
	struct cd_box *box = NULL;
	bool *flat = NULL;
	const char *title, *text;
	size_t nclasses, nrel, i, j, from, to;
	int r, x, y, w, top, nranks = 0, width = 0, height, color;
	int sx, sy, dx, dy, ymid;
	uint32_t g;
	bool ascii;
	char buf[256];
	char *out = NULL;

	if (fymm_theme_resolve(&theme, cfg, fymm_diagram_builder(d)))
		return NULL;

	classes = fy_get(model, "classes");
	relations = fy_get(model, "relations");
	title = fy_get(model, "title", (const char *)NULL);

	nclasses = fy_is_sequence(classes) ? fy_len(classes) : 0;
	nrel = fy_is_sequence(relations) ? fy_len(relations) : 0;
	if (!nclasses)
		return strdup("");

	box = calloc(nclasses, sizeof(*box));
	flat = calloc(nrel ? nrel : 1, sizeof(*flat));
	if (!box || !flat)
		goto out;

	/* measure: the widest of the name, the annotation and every member */
	for (i = 0; i < nclasses; i++) {
		cls = fy_get_at(classes, i);
		box[i].name = fy_get(cls, "label", (const char *)NULL);
		if (!box[i].name)
			box[i].name = fy_get(cls, "name", "");
		box[i].id = fy_get(cls, "name", "");
		box[i].generic = fy_get(cls, "generic", (const char *)NULL);
		box[i].annotation = fy_get(cls, "annotation", (const char *)NULL);
		box[i].members = fy_get(cls, "members");

		snprintf(buf, sizeof(buf), "%s%s%s%s", box[i].name,
			 box[i].generic ? "<" : "", box[i].generic ?
			 box[i].generic : "", box[i].generic ? ">" : "");
		w = fymm_text_width(buf);
		if (box[i].annotation) {
			int aw = fymm_text_width(box[i].annotation) + 4;

			if (aw > w)
				w = aw;
		}
		if (fy_is_sequence(box[i].members)) {
			fy_foreach(member, box[i].members) {
				int mw = fymm_text_width(fy_str(member));

				if (mw > w)
					w = mw;
			}
		}
		box[i].w = w + 4;

		/* a title row, an optional annotation row, a rule and the
		 * members, inside a border */
		box[i].h = 3 + (box[i].annotation ? 1 : 0);
		if (fy_is_sequence(box[i].members) && fy_len(box[i].members))
			box[i].h += 1 + (int)fy_len(box[i].members);
	}

	cd_rank(box, nclasses, relations, flat);
	for (i = 0; i < nclasses; i++) {
		if (box[i].rank + 1 > nranks)
			nranks = box[i].rank + 1;
	}

	/* lay each rank out left to right, then centre the narrow ones */
	top = title ? 2 : 0;
	y = top;
	for (r = 0; r < nranks; r++) {
		int used = 0, tall = 0;

		x = 0;
		for (i = 0; i < nclasses; i++) {
			if (box[i].rank != r)
				continue;
			box[i].x = x;
			box[i].y = y;
			x += box[i].w + CD_COL_GAP;
			if (box[i].h > tall)
				tall = box[i].h;
		}
		used = x - CD_COL_GAP;
		if (used > width)
			width = used;
		for (i = 0; i < nclasses; i++) {
			if (box[i].rank == r)
				box[i].used = used;
		}
		y += tall + CD_RANK_GAP;
	}
	for (i = 0; i < nclasses; i++)
		box[i].x += (width - box[i].used) / 2;

	height = y - CD_RANK_GAP;
	width += 2;
	if (title && fymm_text_width(title) + 2 > width)
		width = fymm_text_width(title) + 2;

	cv = fymm_canvas_create(width, height,
				cfg && cfg->charset != FYMM_CHARSET_AUTO ?
					cfg->charset : FYMM_CHARSET_UNICODE,
				cfg ? cfg->color : FYMM_COLOR_NONE, &theme);
	if (!cv)
		goto out;
	ascii = cv->charset == FYMM_CHARSET_ASCII;

	if (title)
		fymm_canvas_text(cv, 0, 0, title, FYMM_PAL_TITLE,
				 FYMM_ATTR_BOLD);

	/* the relations first, so a box sits on top of its connectors */
	for (i = 0; i < nrel; i++) {
		rel = fy_get_at(relations, i);
		from = cd_find(box, nclasses, fy_get(rel, "from", ""));
		to = cd_find(box, nclasses, fy_get(rel, "to", ""));
		if (from == (size_t)-1 || to == (size_t)-1 || from == to)
			continue;

		color = box[from].rank % 8;
		sx = box[from].x + box[from].w / 2;
		dx = box[to].x + box[to].w / 2;
		sy = box[from].y + box[from].h;
		dy = box[to].y - 1;
		if (dy < sy)
			dy = sy;

		ymid = sy + (dy - sy) / 2;
		fymm_canvas_route_v(cv, sx, sy, dx, dy, ymid, color,
				    !strcmp(fy_get(rel, "line", "solid"),
					    "dotted"));

		/* the decoration each end carries */
		g = cd_end_glyph(cv->charset, fy_get(rel, "fromEnd", "none"),
				 false);
		if (g)
			fymm_canvas_put(cv, sx, sy, g, color, 0);
		g = cd_end_glyph(cv->charset, fy_get(rel, "toEnd", "none"),
				 true);
		if (g)
			fymm_canvas_put(cv, dx, dy, g, color, 0);

		text = fy_get(rel, "label", (const char *)NULL);
		if (text && *text)
			fymm_canvas_text(cv, dx + 2, ymid, text,
					 FYMM_PAL_LABEL, 0);
	}

	/* then the boxes */
	for (i = 0; i < nclasses; i++) {
		x = box[i].x;
		y = box[i].y;
		w = box[i].w;
		color = box[i].rank % 8;

		for (j = 1; j + 1 < (size_t)w; j++) {
			fymm_canvas_put(cv, x + (int)j, y,
					ascii ? '-' : 0x2500, color, 0);
			fymm_canvas_put(cv, x + (int)j, y + box[i].h - 1,
					ascii ? '-' : 0x2500, color, 0);
		}
		fymm_canvas_put(cv, x, y, ascii ? '+' : 0x250c, color, 0);
		fymm_canvas_put(cv, x + w - 1, y, ascii ? '+' : 0x2510, color, 0);
		fymm_canvas_put(cv, x, y + box[i].h - 1, ascii ? '+' : 0x2514,
				color, 0);
		fymm_canvas_put(cv, x + w - 1, y + box[i].h - 1,
				ascii ? '+' : 0x2518, color, 0);
		for (r = 1; r < box[i].h - 1; r++) {
			fymm_canvas_put(cv, x, y + r, ascii ? '|' : 0x2502,
					color, 0);
			fymm_canvas_put(cv, x + w - 1, y + r,
					ascii ? '|' : 0x2502, color, 0);
		}

		/* the name, centred, with its generic parameter */
		snprintf(buf, sizeof(buf), "%s%s%s%s", box[i].name,
			 box[i].generic ? "<" : "", box[i].generic ?
			 box[i].generic : "", box[i].generic ? ">" : "");
		fymm_canvas_text(cv, x + (w - fymm_text_width(buf)) / 2, y + 1,
				 buf, color, FYMM_ATTR_BOLD);
		r = 2;
		if (box[i].annotation) {
			snprintf(buf, sizeof(buf), ascii ? "<<%s>>" : "«%s»",
				 box[i].annotation);
			fymm_canvas_text(cv,
					 x + (w - fymm_text_width(buf)) / 2,
					 y + r, buf, FYMM_PAL_LABEL,
					 FYMM_ATTR_DIM);
			r++;
		}

		if (fy_is_sequence(box[i].members) && fy_len(box[i].members)) {
			/* the rule between the name and the members */
			for (j = 1; j + 1 < (size_t)w; j++)
				fymm_canvas_put(cv, x + (int)j, y + r,
						ascii ? '-' : 0x2500, color, 0);
			fymm_canvas_put(cv, x, y + r, ascii ? '+' : 0x251c,
					color, 0);
			fymm_canvas_put(cv, x + w - 1, y + r,
					ascii ? '+' : 0x2524, color, 0);
			r++;
			fy_foreach(member, box[i].members) {
				fymm_canvas_text(cv, x + 2, y + r,
						 fy_str(member),
						 FYMM_COLOR_DEFAULT, 0);
				r++;
			}
		}
	}

	out = fymm_canvas_emit(cv);
	fymm_canvas_destroy(cv);

out:
	free(box);
	free(flat);
	return out;
}
