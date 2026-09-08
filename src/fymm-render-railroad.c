/*
 * fymm-render-railroad.c - drawing a grammar as rails
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
 * A railroad diagram is a track: the reader follows it left to right, and
 * every path from the entry to the exit spells a sentence the rule allows.
 * The drawing is therefore a recursion over the grammar tree, measured first
 * so that a node knows how much room its children need, then drawn into it.
 *
 * Every node reports a width, a height, and the row its rail enters and leaves
 * on. A sequence lays its children out side by side on one row; a choice
 * stacks them and joins the stack with rails at both ends; the repetitions add
 * a return path underneath and, where the count may be zero, a bypass above.
 */
struct rr_box {
	int w;
	int h;
	int row;	/* the rail row, relative to the box top */
};

struct rr_draw {
	struct fymm_canvas *cv;
	bool ascii;
};

static struct rr_box rr_measure(fy_generic node, int depth);

static int rr_label_width(fy_generic node)
{
	const char *text = fy_get(node, "text", (const char *)NULL);

	return (text ? fymm_text_width(text) : 1) + 4;
}

static struct rr_box rr_measure_row(fy_generic items, int depth, int gap)
{
	struct rr_box b = { 0, 1, 0 }, c;
	size_t n = fy_is_sequence(items) ? fy_len(items) : 0;
	size_t i;
	int above = 0, below = 1;

	for (i = 0; i < n; i++) {
		c = rr_measure(fy_get_at(items, i), depth + 1);
		b.w += c.w + (i ? gap : 0);
		if (c.row > above)
			above = c.row;
		if (c.h - c.row > below)
			below = c.h - c.row;
	}
	b.row = above;
	b.h = above + below;
	return b;
}

static struct rr_box rr_measure_stack(fy_generic items, int depth)
{
	struct rr_box b = { 0, 0, 0 }, c;
	size_t n = fy_is_sequence(items) ? fy_len(items) : 0;
	size_t i;

	for (i = 0; i < n; i++) {
		c = rr_measure(fy_get_at(items, i), depth + 1);
		if (c.w > b.w)
			b.w = c.w;
		if (!i)
			b.row = c.row;
		b.h += c.h + (i ? 1 : 0);
	}
	b.w += 6;	/* the joining rails on both sides */
	return b;
}

static struct rr_box rr_measure(fy_generic node, int depth)
{
	const char *kind = fy_get(node, "kind", "terminal");
	fy_generic items = fy_get(node, "items");
	struct rr_box b;

	/* a runaway grammar is a bug, not a drawing; stop descending */
	if (depth > 32) {
		b.w = 5;
		b.h = 1;
		b.row = 0;
		return b;
	}

	if (!strcmp(kind, "sequence"))
		return rr_measure_row(items, depth, 2);
	if (!strcmp(kind, "choice"))
		return rr_measure_stack(items, depth);
	if (!strcmp(kind, "optional") || !strcmp(kind, "zeroOrMore") ||
	    !strcmp(kind, "oneOrMore")) {
		bool bypass = strcmp(kind, "oneOrMore") != 0;
		bool loop = strcmp(kind, "optional") != 0;

		b = rr_measure_row(items, depth, 2);
		b.w += 6;
		if (bypass) {
			b.row += 2;
			b.h += 2;
		}
		if (loop)
			b.h += 2;
		return b;
	}
	if (!strcmp(kind, "and") || !strcmp(kind, "not")) {
		b = rr_measure_row(items, depth, 2);
		b.w += 2;
		return b;
	}

	b.w = rr_label_width(node);
	b.h = 1;
	b.row = 0;
	return b;
}

static void rr_draw(struct rr_draw *dr, fy_generic node, int x, int y,
		    int w, int depth);

