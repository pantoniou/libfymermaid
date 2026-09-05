/*
 * fymm-render-sequence.c - drawing a sequence diagram as lifelines and arrows
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

/* the gap kept either side of a participant label in its column */
#define SEQ_PAD 4

struct seq_geom {
	int *x;			/* the canvas column of each participant */
	size_t n;
	int width;
	int top;		/* the first lifeline row */
};

static size_t seq_index(fy_generic participants, const char *id)
{
	size_t i, n = fy_len(participants);

	for (i = 0; i < n; i++) {
		if (!strcmp(fy_get(fy_get_at(participants, i), "id", ""), id))
			return i;
	}
	return 0;
}

/* Centre @text on @x, clipped to the canvas. */
static void seq_centre(struct fymm_canvas *cv, int x, int y, const char *text,
		       int color, uint8_t attr)
{
	int w = fymm_rich_measure(text);

	x -= w / 2;
	fymm_rich_text(cv, x < 0 ? 0 : x, y, text, color, attr);
}

/*
 * Draw one message row. The label sits on the shaft when it fits between the
 * two lifelines, and on the row above when it does not, which keeps a long
 * message readable without widening every column to suit it.
 */
static int seq_draw_message(struct fymm_canvas *cv, const struct seq_geom *g,
			    fy_generic st, fy_generic participants, int y,
			    int number)
{
	const char *text, *head, *from_id, *to_id;
	size_t from, to;
	int x0, x1, mid, w, color, i;
	bool dotted, both, back;
	uint32_t tip;
	char buf[32];

	from_id = fy_get(st, "from", "");
	to_id = fy_get(st, "to", "");
	from = seq_index(participants, from_id);
	to = seq_index(participants, to_id);
	text = fy_get(st, "text", "");
	head = fy_get(st, "head", "arrow");
	dotted = !strcmp(fy_get(st, "line", "solid"), "dotted");
	both = fy_get(st, "bidirectional", false);
	color = (int)(from % 8);

	/* a message to itself loops out to the right and comes back */
	if (from == to) {
		x0 = g->x[from];
		if (number > 0) {
			snprintf(buf, sizeof(buf), "%d", number);
			fymm_canvas_text(cv, 0, y, buf, FYMM_PAL_LABEL,
					 FYMM_ATTR_DIM);
		}
		fymm_canvas_line(cv, x0, y, FYMM_LN_E | FYMM_LN_N | FYMM_LN_S,
				 color, dotted);
		fymm_canvas_hline(cv, y, x0 + 1, x0 + 3, color, dotted);
		fymm_canvas_line(cv, x0 + 3, y, FYMM_LN_W | FYMM_LN_S, color,
				 dotted);
		fymm_rich_text(cv, x0 + 5, y, text, FYMM_COLOR_DEFAULT, 0);
		y++;
		fymm_canvas_line(cv, x0 + 3, y, FYMM_LN_N | FYMM_LN_W, color,
				 dotted);
		fymm_canvas_hline(cv, y, x0 + 1, x0 + 2, color, dotted);
		fymm_canvas_put(cv, x0 + 1, y,
				cv->charset == FYMM_CHARSET_ASCII ? '<' :
					0x25c0, color, 0);
		return y + 1;
	}

	back = to < from;
	x0 = g->x[from < to ? from : to];
	x1 = g->x[from < to ? to : from];
	mid = (x0 + x1) / 2;
	w = fymm_rich_measure(text);

	/* the label goes above when the shaft cannot hold it */
	if (w && w + 4 > x1 - x0) {
		seq_centre(cv, mid, y, text, FYMM_COLOR_DEFAULT, 0);
		y++;
		w = 0;
	}

	if (number > 0) {
		snprintf(buf, sizeof(buf), "%d", number);
		fymm_canvas_text(cv, 0, y, buf, FYMM_PAL_LABEL, FYMM_ATTR_DIM);
	}

	fymm_canvas_hline(cv, y, x0 + 1, x1 - 1, color, dotted);
	if (w) {
		/* clear a gap in the shaft and set the label into it */
		for (i = mid - w / 2 - 1; i <= mid - w / 2 + w; i++)
			fymm_canvas_put(cv, i, y, ' ', FYMM_COLOR_DEFAULT, 0);
		seq_centre(cv, mid, y, text, FYMM_COLOR_DEFAULT, 0);
	}

	if (cv->charset == FYMM_CHARSET_ASCII)
		tip = !strcmp(head, "cross") ? 'x' :
		      !strcmp(head, "open") ? '>' :
		      !strcmp(head, "none") ? '-' : (back ? '<' : '>');
	else
		tip = !strcmp(head, "cross") ? 0x2717 :
		      !strcmp(head, "open") ? (back ? 0x27e8 : 0x27e9) :
		      !strcmp(head, "none") ? 0x2500 :
		      (back ? 0x25c0 : 0x25b6);

	if (strcmp(head, "none")) {
		fymm_canvas_put(cv, back ? x0 + 1 : x1 - 1, y, tip, color, 0);
		if (both)
			fymm_canvas_put(cv, back ? x1 - 1 : x0 + 1, y,
					cv->charset == FYMM_CHARSET_ASCII ?
						(back ? '>' : '<') :
						(back ? 0x25b6 : 0x25c0),
					color, 0);
	}
	return y + 1;
}

