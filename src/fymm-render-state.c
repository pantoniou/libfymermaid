/*
 * fymm-render-state.c - drawing a state diagram as ranked states
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
#include "fymm-layout.h"

#define ST_RETURN_LANES 3

/*
 * A start or an end is drawn as a mark rather than a box: it carries no text,
 * and a box around nothing reads as an empty state. A fork or a join is a
 * bar, which is what UML draws.
 */
static bool st_is_mark(const char *kind)
{
	return !strcmp(kind, "start") || !strcmp(kind, "end") ||
	       !strcmp(kind, "fork") || !strcmp(kind, "join");
}

char *fymm_render_state(const struct fymm_diagram *d, fy_generic model,
			const struct fymm_render_cfg *cfg)
{
	fy_generic states, transitions, st, tr;
	struct fymm_canvas *cv;
	struct fymm_theme theme;
	struct fymm_lnode *node = NULL;
	struct fymm_ledge *edge = NULL;
	struct fymm_layout lay;
	struct fymm_metrics met;
	struct fymm_layout_cfg lcfg = { 0, 0, 0, 0, 0, 0, FYMM_LAYOUT_DOWN };
	const char **kind = NULL, **text = NULL;
	const char *title, *label;
	size_t nstates, ntrans, i, j;
	int width, height, top, x, y, w, color, sx, sy, dx, dy, ymid;
	bool ascii;
	char *out = NULL;

	fymm_metrics_resolve(&met, fymm_diagram_type(d), cfg);

	if (fymm_theme_resolve(&theme, cfg, fymm_diagram_builder(d), model))
		return NULL;

	states = fy_get(model, "states");
	transitions = fy_get(model, "transitions");
	title = fy_get(model, "title", (const char *)NULL);
	nstates = fy_is_sequence(states) ? fy_len(states) : 0;
	ntrans = fy_is_sequence(transitions) ? fy_len(transitions) : 0;
	if (!nstates)
		return strdup("");

	node = calloc(nstates, sizeof(*node));
	kind = calloc(nstates, sizeof(*kind));
	text = calloc(nstates, sizeof(*text));
	edge = calloc(ntrans ? ntrans : 1, sizeof(*edge));
	if (!node || !kind || !text || !edge)
		goto out;

	for (i = 0; i < nstates; i++) {
		st = fy_get_at(states, i);
		node[i].id = fy_get(st, "id", "");
		kind[i] = fy_get(st, "kind", "normal");
		text[i] = fy_get(st, "label", (const char *)NULL);
		if (!text[i])
			text[i] = node[i].id;

		if (st_is_mark(kind[i])) {
			node[i].w = !strcmp(kind[i], "fork") ||
				    !strcmp(kind[i], "join") ? 9 : 3;
			node[i].h = 1;
		} else {
			node[i].w = fymm_rich_measure(text[i]) + 4;
			node[i].h = 3;
		}
	}

	for (i = 0, j = 0; i < ntrans; i++) {
		tr = fy_get_at(transitions, i);
		edge[j].from = fymm_layout_find(node, nstates,
						fy_get(tr, "from", ""));
		edge[j].to = fymm_layout_find(node, nstates,
					      fy_get(tr, "to", ""));
		if (edge[j].from == (size_t)-1 || edge[j].to == (size_t)-1)
			continue;
		j++;
	}
	ntrans = j;

	top = title ? 2 : 0;
	lcfg.top = top;
	lcfg.col_gap = met.col_gap;
	lcfg.rank_gap = met.rank_gap;
	lcfg.dir = FYMM_LAYOUT_DOWN;
	if (cfg && (cfg->fit == FYMM_FIT_SHRINK ||
		    cfg->fit == FYMM_FIT_LEGEND))
		lcfg.max_width = met.max_width;
	if (fymm_layout_layered(node, nstates, edge, ntrans, &lcfg, &lay))
		goto out;

	width = lay.width + 2 + ST_RETURN_LANES;
	height = top + lay.height + lay.rank_gap;
	if (title && fymm_rich_measure(title) + 2 > width)
		width = fymm_rich_measure(title) + 2;

	/* a transition label sits beside the arrowhead it belongs to */
	for (i = 0; i < ntrans; i++) {
		label = fy_get(fy_get_at(transitions, i), "label",
			       (const char *)NULL);
		if (!label || !*label)
			continue;
		w = node[edge[i].to].x + node[edge[i].to].w / 2 + 3 +
		    fymm_rich_measure(label);
		if (w > width)
			width = w;
	}

	cv = fymm_canvas_create_cfg(width, height, cfg, &theme);
	if (!cv)
		goto out;
	ascii = cv->charset == FYMM_CHARSET_ASCII;

	if (title)
		fymm_rich_text(cv, 0, 0, title, FYMM_PAL_TITLE,
				 FYMM_ATTR_BOLD);

	/* the transitions first, so a state sits on top of its arrows */
	for (i = 0; i < ntrans; i++) {
		size_t from = edge[i].from, to = edge[i].to;

		color = node[from].rank % 8;
		sx = node[from].x + node[from].w / 2;
		dx = node[to].x + node[to].w / 2;
		sy = node[from].y + node[from].h;
		dy = node[to].y - 1;

		if (edge[i].back) {
			int lane = width - 2 - (int)(i % ST_RETURN_LANES);

			fymm_canvas_line(cv, sx, sy, FYMM_LN_N | FYMM_LN_E,
					 color, false);
			fymm_canvas_hline(cv, sy, sx + 1, lane - 1, color,
					  false);
			fymm_canvas_line(cv, lane, sy, FYMM_LN_W | FYMM_LN_N,
					 color, false);
			fymm_canvas_vline(cv, lane, dy, sy - 1, color, false);
			fymm_canvas_line(cv, lane, dy, FYMM_LN_S | FYMM_LN_W,
					 color, false);
			fymm_canvas_hline(cv, dy, dx + 1, lane - 1, color,
					  false);
			fymm_canvas_line(cv, dx, dy, FYMM_LN_E | FYMM_LN_S,
					 color, false);
		} else {
			ymid = sy + (dy - sy) / 2;
			fymm_canvas_route_v(cv, sx, sy, dx, dy, ymid, color,
					    false);
		}
		fymm_canvas_put(cv, dx, dy, ascii ? 'v' : 0x25bc, color, 0);

		label = fy_get(fy_get_at(transitions, i), "label",
			       (const char *)NULL);
		if (label && *label)
			fymm_canvas_text(cv, dx + 2, dy, label, FYMM_PAL_LABEL,
					 0);
	}

	/* then the states */
	for (i = 0; i < nstates; i++) {
		x = node[i].x;
		y = node[i].y;
		w = node[i].w;
		color = node[i].rank % 8;

		if (!strcmp(kind[i], "start")) {
			fymm_canvas_text(cv, x, y, ascii ? "(o)" : "◉◉◉",
					 color, FYMM_ATTR_BOLD);
			continue;
		}
		if (!strcmp(kind[i], "end")) {
			fymm_canvas_text(cv, x, y, ascii ? "(*)" : "◎◎◎",
					 color, FYMM_ATTR_BOLD);
			continue;
		}
		if (!strcmp(kind[i], "fork") || !strcmp(kind[i], "join")) {
			for (j = 0; j < (size_t)w; j++)
				fymm_canvas_put(cv, x + (int)j, y,
						ascii ? '=' : 0x2501, color,
						FYMM_ATTR_BOLD);
			continue;
		}

		/* a choice is a decision, so it gets the double border the
		 * flowchart gives one */
		{
			bool choice = !strcmp(kind[i], "choice");
			uint32_t h = ascii ? '-' : (choice ? 0x2550 : 0x2500);
			uint32_t v = ascii ? '|' : (choice ? 0x2551 : 0x2502);
			uint32_t tl = ascii ? '+' : (choice ? 0x2554 : 0x256d);
			uint32_t trc = ascii ? '+' : (choice ? 0x2557 : 0x256e);
			uint32_t bl = ascii ? '+' : (choice ? 0x255a : 0x2570);
			uint32_t br = ascii ? '+' : (choice ? 0x255d : 0x256f);

			for (j = 1; j + 1 < (size_t)w; j++) {
				fymm_canvas_put(cv, x + (int)j, y, h, color, 0);
				fymm_canvas_put(cv, x + (int)j, y + 2, h, color,
						0);
			}
			fymm_canvas_put(cv, x, y, tl, color, 0);
			fymm_canvas_put(cv, x + w - 1, y, trc, color, 0);
			fymm_canvas_put(cv, x, y + 2, bl, color, 0);
			fymm_canvas_put(cv, x + w - 1, y + 2, br, color, 0);
			fymm_canvas_put(cv, x, y + 1, v, color, 0);
			fymm_canvas_put(cv, x + w - 1, y + 1, v, color, 0);
			fymm_canvas_text(cv, x + 2, y + 1, text[i],
					 FYMM_COLOR_DEFAULT, 0);
		}
	}

	out = fymm_canvas_emit(cv);
	fymm_canvas_destroy(cv);

out:
	free(node);
	free(edge);
	free(kind);
	free(text);
	return out;
}
