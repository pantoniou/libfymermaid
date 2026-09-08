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
#include "fymm-frame.h"
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

/* How deep @id sits: the number of composites enclosing it. */
static int st_depth(fy_generic states, size_t nstates, const char *id)
{
	const char *p = NULL;
	size_t i;
	int depth = 0;

	while (depth < (int)nstates) {
		for (i = 0; i < nstates; i++) {
			if (strcmp(fy_get(fy_get_at(states, i), "id", ""), id))
				continue;
			p = fy_get(fy_get_at(states, i), "parent",
				   (const char *)NULL);
			break;
		}
		if (i == nstates || !p)
			break;
		id = p;
		p = NULL;
		depth++;
	}
	return depth;
}

/* The frame @id names, or (size_t)-1 when it names no composite. */
static size_t st_frame_of(const struct fymm_frame *fr, size_t nfr,
			  const char *id)
{
	size_t g;

	if (!id)
		return (size_t)-1;
	for (g = 0; g < nfr; g++) {
		if (!strcmp(fr[g].id, id))
			return g;
	}
	return (size_t)-1;
}

/*
 * The node a transition endpoint means. A transition naming a composite is
 * entering or leaving it, and what that means is the state inside: its start
 * mark when it is entered, its end mark when it is left, and failing either
 * the first or the last state it holds.
 */
static size_t st_endpoint(const struct fymm_lnode *node, size_t nnodes,
			  fy_generic states, size_t nstates,
			  const struct fymm_frame *fr, size_t nfr,
			  const char *id, bool entering)
{
	size_t g, i, found = (size_t)-1;

	g = st_frame_of(fr, nfr, id);
	if (g == (size_t)-1)
		return fymm_layout_find(node, nnodes, id);

	for (i = 0; i < nstates; i++) {
		fy_generic st = fy_get_at(states, i);
		const char *parent = fy_get(st, "parent", (const char *)NULL);
		const char *kind = fy_get(st, "kind", "normal");
		size_t n;

		if (!parent || strcmp(parent, id))
			continue;
		n = fymm_layout_find(node, nnodes, fy_get(st, "id", ""));
		if (n == (size_t)-1)
			continue;
		if (!strcmp(kind, entering ? "start" : "end"))
			return n;
		if (found == (size_t)-1 || !entering)
			found = n;
	}
	return found;
}

