/*
 * fymm-render-flowchart.c - drawing a flowchart as a layered graph
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

/* the rows a node box occupies, and the rows left between two ranks */
#define FC_BOX_ROWS 3
#define FC_RANK_GAP 2
#define FC_COL_GAP 2

/* how many columns of the right margin are kept for returning links */
#define FC_RETURN_LANES 3

/*
 * The border a shape is drawn with. A terminal cannot show a stadium apart
 * from a circle at this size, so the shapes are gathered into three families
 * that a reader can actually tell apart: a plain box, a rounded box, and a
 * double box for the decisions, which are the ones worth spotting.
 */
enum fc_border {
	FC_BORDER_SHARP = 0,
	FC_BORDER_ROUND,
	FC_BORDER_DOUBLE,
};

static enum fc_border fc_border_of(const char *shape)
{
	static const char *const round_shapes[] = {
		"round", "rounded", "stadium", "circle", "doublecircle",
		"pill", "terminal", "start", "stop", "event",
	};
	static const char *const double_shapes[] = {
		"rhombus", "diamond", "decision", "question", "hexagon",
		"hex", "prepare",
	};
	size_t i;

	for (i = 0; i < sizeof(double_shapes) / sizeof(double_shapes[0]); i++) {
		if (!strcmp(shape, double_shapes[i]))
			return FC_BORDER_DOUBLE;
	}
	for (i = 0; i < sizeof(round_shapes) / sizeof(round_shapes[0]); i++) {
		if (!strcmp(shape, round_shapes[i]))
			return FC_BORDER_ROUND;
	}
	return FC_BORDER_SHARP;
}

/* struct fc_pair - one edge, resolved to node indices */
struct fc_pair {
	size_t from, to;
};

/* struct fc_box - one node, placed */
struct fc_box {
	const char *id;
	const char *text;
	enum fc_border border;
	int rank;
	int order;		/* position within the rank */
	int x, y, w;		/* the top left cell and the width */
};

struct fc_layout {
	struct fc_box *box;
	size_t n;
	int *rank_width;	/* the cells each rank occupies */
	int *rank_y;
	int nranks;
	int width, height;
};

static size_t fc_find(const struct fc_layout *l, const char *id)
{
	size_t i;

	for (i = 0; i < l->n; i++) {
		if (!strcmp(l->box[i].id, id))
			return i;
	}
	return (size_t)-1;
}

/*
 * Mark the edges that close a cycle. A depth-first walk meets a back edge
 * when it reaches a node already on its own stack; ranking such an edge would
 * push its target below itself for ever, so the layering ignores them and the
 * renderer draws them as returns.
 *
 * @state is 0 for unvisited, 1 for on the stack and 2 for done.
 */
static void fc_mark_back(struct fc_layout *l, const struct fc_pair *pair,
			 size_t npairs, uint8_t *state, bool *back, size_t v)
{
	size_t i;

	state[v] = 1;
	for (i = 0; i < npairs; i++) {
		if (pair[i].from != v || back[i])
			continue;
		if (state[pair[i].to] == 1)
			back[i] = true;
		else if (!state[pair[i].to])
			fc_mark_back(l, pair, npairs, state, back, pair[i].to);
	}
	state[v] = 2;
}

/*
 * Longest-path layering over the edges that are not back edges: a node sits
 * one rank below its deepest predecessor.
 */
static void fc_rank(struct fc_layout *l, const struct fc_pair *pair,
		    size_t npairs, const bool *back)
{
	size_t i, passes;
	bool moved = true;

	for (passes = 0; moved && passes <= l->n; passes++) {
		moved = false;
		for (i = 0; i < npairs; i++) {
			if (back[i] || pair[i].from == pair[i].to)
				continue;
			if (l->box[pair[i].to].rank <=
			    l->box[pair[i].from].rank) {
				l->box[pair[i].to].rank =
					l->box[pair[i].from].rank + 1;
				moved = true;
			}
		}
	}
}

/*
 * A vertical run whose every cell reaches both ways, so that a crossing with
 * a horizontal run resolves to a junction on its own.
 */
static void fc_run_v(struct fymm_canvas *cv, int x, int y0, int y1, int color)
{
	int y;

	for (y = y0; y <= y1; y++)
		fymm_canvas_line(cv, x, y, FYMM_LN_N | FYMM_LN_S, color, false);
}