/* Where a note sits, given the participants it names. */
static int seq_note_x(const struct seq_geom *g, fy_generic participants,
		      fy_generic st, int w)
{
	const char *place = fy_get(st, "placement", "over");
	fy_generic actors = fy_get(st, "actors");
	int lo = g->width, hi = 0, x;
	size_t j, n;

	n = fy_is_sequence(actors) ? fy_len(actors) : 0;
	for (j = 0; j < n; j++) {
		int px = g->x[seq_index(participants, fy_get_at(actors, j, ""))];

		if (px < lo)
			lo = px;
		if (px > hi)
			hi = px;
	}
	if (!n)
		lo = hi = g->x[0];

	/* `over` straddles its actors; `left of` and `right of` sit beside
	 * the one they name */
	if (!strcmp(place, "right"))
		x = hi + 2;
	else if (!strcmp(place, "left"))
		x = lo - 2 - w;
	else
		x = (lo + hi) / 2 - w / 2;
	return x < 0 ? 0 : x;
}

char *fymm_render_sequence(const struct fymm_diagram *d, fy_generic model,
			   const struct fymm_render_cfg *cfg)
{
	fy_generic participants, statements, st, config;
	struct fymm_canvas *cv;
	struct fymm_theme theme;
	struct seq_geom g;
	const char *title, *kind, *text, *label, *block;
	size_t n, ns, i;
	int y, w, x, r, height, number = 0, indent;
	bool autonumber;
	char buf[128];
	char *out = NULL;

	if (fymm_theme_resolve(&theme, cfg, fymm_diagram_builder(d)))
		return NULL;

	memset(&g, 0, sizeof(g));
	participants = fy_get(model, "participants");
	statements = fy_get(model, "statements");
	config = fy_get(model, "config");
	title = fy_get(model, "title", (const char *)NULL);
	autonumber = fy_get(config, "autonumber", false);

	n = fy_is_sequence(participants) ? fy_len(participants) : 0;
	ns = fy_is_sequence(statements) ? fy_len(statements) : 0;
	if (!n)
		return strdup("");

	/* one column per participant, wide enough for its own label */
	g.n = n;
	g.x = calloc(n, sizeof(*g.x));
	if (!g.x)
		return NULL;

	x = 2;
	for (i = 0; i < n; i++) {
		w = fymm_text_width(fy_get(fy_get_at(participants, i),
					   "label", "")) + SEQ_PAD;
		g.x[i] = x + w / 2;
		x += w + 2;
	}
	g.width = x + 4;

	/*
	 * Measure the rows and the width in one pass. A message takes a row
	 * of clearance and a shaft, plus a row above when its label cannot
	 * fit between the two lifelines. A note beside the last participant
	 * reaches past the last column.
	 */
	height = (title ? 2 : 0) + 3;
	for (i = 0; i < ns; i++) {
		st = fy_get_at(statements, i);
		kind = fy_get(st, "kind", "");
		w = fymm_rich_measure(fy_get(st, "text", ""));

		if (!strcmp(kind, "message")) {
			height += 2 + (w ? 1 : 0);
			if (w + 6 > g.width)
				g.width = w + 6;
		} else if (!strcmp(kind, "note")) {
			height += 2;
			x = seq_note_x(&g, participants, st, w + 4) + w + 6;
			if (x > g.width)
				g.width = x;
		} else if (!strncmp(kind, "block", 5)) {
			height += 1;
		}
	}
	height += 2;

	cv = fymm_canvas_create(g.width, height,
				cfg && cfg->charset != FYMM_CHARSET_AUTO ?
					cfg->charset : FYMM_CHARSET_UNICODE,
				cfg ? cfg->color : FYMM_COLOR_NONE, &theme);
	if (!cv)
		goto out;

	y = 0;
	if (title) {
		fymm_rich_text(cv, 0, y, title, FYMM_PAL_TITLE,
				 FYMM_ATTR_BOLD);
		y += 2;
	}
	g.top = y;

	for (i = 0; i < n; i++) {
		label = fy_get(fy_get_at(participants, i), "label", "");
		seq_centre(cv, g.x[i], y, label, (int)(i % 8), FYMM_ATTR_BOLD);
	}
	y++;

	for (i = 0; i < ns; i++) {
		st = fy_get_at(statements, i);
		kind = fy_get(st, "kind", "");
		text = fy_get(st, "text", "");
		indent = (int)fy_get(st, "depth", 0LL);

		if (!strcmp(kind, "message")) {
			y++;
			if (autonumber)
				number++;
			y = seq_draw_message(cv, &g, st, participants, y,
					     autonumber ? number : 0);
		} else if (!strcmp(kind, "note")) {
			y++;
			w = fymm_rich_measure(text);
			x = seq_note_x(&g, participants, st, w + 4);
			fymm_canvas_text(cv, x, y, "[ ", FYMM_PAL_TAG, 0);
			fymm_rich_text(cv, x + 2, y, text, FYMM_PAL_TAG, 0);
			fymm_canvas_text(cv, x + 2 + w, y, " ]", FYMM_PAL_TAG,
					 0);
			y++;
		} else if (!strncmp(kind, "block", 5)) {
			block = fy_get(st, "block", "");
			if (!strcmp(kind, "block-end"))
				snprintf(buf, sizeof(buf), "%*send",
					 indent * 2, "");
			else
				snprintf(buf, sizeof(buf), "%*s%s%s%s",
					 indent * 2, "", block,
					 *text ? " " : "", text);
			fymm_canvas_text(cv, 0, y, buf, FYMM_PAL_LABEL,
					 FYMM_ATTR_DIM);
			y++;
		}
	}
	y++;

	/* the lifelines last, so that a message always sits on top of them */
	for (i = 0; i < n; i++) {
		for (r = g.top + 1; r < y; r++)
			fymm_canvas_line(cv, g.x[i], r, FYMM_LN_N | FYMM_LN_S,
					 FYMM_PAL_LABEL, false);
	}

	/* and the heads repeated at the foot, as mermaid does */
	for (i = 0; i < n; i++) {
		label = fy_get(fy_get_at(participants, i), "label", "");
		seq_centre(cv, g.x[i], y, label, (int)(i % 8), FYMM_ATTR_BOLD);
	}

	out = fymm_canvas_emit(cv);
	fymm_canvas_destroy(cv);

out:
	free(g.x);
	return out;
}