struct fymm_canvas *fymm_render_state(const struct fymm_diagram *d,
				      fy_generic model,
				      const struct fymm_render_cfg *cfg)
{
	fy_generic states, transitions, st, tr;
	struct fymm_canvas *cv = NULL;
	struct fymm_theme theme;
	struct fymm_lnode *node = NULL;
	struct fymm_ledge *edge = NULL;
	struct fymm_layout lay;
	struct fymm_metrics met;
	struct fymm_layout_cfg lcfg = { 0, 0, 0, 0, 0, 0, FYMM_LAYOUT_DOWN };
	struct fymm_frame *frame = NULL;
	struct fymm_frame_node *fnode = NULL;
	struct fymm_rich **ftitle = NULL;
	size_t *order = NULL;
	const char **kind = NULL, **text = NULL;
	const char *title, *label;
	size_t nstates, ntrans, nframes = 0, i, j;
	int pad = 0, maxdepth = 0;
	int width, height, top, x, y, w, color, sx, sy, dx, dy, ymid;
	size_t nnodes;
	bool ascii;

	fymm_metrics_resolve(&met, fymm_diagram_type(d), cfg);

	if (fymm_theme_resolve(&theme, cfg, fymm_diagram_builder(d), model))
		return NULL;

	states = fy_get(model, "states");
	transitions = fy_get(model, "transitions");
	title = fy_get(model, "title", (const char *)NULL);
	nstates = fy_is_sequence(states) ? fy_len(states) : 0;
	ntrans = fy_is_sequence(transitions) ? fy_len(transitions) : 0;
	if (!nstates)
		return fymm_canvas_empty(cfg);

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

	/*
	 * A composite state is what its children are drawn inside, not a box
	 * of its own: it becomes a frame, and its label the frame's title.
	 * The states keep their order, so a composite comes before the states
	 * it holds and the frames nest the way the shared code expects.
	 */
	frame = calloc(nstates, sizeof(*frame));
	ftitle = calloc(nstates, sizeof(*ftitle));
	fnode = calloc(nstates, sizeof(*fnode));
	order = calloc(nstates, sizeof(*order));
	if (!frame || !ftitle || !fnode || !order)
		goto out;

	for (i = 0; i < nstates; i++) {
		const char *id = fy_get(fy_get_at(states, i), "id", "");
		bool composite = false;

		for (j = 0; j < nstates && !composite; j++) {
			const char *p = fy_get(fy_get_at(states, j), "parent",
					       (const char *)NULL);

			composite = p && !strcmp(p, id);
		}
		if (!composite)
			continue;

		frame[nframes].id = id;
		ftitle[nframes] = fymm_rich_parse(text[i], false);
		if (!ftitle[nframes])
			goto out;
		frame[nframes].tw = fymm_rich_width(ftitle[nframes]);
		frame[nframes].depth = st_depth(states, nstates, id);
		frame[nframes].framed = true;
		if (frame[nframes].depth > maxdepth)
			maxdepth = frame[nframes].depth;
		nframes++;
	}

	/*
	 * Drop the composites from the nodes; a transition that named one is
	 * carried to the state inside it that the transition means, which is
	 * where entering or leaving the composite actually goes.
	 */
	for (i = 0, nnodes = 0; i < nstates; i++) {
		st = fy_get_at(states, i);
		if (st_frame_of(frame, nframes, fy_get(st, "id", "")) !=
		    (size_t)-1)
			continue;
		node[nnodes] = node[i];
		kind[nnodes] = kind[i];
		text[nnodes] = text[i];
		fnode[nnodes].group =
			st_frame_of(frame, nframes,
				    fy_get(st, "parent", (const char *)NULL));
		nnodes++;
	}


	fymm_frames_mark(frame, nframes, fnode, nnodes);
	fymm_frames_order(frame, nframes, order, fnode, nnodes);
	for (i = 1; i < nnodes; i++) {
		struct fymm_lnode tn = node[i];
		struct fymm_frame_node tf = fnode[i];
		const char *tk = kind[i], *tt = text[i];
		size_t key = order[i];

		for (j = i; j && order[j - 1] > key; j--) {
			node[j] = node[j - 1];
			fnode[j] = fnode[j - 1];
			kind[j] = kind[j - 1];
			text[j] = text[j - 1];
			order[j] = order[j - 1];
		}
		node[j] = tn;
		fnode[j] = tf;
		kind[j] = tk;
		text[j] = tt;
		order[j] = key;
	}

	/* the endpoints were resolved before the sort moved the nodes */
	for (i = 0, j = 0; i < fy_len(transitions); i++) {
		tr = fy_get_at(transitions, i);
		edge[j].from = st_endpoint(node, nnodes, states, nstates,
					   frame, nframes,
					   fy_get(tr, "from", ""), false);
		edge[j].to = st_endpoint(node, nnodes, states, nstates,
					 frame, nframes,
					 fy_get(tr, "to", ""), true);
		if (edge[j].from == (size_t)-1 || edge[j].to == (size_t)-1)
			continue;
		j++;
	}
	ntrans = j;

	pad = nframes ? 2 * (maxdepth + 1) * FYMM_FRAME_PAD : 0;

	top = title ? 2 : 0;
	lcfg.top = top;
	lcfg.col_gap = met.col_gap + pad;
	lcfg.rank_gap = met.rank_gap + pad;
	lcfg.dir = FYMM_LAYOUT_DOWN;
	if (cfg && (cfg->fit == FYMM_FIT_SHRINK ||
		    cfg->fit == FYMM_FIT_LEGEND))
		lcfg.max_width = met.max_width;
	if (fymm_layout_layered(node, nnodes, edge, ntrans, &lcfg, &lay))
		goto out;

	for (i = 0; i < nnodes; i++) {
		node[i].x += pad;
		node[i].y += pad;
		fnode[i].x = node[i].x;
		fnode[i].y = node[i].y;
		fnode[i].w = node[i].w;
		fnode[i].h = node[i].h;
		fnode[i].rank = node[i].rank;
	}
	for (i = 0; i < 8 && nframes; i++) {
		if (!fymm_frames_measure(frame, nframes, fnode, nnodes,
					 lay.col_gap))
			break;
	}
	for (i = 0; i < nnodes; i++)
		node[i].x = fnode[i].x;

	width = lay.width + 2 + ST_RETURN_LANES + 2 * pad;
	height = top + lay.height + lay.rank_gap + 2 * pad;
	for (i = 0; i < nnodes; i++) {
		if (node[i].x + node[i].w > width - 2 - ST_RETURN_LANES)
			width = node[i].x + node[i].w + 2 + ST_RETURN_LANES;
	}
	for (i = 0; i < nframes; i++) {
		if (!frame[i].used)
			continue;
		if (frame[i].x1 + 2 + ST_RETURN_LANES > width)
			width = frame[i].x1 + 2 + ST_RETURN_LANES;
		if (frame[i].y1 + 2 > height)
			height = frame[i].y1 + 2;
	}
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

	if (title) {
		fymm_canvas_elem_begin(cv, FYMM_EL_TITLE, fy_invalid, "title");
		fymm_rich_text(cv, 0, 0, title, FYMM_PAL_TITLE,
				 FYMM_ATTR_BOLD);
		fymm_canvas_elem_end(cv);
	}

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
		if (label && *label) {
			int room = fymm_frames_room(frame, nframes, dx + 2,
						    dy);

			/* a label is a literal glyph and a frame is a line,
			 * so a label left to run would take the cell and
			 * leave a hole in the frame */
			if (room < 0)
				fymm_canvas_text(cv, dx + 2, dy, label,
						 FYMM_PAL_LABEL, 0);
			else if (room >= 2)
				fymm_rich_text_max(cv, dx + 2, dy, label,
						   room, FYMM_PAL_LABEL, 0);
		}
	}

	/* the frames, over the links and under the states they hold */
	fymm_frames_draw(cv, frame, nframes, ftitle);

	/* then the states */
	for (i = 0; i < nnodes; i++) {
		x = node[i].x;
		y = node[i].y;
		w = node[i].w;
		color = node[i].rank % 8;

		fymm_canvas_elem_begin(cv, FYMM_EL_NODE, fy_get_at(states, i),
				       "states/%zu", i);

		/*
		 * One mark, in the middle of the three cells the layout gave
		 * it. Three of the same glyph side by side read as three
		 * pseudo-states rather than as one. The other two cells are
		 * left alone: writing a blank into them would be content, and
		 * emission trims only cells nothing was drawn in.
		 */
		if (!strcmp(kind[i], "start") || !strcmp(kind[i], "end")) {
			bool start = !strcmp(kind[i], "start");

			fymm_canvas_put(cv, x + 1, y,
					ascii ? (start ? 'o' : '*') :
						(start ? 0x25cf : 0x25c9),
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

			/* clear the row the label sits on: a link that ran
			 * through this cell before the box was drawn would
			 * otherwise show between the border and the text */
			for (j = 1; j + 1 < (size_t)w; j++)
				fymm_canvas_put(cv, x + (int)j, y + 1, ' ',
						FYMM_COLOR_DEFAULT, 0);
			fymm_canvas_text(cv, x + 2, y + 1, text[i],
					 FYMM_COLOR_DEFAULT, 0);
		}
		fymm_canvas_elem_end(cv);
	}

out:
	for (i = 0; i < nstates && ftitle; i++)
		fymm_rich_destroy(ftitle[i]);
	free(ftitle);
	free(frame);
	free(fnode);
	free(order);
	free(node);
	free(edge);
	free(kind);
	free(text);
	return cv;
}