static void fc_run_h(struct fymm_canvas *cv, int y, int x0, int x1, int color)
{
	int x;

	for (x = x0; x <= x1; x++)
		fymm_canvas_line(cv, x, y, FYMM_LN_W | FYMM_LN_E, color, false);
}

/*
 * Route a link that leaves the bottom of a box at (@sx, @sy) and enters the
 * top of another at (@dx, @dy), turning on row @ymid. The corners carry their
 * own masks; a single cell run would otherwise reach nowhere and draw
 * nothing.
 */
static void fc_route(struct fymm_canvas *cv, int sx, int sy, int dx, int dy,
		     int ymid, int color)
{
	if (sx == dx) {
		fc_run_v(cv, sx, sy, dy, color);
		return;
	}

	fc_run_v(cv, sx, sy, ymid - 1, color);
	fymm_canvas_line(cv, sx, ymid,
			 (uint8_t)(FYMM_LN_N | (dx > sx ? FYMM_LN_E :
						FYMM_LN_W)), color, false);
	if (dx > sx)
		fc_run_h(cv, ymid, sx + 1, dx - 1, color);
	else
		fc_run_h(cv, ymid, dx + 1, sx - 1, color);
	fymm_canvas_line(cv, dx, ymid,
			 (uint8_t)(FYMM_LN_S | (dx > sx ? FYMM_LN_W :
						FYMM_LN_E)), color, false);
	fc_run_v(cv, dx, ymid + 1, dy, color);
}

