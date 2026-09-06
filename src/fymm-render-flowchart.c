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
#include "fymm-layout.h"
#include "fymm-markdown.h"

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
	struct fymm_rich *text;
	enum fc_border border;
	int rank;
	int order;		/* position within the rank */
	int x, y, w, h;		/* the top left cell, the width and the height */
};

struct fc_layout {
	struct fc_box *box;
	size_t n;
	int nranks;
	int tall;		/* the tallest box, which sets the rank pitch */
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
	struct fymm_lnode *lnode = NULL;
	struct fymm_ledge *ledge = NULL;
	struct fymm_layout lay;
	enum fymm_layout_dir dir;
	struct fc_pair *pair = NULL;
	bool *back = NULL;
	const char *title, *text;
	size_t nnodes, nedges, i, j, from, to;
	int x, y, w, top, color, sx, sy, dx, dy, mid = 0, rank_gap;
	bool ascii;
	char *out = NULL;

	if (fymm_theme_resolve(&theme, cfg, fymm_diagram_builder(d), model))
		return NULL;

	memset(&l, 0, sizeof(l));
	nodes = fy_get(model, "nodes");
	dir = strcmp(fy_get(model, "direction", "TB"), "TB") ?
	      FYMM_LAYOUT_RIGHT : FYMM_LAYOUT_DOWN;
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
		l.box[i].text = fymm_rich_parse(fy_get(node, "text", ""),
						fy_get(node, "markdown", false));
		if (!l.box[i].text)
			goto out;
		l.box[i].border = fc_border_of(fy_get(node, "shape", "rect"));
		l.box[i].w = fymm_rich_width(l.box[i].text) + 4;
		l.box[i].h = (int)fymm_rich_lines(l.box[i].text) + 2;
		if (l.box[i].h > l.tall)
			l.tall = l.box[i].h;
	}

	/* resolve the edges once; the shared layout finds the ones that close
	 * a cycle and marks them */
	pair = calloc(nedges ? nedges : 1, sizeof(*pair));
	back = calloc(nedges ? nedges : 1, sizeof(*back));
	if (!pair || !back)
		goto out;
	for (i = 0; i < nedges; i++) {
		edge = fy_get_at(edges, i);
		pair[i].from = fc_find(&l, fy_get(edge, "from", ""));
		pair[i].to = fc_find(&l, fy_get(edge, "to", ""));
	}

	/*
	 * The ranks run down the page for a `TB` chart and across it for an
	 * `LR` one, which is the direction most flowcharts are written in.
	 */
	lnode = calloc(nnodes, sizeof(*lnode));
	ledge = calloc(nedges ? nedges : 1, sizeof(*ledge));
	if (!lnode || !ledge)
		goto out;
	for (i = 0; i < nnodes; i++) {
		lnode[i].id = l.box[i].id;
		lnode[i].w = l.box[i].w;
		lnode[i].h = l.box[i].h;
	}
	for (i = 0, j = 0; i < nedges; i++) {
		if (pair[i].from == (size_t)-1 || pair[i].to == (size_t)-1)
			continue;
		ledge[j].from = pair[i].from;
		ledge[j].to = pair[i].to;
		j++;
	}

	/*
	 * Running across the page an edge label sits in the gap between two
	 * ranks, so the gap has to be wide enough to hold the widest one.
	 * Running down the page the label sits beside the arrowhead and the
	 * gap does not have to grow.
	 */
	rank_gap = FC_RANK_GAP;
	if (dir == FYMM_LAYOUT_RIGHT) {
		for (i = 0; i < nedges; i++) {
			text = fy_get(fy_get_at(edges, i), "text",
				      (const char *)NULL);
			if (!text || !*text)
				continue;
			w = fymm_rich_measure(text) + 6;
			if (w > rank_gap)
				rank_gap = w;
		}
	}

	top = title ? 2 : 0;
	if (fymm_layout_layered(lnode, nnodes, ledge, j, top, FC_COL_GAP,
				rank_gap, dir, &lay))
		goto out;

	/* the shared layout marks the returning links; carry that back so the
	 * drawing pass routes them through the margin */
	for (i = 0, j = 0; i < nedges; i++) {
		if (pair[i].from == (size_t)-1 || pair[i].to == (size_t)-1)
			continue;
		back[i] = ledge[j].back;
		j++;
	}
	for (i = 0; i < nnodes; i++) {
		l.box[i].x = lnode[i].x;
		l.box[i].y = lnode[i].y;
		l.box[i].rank = lnode[i].rank;
	}

	l.width = lay.width + 2 + FC_RETURN_LANES;
	l.height = top + lay.height + FC_RANK_GAP;
	if (dir == FYMM_LAYOUT_RIGHT)
		l.height = top + lay.height + FC_RETURN_LANES + 1;
	if (title && fymm_rich_measure(title) + 2 > l.width)
		l.width = fymm_rich_measure(title) + 2;

	/* an edge label sits beside the arrowhead it belongs to */
	for (i = 0; i < nedges; i++) {
		text = fy_get(fy_get_at(edges, i), "text", (const char *)NULL);
		to = pair[i].to;
		if (!text || !*text || to == (size_t)-1)
			continue;
		w = (dir == FYMM_LAYOUT_DOWN ?
		     l.box[to].x + l.box[to].w / 2 :
		     l.box[to].x + l.box[to].w) + 3 + fymm_rich_measure(text);
		if (w > l.width)
			l.width = w;
	}

	cv = fymm_canvas_create(l.width, l.height,
				cfg && cfg->charset != FYMM_CHARSET_AUTO ?
					cfg->charset : FYMM_CHARSET_UNICODE,
				cfg ? cfg->color : FYMM_COLOR_NONE, &theme);
	if (!cv)
		goto out;
	ascii = cv->charset == FYMM_CHARSET_ASCII;

	if (title)
		fymm_rich_text(cv, 0, 0, title, FYMM_PAL_TITLE,
				 FYMM_ATTR_BOLD);

	/* the edges first, so that a box always sits on top of its links */
	for (i = 0; i < nedges; i++) {
		edge = fy_get_at(edges, i);
		from = pair[i].from;
		to = pair[i].to;
		if (from == (size_t)-1 || to == (size_t)-1)
			continue;

		color = l.box[from].rank % 8;

		if (dir == FYMM_LAYOUT_DOWN) {
			sx = l.box[from].x + l.box[from].w / 2;
			dx = l.box[to].x + l.box[to].w / 2;
			sy = l.box[from].y + l.box[from].h;
			dy = l.box[to].y - 1;
		} else {
			sx = l.box[from].x + l.box[from].w;
			dx = l.box[to].x - 1;
			sy = l.box[from].y + l.box[from].h / 2;
			dy = l.box[to].y + l.box[to].h / 2;
		}

		/*
		 * A link back to an earlier rank cannot run straight through
		 * the ranks between; it leaves its node, runs out to a lane
		 * in the margin, comes back and enters from the far side.
		 */
		if (back[i]) {
			int lane = dir == FYMM_LAYOUT_DOWN ?
				   l.width - 2 - (int)(i % FC_RETURN_LANES) :
				   l.height - 1 - (int)(i % FC_RETURN_LANES);

			if (dir == FYMM_LAYOUT_DOWN) {
				fymm_canvas_line(cv, sx, sy,
						 FYMM_LN_N | FYMM_LN_E, color,
						 false);
				fymm_canvas_hline(cv, sy, sx + 1, lane - 1,
						  color, false);
				fymm_canvas_line(cv, lane, sy,
						 FYMM_LN_W | FYMM_LN_N, color,
						 false);
				fymm_canvas_vline(cv, lane, dy, sy - 1, color,
						  false);
				fymm_canvas_line(cv, lane, dy,
						 FYMM_LN_S | FYMM_LN_W, color,
						 false);
				fymm_canvas_hline(cv, dy, dx + 1, lane - 1,
						  color, false);
				fymm_canvas_line(cv, dx, dy,
						 FYMM_LN_E | FYMM_LN_S, color,
						 false);
			} else {
				fymm_canvas_line(cv, sx, sy,
						 FYMM_LN_W | FYMM_LN_S, color,
						 false);
				fymm_canvas_vline(cv, sx, sy + 1, lane - 1,
						  color, false);
				fymm_canvas_line(cv, sx, lane,
						 FYMM_LN_N | FYMM_LN_W, color,
						 false);
				fymm_canvas_hline(cv, lane, dx, sx - 1, color,
						  false);
				fymm_canvas_line(cv, dx, lane,
						 FYMM_LN_E | FYMM_LN_N, color,
						 false);
				fymm_canvas_vline(cv, dx, dy + 1, lane - 1,
						  color, false);
				fymm_canvas_line(cv, dx, dy,
						 FYMM_LN_S | FYMM_LN_E, color,
						 false);
			}
		} else if (dir == FYMM_LAYOUT_DOWN) {
			mid = sy + (dy - sy) / 2;
			fymm_canvas_route_v(cv, sx, sy, dx, dy, mid, color,
					    false);
		} else {
			/* turn as soon as the link leaves its node, so that
			 * the run into the next rank is long enough to carry
			 * the label; several links leaving one node then share
			 * the turn, which reads as the bus it is */
			mid = sx + 2;
			fymm_canvas_route_h(cv, sx, sy, dx, dy, mid, color,
					    false);
		}

		/* the arrowhead points the way the rank runs */
		if (strcmp(fy_get(edge, "head", "none"), "none")) {
			const char *head = fy_get(edge, "head", "");
			uint32_t tip;

			if (!strcmp(head, "cross"))
				tip = ascii ? 'x' : 0x2717;
			else if (!strcmp(head, "circle"))
				tip = ascii ? 'o' : 0x25cb;
			else if (dir == FYMM_LAYOUT_DOWN)
				tip = ascii ? 'v' : 0x25bc;
			else
				tip = ascii ? '>' : 0x25b6;
			fymm_canvas_put(cv, dx, dy, tip, color, 0);
		}

		/*
		 * The label goes beside the arrowhead rather than along the
		 * shaft. Centring it on the run reads well until two links
		 * share a row, and then it lands on another link's line.
		 */
		text = fy_get(edge, "text", (const char *)NULL);
		if (text && *text) {
			if (dir == FYMM_LAYOUT_DOWN)
				fymm_rich_text(cv, dx + 2, dy, text,
					       FYMM_PAL_LABEL, 0);
			else {
				/* on the row of the node it arrives at, and
				 * ending before the arrowhead: several links
				 * leaving one node share a row, so a label at
				 * that end would land on another's */
				x = dx - 2 - fymm_rich_measure(text);
				if (x < mid + 1)
					x = mid + 1;
				fymm_rich_text(cv, x, dy, text,
					       FYMM_PAL_LABEL, 0);
			}
		}
	}

	/* then the boxes */
	for (i = 0; i < nnodes; i++) {
		const uint32_t *c = corners[l.box[i].border];
		size_t line, nlines;

		x = l.box[i].x;
		y = l.box[i].y;
		w = l.box[i].w;
		color = l.box[i].rank % 8;
		nlines = fymm_rich_lines(l.box[i].text);

		for (j = 1; j + 1 < (size_t)w; j++) {
			fymm_canvas_put(cv, x + (int)j, y,
					ascii ? '-' : c[4], color, 0);
			fymm_canvas_put(cv, x + (int)j, y + l.box[i].h - 1,
					ascii ? '-' : c[4], color, 0);
		}
		fymm_canvas_put(cv, x, y, ascii ? '+' : c[0], color, 0);
		fymm_canvas_put(cv, x + w - 1, y, ascii ? '+' : c[1], color, 0);
		fymm_canvas_put(cv, x, y + l.box[i].h - 1, ascii ? '+' : c[2],
				color, 0);
		fymm_canvas_put(cv, x + w - 1, y + l.box[i].h - 1,
				ascii ? '+' : c[3], color, 0);
		for (j = 1; j + 1 < (size_t)l.box[i].h; j++) {
			fymm_canvas_put(cv, x, y + (int)j, ascii ? '|' : c[5],
					color, 0);
			fymm_canvas_put(cv, x + w - 1, y + (int)j,
					ascii ? '|' : c[5], color, 0);
		}

		for (line = 0; line < nlines; line++)
			fymm_rich_draw_line(cv, x + 2, y + 1 + (int)line,
					    l.box[i].text, line,
					    FYMM_COLOR_DEFAULT, 0);
	}

	out = fymm_canvas_emit(cv);
	fymm_canvas_destroy(cv);

out:
	for (i = 0; i < nnodes; i++)
		fymm_rich_destroy(l.box[i].text);
	free(lnode);
	free(ledge);
	free(pair);
	free(back);
	free(l.box);
	return out;
}