/* the leaves: a terminal is rounded, a non-terminal boxed, a special angled */
static void rr_draw_leaf(struct rr_draw *dr, fy_generic node, int x, int y,
			 int w)
{
	const char *kind = fy_get(node, "kind", "terminal");
	const char *text = fy_get(node, "text", (const char *)NULL);
	uint32_t l, r;
	int color, tw;

	if (!text)
		text = ".";

	if (!strcmp(kind, "nonterminal")) {
		l = '[';
		r = ']';
		color = FYMM_PAL_BRANCH0 + 2;
	} else if (!strcmp(kind, "special")) {
		l = dr->ascii ? '<' : 0x27e8;
		r = dr->ascii ? '>' : 0x27e9;
		color = FYMM_PAL_TAG;
	} else if (!strcmp(kind, "any")) {
		l = '(';
		r = ')';
		color = FYMM_PAL_TAG;
	} else {
		l = '(';
		r = ')';
		color = FYMM_PAL_BRANCH0 + 1;
	}

	tw = fymm_text_width(text);
	fymm_canvas_put(dr->cv, x + 1, y, l, color, 0);
	fymm_canvas_text(dr->cv, x + 2, y, text, color,
			 !strcmp(kind, "terminal") ? FYMM_ATTR_BOLD : 0);
	fymm_canvas_put(dr->cv, x + 2 + tw, y, r, color, 0);

	/* the stubs that join the leaf to the track on either side */
	fymm_canvas_hline(dr->cv, y, x, x, FYMM_PAL_LABEL, false);
	if (w > tw + 4)
		fymm_canvas_hline(dr->cv, y, x + 3 + tw, x + w - 1,
				  FYMM_PAL_LABEL, false);
	else
		fymm_canvas_hline(dr->cv, y, x + 3 + tw, x + 3 + tw,
				  FYMM_PAL_LABEL, false);
}

static void rr_draw_row(struct rr_draw *dr, fy_generic items, int x, int y,
			int w, int depth, int gap)
{
	size_t n = fy_is_sequence(items) ? fy_len(items) : 0;
	size_t i;
	struct rr_box row = rr_measure_row(items, depth, gap);
	int cx = x;

	for (i = 0; i < n; i++) {
		fy_generic child = fy_get_at(items, i);
		struct rr_box c = rr_measure(child, depth + 1);

		if (i)
			fymm_canvas_hline(dr->cv, y + row.row, cx, cx + gap - 1,
					  FYMM_PAL_LABEL, false);
		cx += i ? gap : 0;
		rr_draw(dr, child, cx, y + row.row - c.row, c.w, depth + 1);
		cx += c.w;
	}
	if (cx < x + w)
		fymm_canvas_hline(dr->cv, y + row.row, cx, x + w - 1,
				  FYMM_PAL_LABEL, false);
}

/* a choice stacks its branches and rails them together at both ends */
static void rr_draw_choice(struct rr_draw *dr, fy_generic items, int x, int y,
			   int w, int depth)
{
	size_t n = fy_is_sequence(items) ? fy_len(items) : 0;
	size_t i;
	int inner = w - 6, cy = y, first_row = y, last_row = y;

	for (i = 0; i < n; i++) {
		fy_generic child = fy_get_at(items, i);
		struct rr_box c = rr_measure(child, depth + 1);
		int row = cy + c.row;

		rr_draw(dr, child, x + 3, cy, inner, depth + 1);
		if (!i)
			first_row = row;
		last_row = row;

		/* the branch stubs into the left and right rails */
		fymm_canvas_line(dr->cv, x + 2, row,
				 (uint8_t)(FYMM_LN_E | FYMM_LN_N |
					   (i + 1 < n ? FYMM_LN_S : 0)),
				 FYMM_PAL_LABEL, false);
		fymm_canvas_line(dr->cv, x + 3 + inner, row,
				 (uint8_t)(FYMM_LN_W | FYMM_LN_N |
					   (i + 1 < n ? FYMM_LN_S : 0)),
				 FYMM_PAL_LABEL, false);
		cy += c.h + 1;
	}

	if (last_row > first_row) {
		fymm_canvas_vline(dr->cv, x + 2, first_row, last_row,
				  FYMM_PAL_LABEL, false);
		fymm_canvas_vline(dr->cv, x + 3 + inner, first_row, last_row,
				  FYMM_PAL_LABEL, false);
	}
	fymm_canvas_hline(dr->cv, first_row, x, x + 2, FYMM_PAL_LABEL, false);
	fymm_canvas_hline(dr->cv, first_row, x + 3 + inner, x + w - 1,
			  FYMM_PAL_LABEL, false);
}