char *fymm_render_flowchart(const struct fymm_diagram *d, fy_generic model,
			    const struct fymm_render_cfg *cfg)
{
	static const uint32_t corners[3][6] = {
		/* top-left, top-right, bottom-left, bottom-right, h, v */
		{ 0x250c, 0x2510, 0x2514, 0x2518, 0x2500, 0x2502 },
		{ 0x256d, 0x256e, 0x2570, 0x256f, 0x2500, 0x2502 },
		{ 0x2554, 0x2557, 0x255a, 0x255d, 0x2550, 0x2551 },
	};
	fy_generic nodes, edges, node, edge;
	struct fymm_canvas *cv;
	struct fymm_theme theme;
	struct fc_layout l;
	struct fc_pair *pair = NULL;
	uint8_t *state = NULL;
	bool *back = NULL;
	const char *title, *text;
	size_t nnodes, nedges, i, j, from, to;
	int r, x, y, w, top, maxw, color, sx, sy, dx, dy, ymid;
	bool ascii;
	char *out = NULL;

	if (fymm_theme_resolve(&theme, cfg, fymm_diagram_builder(d)))
		return NULL;

	memset(&l, 0, sizeof(l));
	nodes = fy_get(model, "nodes");
	edges = fy_get(model, "edges");
	title = fy_get(model, "title", (const char *)NULL);

	nnodes = fy_is_sequence(nodes) ? fy_len(nodes) : 0;
	nedges = fy_is_sequence(edges) ? fy_len(edges) : 0;
	if (!nnodes)
		return strdup("");

	l.n = nnodes;
	l.box = calloc(nnodes, sizeof(*l.box));
	if (!l.box)
		return NULL;

	for (i = 0; i < nnodes; i++) {
		node = fy_get_at(nodes, i);
		l.box[i].id = fy_get(node, "id", "");
		l.box[i].text = fy_get(node, "text", "");
		l.box[i].border = fc_border_of(fy_get(node, "shape", "rect"));
		l.box[i].w = fymm_text_width(l.box[i].text) + 4;
	}

	/* resolve the edges once, then find the ones that close a cycle */
	pair = calloc(nedges ? nedges : 1, sizeof(*pair));
	back = calloc(nedges ? nedges : 1, sizeof(*back));
	state = calloc(nnodes, sizeof(*state));
	if (!pair || !back || !state)
		goto out;
	for (i = 0; i < nedges; i++) {
		edge = fy_get_at(edges, i);
		pair[i].from = fc_find(&l, fy_get(edge, "from", ""));
		pair[i].to = fc_find(&l, fy_get(edge, "to", ""));
	}
	for (i = 0; i < nedges; i++) {
		if (pair[i].from == (size_t)-1 || pair[i].to == (size_t)-1)
			back[i] = true;		/* nothing to rank */
	}
	for (i = 0; i < nnodes; i++) {
		if (!state[i])
			fc_mark_back(&l, pair, nedges, state, back, i);
	}

	fc_rank(&l, pair, nedges, back);

	for (i = 0; i < nnodes; i++) {
		if (l.box[i].rank + 1 > l.nranks)
			l.nranks = l.box[i].rank + 1;
	}
	l.rank_width = calloc((size_t)l.nranks, sizeof(*l.rank_width));
	l.rank_y = calloc((size_t)l.nranks, sizeof(*l.rank_y));
	if (!l.rank_width || !l.rank_y)
		goto out;

	/* place each rank as a row of boxes, left to right in the order the
	 * nodes were written */
	for (r = 0; r < l.nranks; r++) {
		x = 0;
		for (i = 0; i < nnodes; i++) {
			if (l.box[i].rank != r)
				continue;
			l.box[i].order = l.rank_width[r];
			l.box[i].x = x;
			x += l.box[i].w + FC_COL_GAP;
			l.rank_width[r]++;
		}
		if (x - FC_COL_GAP > l.width)
			l.width = x - FC_COL_GAP;
	}

	/* centre the narrower ranks under the widest one */
	for (r = 0; r < l.nranks; r++) {
		int used = 0;

		for (i = 0; i < nnodes; i++) {
			if (l.box[i].rank == r)
				used = l.box[i].x + l.box[i].w;
		}
		for (i = 0; i < nnodes; i++) {
			if (l.box[i].rank == r)
				l.box[i].x += (l.width - used) / 2;
		}
	}

	top = title ? 2 : 0;
	for (r = 0; r < l.nranks; r++)
		l.rank_y[r] = top + r * (FC_BOX_ROWS + FC_RANK_GAP);
	for (i = 0; i < nnodes; i++)
		l.box[i].y = l.rank_y[l.box[i].rank];

	l.height = top + l.nranks * (FC_BOX_ROWS + FC_RANK_GAP) - FC_RANK_GAP;
	/* a returning link leaves the bottom of its source, so the last rank
	 * needs a row beneath it to turn in */
	for (i = 0; i < nedges; i++) {
		if (back[i] && pair[i].from != (size_t)-1) {
			l.height += FC_RANK_GAP;
			break;
		}
	}
	/* an edge label sits to the right of the arrowhead it belongs to */
	for (i = 0; i < nedges; i++) {
		text = fy_get(fy_get_at(edges, i), "text", (const char *)NULL);
		to = pair[i].to;
		if (!text || !*text || to == (size_t)-1)
			continue;
		w = l.box[to].x + l.box[to].w / 2 + 3 + fymm_text_width(text);
		if (w > l.width)
			l.width = w;
	}
	l.width += 2 + FC_RETURN_LANES;
	if (title && fymm_text_width(title) + 2 > l.width)
		l.width = fymm_text_width(title) + 2;

	cv = fymm_canvas_create(l.width, l.height,
				cfg && cfg->charset != FYMM_CHARSET_AUTO ?
					cfg->charset : FYMM_CHARSET_UNICODE,
				cfg ? cfg->color : FYMM_COLOR_NONE, &theme);
	if (!cv)
		goto out;
	ascii = cv->charset == FYMM_CHARSET_ASCII;

	if (title)
		fymm_canvas_text(cv, 0, 0, title, FYMM_PAL_TITLE,
				 FYMM_ATTR_BOLD);

	/* the edges first, so that a box always sits on top of its links */
	for (i = 0; i < nedges; i++) {
		edge = fy_get_at(edges, i);
		from = fc_find(&l, fy_get(edge, "from", ""));
		to = fc_find(&l, fy_get(edge, "to", ""));
		if (from == (size_t)-1 || to == (size_t)-1)
			continue;

		color = l.box[from].rank % 8;
		sx = l.box[from].x + l.box[from].w / 2;
		dx = l.box[to].x + l.box[to].w / 2;
		sy = l.box[from].y + FC_BOX_ROWS - 1;
		dy = l.box[to].y;

		/*
		 * A link back to an earlier rank cannot run straight up
		 * through the ranks between; it leaves the bottom, runs out
		 * to a lane in the right margin, climbs, and comes back in.
		 */
		if (back[i]) {
			int lane = l.width - 2 - (int)(i % FC_RETURN_LANES);

			fymm_canvas_line(cv, sx, sy + 1,
					 FYMM_LN_N | FYMM_LN_E, color, false);
			fc_run_h(cv, sy + 1, sx + 1, lane - 1, color);
			fymm_canvas_line(cv, lane, sy + 1,
					 FYMM_LN_W | FYMM_LN_N, color, false);
			fc_run_v(cv, lane, dy, sy, color);
			fymm_canvas_line(cv, lane, dy - 1,
					 FYMM_LN_S | FYMM_LN_W, color, false);
			fc_run_h(cv, dy - 1, dx + 1, lane - 1, color);
			fymm_canvas_line(cv, dx, dy - 1,
					 FYMM_LN_E | FYMM_LN_S, color, false);
		} else {
			ymid = sy + 1 + (dy - sy - 2) / 2;
			fc_route(cv, sx, sy + 1, dx, dy - 1, ymid, color);
		}

		/* the arrowhead sits on the row above the box it enters */
		if (strcmp(fy_get(edge, "head", "none"), "none"))
			fymm_canvas_put(cv, dx, dy - 1,
					!strcmp(fy_get(edge, "head", ""),
						"cross") ? (ascii ? 'x' : 0x2717) :
					!strcmp(fy_get(edge, "head", ""),
						"circle") ? (ascii ? 'o' : 0x25cb) :
					(ascii ? 'v' : 0x25bc), color, 0);

		/*
		 * The label goes beside the arrowhead rather than along the
		 * shaft. Centring it on the run reads well until two links
		 * share a row, and then it lands on another link's line.
		 */
		text = fy_get(edge, "text", (const char *)NULL);
		if (text && *text)
			fymm_canvas_text(cv, dx + 2, dy - 1, text,
					 FYMM_PAL_LABEL, 0);
	}

	/* then the boxes */
	for (i = 0; i < nnodes; i++) {
		const uint32_t *c = corners[l.box[i].border];

		x = l.box[i].x;
		y = l.box[i].y;
		w = l.box[i].w;
		color = l.box[i].rank % 8;

		if (ascii) {
			for (j = 1; j + 1 < (size_t)w; j++) {
				fymm_canvas_put(cv, x + (int)j, y, '-', color, 0);
				fymm_canvas_put(cv, x + (int)j, y + 2, '-',
						color, 0);
			}
			fymm_canvas_put(cv, x, y, '+', color, 0);
			fymm_canvas_put(cv, x + w - 1, y, '+', color, 0);
			fymm_canvas_put(cv, x, y + 2, '+', color, 0);
			fymm_canvas_put(cv, x + w - 1, y + 2, '+', color, 0);
			fymm_canvas_put(cv, x, y + 1, '|', color, 0);
			fymm_canvas_put(cv, x + w - 1, y + 1, '|', color, 0);
		} else {
			for (j = 1; j + 1 < (size_t)w; j++) {
				fymm_canvas_put(cv, x + (int)j, y, c[4], color, 0);
				fymm_canvas_put(cv, x + (int)j, y + 2, c[4],
						color, 0);
			}
			fymm_canvas_put(cv, x, y, c[0], color, 0);
			fymm_canvas_put(cv, x + w - 1, y, c[1], color, 0);
			fymm_canvas_put(cv, x, y + 2, c[2], color, 0);
			fymm_canvas_put(cv, x + w - 1, y + 2, c[3], color, 0);
			fymm_canvas_put(cv, x, y + 1, c[5], color, 0);
			fymm_canvas_put(cv, x + w - 1, y + 1, c[5], color, 0);
		}
		fymm_canvas_text(cv, x + 2, y + 1, l.box[i].text,
				 FYMM_COLOR_DEFAULT, 0);
	}

	(void)maxw;
	out = fymm_canvas_emit(cv);
	fymm_canvas_destroy(cv);

out:
	free(pair);
	free(back);
	free(state);
	free(l.rank_width);
	free(l.rank_y);
	free(l.box);
	return out;
}