/*
 * A repetition is its body with a return path beneath it, and, when the body
 * may be skipped, a bypass above. Both are drawn as rails around the body, so
 * the entry row is the bypass when there is one.
 */
static void rr_draw_repeat(struct rr_draw *dr, fy_generic node, int x, int y,
			   int w, int depth)
{
	const char *kind = fy_get(node, "kind", "optional");
	fy_generic items = fy_get(node, "items");
	bool bypass = strcmp(kind, "oneOrMore") != 0;
	bool loop = strcmp(kind, "optional") != 0;
	struct rr_box body = rr_measure_row(items, depth, 2);
	int by = y + (bypass ? 2 : 0);
	int rail = by + body.row;
	int x0 = x + 3, x1 = x + w - 4;

	rr_draw_row(dr, items, x0, by, x1 - x0 + 1, depth, 2);

	fymm_canvas_hline(dr->cv, rail, x, x + 2, FYMM_PAL_LABEL, false);
	fymm_canvas_hline(dr->cv, rail, x1 + 1, x + w - 1, FYMM_PAL_LABEL,
			  false);

	if (bypass) {
		fymm_canvas_line(dr->cv, x + 1, y,
				 (uint8_t)(FYMM_LN_E | FYMM_LN_S),
				 FYMM_PAL_LABEL, false);
		fymm_canvas_hline(dr->cv, y, x + 1, x + w - 2, FYMM_PAL_LABEL,
				  false);
		fymm_canvas_line(dr->cv, x + w - 2, y,
				 (uint8_t)(FYMM_LN_W | FYMM_LN_S),
				 FYMM_PAL_LABEL, false);
		fymm_canvas_vline(dr->cv, x + 1, y, rail, FYMM_PAL_LABEL,
				  false);
		fymm_canvas_vline(dr->cv, x + w - 2, y, rail, FYMM_PAL_LABEL,
				  false);
	}

	if (loop) {
		int ly = by + body.h + 1;

		fymm_canvas_vline(dr->cv, x + 2, rail, ly, FYMM_PAL_LABEL,
				  false);
		fymm_canvas_vline(dr->cv, x + w - 3, rail, ly, FYMM_PAL_LABEL,
				  false);
		fymm_canvas_hline(dr->cv, ly, x + 2, x + w - 3, FYMM_PAL_LABEL,
				  false);
		fymm_canvas_put(dr->cv, (x + 2 + x + w - 3) / 2, ly,
				dr->ascii ? '<' : 0x25c0, FYMM_PAL_LABEL, 0);
	}
}

static void rr_draw(struct rr_draw *dr, fy_generic node, int x, int y,
		    int w, int depth)
{
	const char *kind = fy_get(node, "kind", "terminal");
	fy_generic items = fy_get(node, "items");

	if (depth > 32) {
		fymm_canvas_text(dr->cv, x, y, "...", FYMM_PAL_LABEL,
				 FYMM_ATTR_DIM);
		return;
	}

	if (!strcmp(kind, "sequence")) {
		rr_draw_row(dr, items, x, y, w, depth, 2);
	} else if (!strcmp(kind, "choice")) {
		rr_draw_choice(dr, items, x, y, w, depth);
	} else if (!strcmp(kind, "optional") || !strcmp(kind, "zeroOrMore") ||
		   !strcmp(kind, "oneOrMore")) {
		rr_draw_repeat(dr, node, x, y, w, depth);
	} else if (!strcmp(kind, "and") || !strcmp(kind, "not")) {
		/* the predicate sits on the rail, which the body then joins */
		fymm_canvas_put(dr->cv, x, y, !strcmp(kind, "and") ? '&' : '!',
				FYMM_PAL_TAG, FYMM_ATTR_BOLD);
		rr_draw_row(dr, items, x + 1, y, w - 1, depth, 2);
	} else {
		rr_draw_leaf(dr, node, x, y, w);
	}
}

struct fymm_canvas *fymm_render_railroad(const struct fymm_diagram *d,
					 fy_generic model,
					 const struct fymm_render_cfg *cfg)
{
	fy_generic rules, rule;
	struct fymm_canvas *cv = NULL;
	struct fymm_theme theme;
	struct rr_draw dr;
	struct rr_box *boxes = NULL;
	const char *title;
	size_t nrules, i;
	int width = 0, height, y, name_w = 0;

	if (fymm_theme_resolve(&theme, cfg, fymm_diagram_builder(d), model))
		return NULL;

	rules = fy_get(model, "rules");
	title = fy_get(model, "title", (const char *)NULL);
	nrules = fy_is_sequence(rules) ? fy_len(rules) : 0;
	if (!nrules) {
		/* a source with a title and no rule still says what it is */
		if (!title)
			return fymm_canvas_empty(cfg);
		cv = fymm_canvas_create_cfg(fymm_text_width(title), 1, cfg,
					    &theme);
		if (cv)
			fymm_canvas_text(cv, 0, 0, title, FYMM_PAL_TITLE,
					 FYMM_ATTR_BOLD);
		return cv;
	}

	boxes = malloc(nrules * sizeof(*boxes));
	if (!boxes)
		return NULL;

	height = title ? 2 : 0;
	for (i = 0; i < nrules; i++) {
		int w;

		rule = fy_get_at(rules, i);
		boxes[i] = rr_measure(fy_get(rule, "expr"), 0);
		w = fymm_text_width(fy_get(rule, "name", ""));
		if (w > name_w)
			name_w = w;
		height += boxes[i].h + 2;
	}

	for (i = 0; i < nrules; i++) {
		int w = name_w + 5 + boxes[i].w + 2;

		if (w > width)
			width = w;
	}

	cv = fymm_canvas_create_cfg(width, height, cfg, &theme);
	if (!cv) {
		free(boxes);
		return NULL;
	}
	dr.cv = cv;
	dr.ascii = cv->charset == FYMM_CHARSET_ASCII;

	if (title) {
		fymm_canvas_elem_begin(cv, FYMM_EL_TITLE, fy_invalid, "title");
		fymm_rich_text(cv, 0, 0, title, FYMM_PAL_TITLE,
			       FYMM_ATTR_BOLD);
		fymm_canvas_elem_end(cv);
	}

	y = title ? 2 : 0;
	for (i = 0; i < nrules; i++) {
		int rail;

		rule = fy_get_at(rules, i);
		rail = y + boxes[i].row;
		fymm_canvas_elem_begin(cv, FYMM_EL_NODE, rule, "rules/%zu", i);
		fymm_canvas_text(cv, 0, rail, fy_get(rule, "name", ""),
				 FYMM_PAL_TITLE, FYMM_ATTR_BOLD);
		fymm_canvas_put(cv, name_w + 1, rail,
				dr.ascii ? '|' : 0x2551, FYMM_PAL_LABEL, 0);
		fymm_canvas_hline(cv, rail, name_w + 2, name_w + 3,
				  FYMM_PAL_LABEL, false);
		rr_draw(&dr, fy_get(rule, "expr"), name_w + 4, y, boxes[i].w,
			0);
		fymm_canvas_put(cv, name_w + 4 + boxes[i].w, rail,
				dr.ascii ? '|' : 0x2551, FYMM_PAL_LABEL, 0);
		fymm_canvas_elem_end(cv);
		y += boxes[i].h + 2;
	}

	free(boxes);
	return cv;
}
