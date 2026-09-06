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

/* the rows a node box occupies */
#define FC_BOX_ROWS 3

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

/* the cells a container frame keeps clear around the nodes it holds */
#define FC_FRAME_PAD 1

/*
 * struct fc_group - one container, placed
 *
 * @rect is the frame, computed from the nodes the container holds and from
 * the frames of the containers nested in it. @framed is clear when a node the
 * container does not hold falls inside that rectangle: the frame would then
 * claim a node that is not its own, so the title is drawn beside the first
 * member instead.
 */
struct fc_group {
	const char *id;
	struct fymm_rich *title;
	int tw;			/* the drawn width of the title */
	int depth;
	int x0, y0, x1, y1;
	bool used;
	bool framed;
};

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
	size_t group;		/* the innermost container, or (size_t)-1 */
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

/* The outermost container @inner sits in, which may be @inner itself. */
static size_t fc_outermost(const struct fc_group *grp, size_t inner)
{
	size_t k, out = inner;
	int depth;

	if (inner == (size_t)-1)
		return inner;
	depth = grp[inner].depth;
	for (k = inner; depth && k-- > 0; ) {
		if (grp[k].depth >= depth)
			continue;
		depth = grp[k].depth;
		out = k;
	}
	return out;
}

/*
 * Does @g hold @box, at any depth? A container is entered by the nodes
 * between its header and its `end`, so the containers open at that point are
 * the ones the node belongs to. The model records only the innermost, and the
 * enclosing ones are the entries before it that are still open, which are the
 * ones of a smaller depth that come earlier in the list.
 */
static bool fc_holds(const struct fc_group *grp, size_t ngroups, size_t g,
		     size_t inner)
{
	size_t k;
	int depth;

	if (inner == (size_t)-1)
		return false;
	if (inner == g)
		return true;
	if (grp[g].depth >= grp[inner].depth || g > inner)
		return false;

	/* walk out of @inner, one enclosing container at a time */
	depth = grp[inner].depth;
	for (k = inner; k-- > 0; ) {
		if (grp[k].depth >= depth)
			continue;
		depth = grp[k].depth;
		if (k == g)
			return true;
		if (!depth)
			break;
	}
	(void)ngroups;
	return false;
}



/*
 * Measure every container frame from the nodes it holds and from the frames
 * nested in it, innermost first.
 *
 * A node the container does not hold may fall inside the rectangle that
 * covers the ones it does, because two ranks are placed independently. Such a
 * node is pushed to the right, together with everything after it in its rank,
 * and the caller measures again.
 *
 * Returns true when a node was moved, so that the frames are now stale.
 */
static bool fc_frames(struct fc_group *grp, size_t ngroups,
		      struct fc_box *box, size_t nnodes, int col_gap)
{
	size_t g, i, j, k;
	bool moved = false;

	for (g = ngroups; g-- > 0; ) {
		bool first = true;

		if (!grp[g].used)
			continue;
		grp[g].framed = true;

		for (i = 0; i < nnodes; i++) {
			if (!fc_holds(grp, ngroups, g, box[i].group))
				continue;
			if (first) {
				grp[g].x0 = box[i].x;
				grp[g].y0 = box[i].y;
				grp[g].x1 = box[i].x + box[i].w - 1;
				grp[g].y1 = box[i].y + box[i].h - 1;
				first = false;
				continue;
			}
			if (box[i].x < grp[g].x0)
				grp[g].x0 = box[i].x;
			if (box[i].y < grp[g].y0)
				grp[g].y0 = box[i].y;
			if (box[i].x + box[i].w - 1 > grp[g].x1)
				grp[g].x1 = box[i].x + box[i].w - 1;
			if (box[i].y + box[i].h - 1 > grp[g].y1)
				grp[g].y1 = box[i].y + box[i].h - 1;
		}
		if (first) {
			grp[g].used = false;
			continue;
		}

		/* the containers nested in this one follow it in the list */
		for (k = g + 1; k < ngroups; k++) {
			if (!grp[k].used || grp[k].depth <= grp[g].depth)
				break;
			if (grp[k].x0 - 1 < grp[g].x0)
				grp[g].x0 = grp[k].x0 - 1;
			if (grp[k].y0 - 1 < grp[g].y0)
				grp[g].y0 = grp[k].y0 - 1;
			if (grp[k].x1 + 1 > grp[g].x1)
				grp[g].x1 = grp[k].x1 + 1;
			if (grp[k].y1 + 1 > grp[g].y1)
				grp[g].y1 = grp[k].y1 + 1;
		}

		grp[g].x0 -= FC_FRAME_PAD;
		grp[g].y0 -= FC_FRAME_PAD;
		grp[g].x1 += FC_FRAME_PAD;
		grp[g].y1 += FC_FRAME_PAD;

		/* the title sits on the top edge, between the corners */
		if (grp[g].tw && grp[g].x0 + grp[g].tw + 4 > grp[g].x1)
			grp[g].x1 = grp[g].x0 + grp[g].tw + 4;

		for (i = 0; i < nnodes; i++) {
			int shift;

			if (fc_holds(grp, ngroups, g, box[i].group))
				continue;
			if (box[i].x + box[i].w - 1 < grp[g].x0 ||
			    box[i].x > grp[g].x1 ||
			    box[i].y + box[i].h - 1 < grp[g].y0 ||
			    box[i].y > grp[g].y1)
				continue;

			/* a node that starts left of the frame cannot be
			 * pushed clear of it without crossing it */
			if (box[i].x < grp[g].x0) {
				grp[g].framed = false;
				continue;
			}

			/*
			 * Past the frame, and past everything else in the
			 * rank that was already clear of it. A node the
			 * container holds never moves: the frame covers it,
			 * so clearing the frame clears it too.
			 */
			shift = grp[g].x1 + 1 + col_gap / 2 - box[i].x;
			for (j = 0; j < nnodes; j++) {
				if (box[j].rank != box[i].rank ||
				    box[j].x < box[i].x ||
				    fc_holds(grp, ngroups, g, box[j].group))
					continue;
				box[j].x += shift;
			}
			moved = true;
		}
	}
	return moved;
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
	fy_generic nodes, edges, subgraphs, node, edge;
	struct fymm_canvas *cv;
	struct fymm_theme theme;
	struct fc_layout l;
	struct fymm_lnode *lnode = NULL;
	struct fymm_ledge *ledge = NULL;
	struct fymm_layout lay;
	struct fymm_metrics met;
	struct fymm_layout_cfg lcfg = { 0, 0, 0, 0, 0, 0, FYMM_LAYOUT_DOWN };
	enum fymm_layout_dir dir;
	struct fc_pair *pair = NULL;
	struct fc_group *grp = NULL;
	size_t *order = NULL;
	bool *back = NULL;
	const char *title, *text;
	size_t nnodes, nedges, ngroups, i, j, g, from, to;
	int x, y, w, top, color, sx, sy, dx, dy, mid = 0, rank_gap;
	int col_gap, pad, maxdepth;
	bool ascii;
	char *out = NULL;

	fymm_metrics_resolve(&met, fymm_diagram_type(d), cfg);

	if (fymm_theme_resolve(&theme, cfg, fymm_diagram_builder(d), model))
		return NULL;

	memset(&l, 0, sizeof(l));
	nodes = fy_get(model, "nodes");
	dir = strcmp(fy_get(model, "direction", "TB"), "TB") ?
	      FYMM_LAYOUT_RIGHT : FYMM_LAYOUT_DOWN;
	edges = fy_get(model, "edges");
	subgraphs = fy_get(model, "subgraphs");
	title = fy_get(model, "title", (const char *)NULL);

	nnodes = fy_is_sequence(nodes) ? fy_len(nodes) : 0;
	nedges = fy_is_sequence(edges) ? fy_len(edges) : 0;
	ngroups = fy_is_sequence(subgraphs) ? fy_len(subgraphs) : 0;
	if (!nnodes)
		return strdup("");

	l.n = nnodes;
	l.box = calloc(nnodes, sizeof(*l.box));
	if (!l.box)
		return NULL;

	maxdepth = 0;
	if (ngroups) {
		grp = calloc(ngroups, sizeof(*grp));
		if (!grp)
			goto out;
		for (g = 0; g < ngroups; g++) {
			fy_generic sg = fy_get_at(subgraphs, g);

			grp[g].id = fy_get(sg, "id", "");
			grp[g].title = fymm_rich_parse(fy_get(sg, "title", ""),
						fy_get(sg, "markdown", false));
			if (!grp[g].title)
				goto out;
			grp[g].tw = fymm_rich_width(grp[g].title);
			grp[g].depth = (int)fy_get(sg, "depth", 0LL);
			grp[g].framed = true;
			if (grp[g].depth > maxdepth)
				maxdepth = grp[g].depth;
		}
	}

	for (i = 0; i < nnodes; i++) {
		node = fy_get_at(nodes, i);
		l.box[i].id = fy_get(node, "id", "");
		l.box[i].group = (size_t)-1;
		text = fy_get(node, "subgraph", (const char *)NULL);
		for (g = 0; text && g < ngroups; g++) {
			if (strcmp(grp[g].id, text))
				continue;
			l.box[i].group = g;
			break;
		}
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

	/* a container holding only other containers holds their nodes too */
	for (i = 0; i < nnodes; i++) {
		for (g = 0; g < ngroups; g++) {
			if (fc_holds(grp, ngroups, g, l.box[i].group))
				grp[g].used = true;
		}
	}

	/*
	 * The layout keeps the order it is given within a rank, so the nodes
	 * of one container have to arrive together for its frame to enclose a
	 * contiguous run. Each node sorts at the first node of its outermost
	 * container, which pulls the members together and leaves every node
	 * outside a container where it was.
	 */
	if (ngroups) {
		order = calloc(nnodes, sizeof(*order));
		if (!order)
			goto out;
		for (i = 0; i < nnodes; i++) {
			g = fc_outermost(grp, l.box[i].group);

			order[i] = i;
			for (j = 0; g != (size_t)-1 && j < i; j++) {
				if (fc_outermost(grp, l.box[j].group) != g)
					continue;
				order[i] = order[j];
				break;
			}
		}
		/* a stable insertion sort; the key is the run each node
		 * belongs to, so equal keys keep their written order */
		for (i = 1; i < nnodes; i++) {
			struct fc_box tmp = l.box[i];
			size_t key = order[i];

			for (j = i; j && order[j - 1] > key; j--) {
				l.box[j] = l.box[j - 1];
				order[j] = order[j - 1];
			}
			l.box[j] = tmp;
			order[j] = key;
		}
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
	/*
	 * Every level of nesting takes a frame edge and the cell inside it,
	 * on each side. That is the room a container needs between it and its
	 * neighbour, and the margin the outermost frame needs at the edge of
	 * the canvas.
	 */
	pad = ngroups ? 2 * (maxdepth + 1) * FC_FRAME_PAD : 0;
	col_gap = met.col_gap + pad;
	rank_gap = met.rank_gap + pad;
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
	lcfg.top = top;
	lcfg.col_gap = col_gap;
	lcfg.rank_gap = rank_gap;
	lcfg.dir = dir;
	/*
	 * The budget is the width itself. What is drawn around the graph -
	 * the margin, the return lanes, the frames - is only drawn where it
	 * is used, so taking it off here would close up a graph that fits.
	 * Whatever those add over the width is for the clip.
	 */
	if (cfg && (cfg->fit == FYMM_FIT_SHRINK ||
		    cfg->fit == FYMM_FIT_LEGEND))
		lcfg.max_width = met.max_width;
	if (fymm_layout_layered(lnode, nnodes, ledge, j, &lcfg, &lay))
		goto out;

	/* the shared layout marks the returning links; carry that back so the
	 * drawing pass routes them through the margin */
	for (i = 0, j = 0; i < nedges; i++) {
		if (pair[i].from == (size_t)-1 || pair[i].to == (size_t)-1)
			continue;
		back[i] = ledge[j].back;
		j++;
	}
	/*
	 * The layout puts the first rank against the left edge and against
	 * @top, which leaves a frame nowhere to draw. Shift the whole graph
	 * in by the room the deepest nesting needs.
	 */
	for (i = 0; i < nnodes; i++) {
		l.box[i].x = lnode[i].x + pad;
		l.box[i].y = lnode[i].y + pad;
		l.box[i].rank = lnode[i].rank;
	}

	/*
	 * A container needs room the layered layout does not know to leave:
	 * a rank is placed without regard to the frames another rank's nodes
	 * sit inside. Measure the frames, push whatever they would wrongly
	 * enclose out to the right, and measure again. A node only ever moves
	 * right, so this settles.
	 */
	for (i = 0; i < 8 && ngroups; i++) {
		if (!fc_frames(grp, ngroups, l.box, nnodes, col_gap))
			break;
	}

	/* a node pushed clear of a frame sits past the width the layout
	 * reported, so the extents are measured from the nodes themselves */
	for (i = 0; i < nnodes; i++) {
		if (l.box[i].x + l.box[i].w > lay.width)
			lay.width = l.box[i].x + l.box[i].w;
	}

	l.width = lay.width + 2 + FC_RETURN_LANES + 2 * pad;
	l.height = top + lay.height + lay.rank_gap + 2 * pad;
	if (dir == FYMM_LAYOUT_RIGHT)
		l.height = top + lay.height + FC_RETURN_LANES + 1 + 2 * pad;
	if (title && fymm_rich_measure(title) + 2 > l.width)
		l.width = fymm_rich_measure(title) + 2;

	for (g = 0; g < ngroups; g++) {
		if (!grp[g].used)
			continue;
		if (grp[g].x1 + 2 > l.width)
			l.width = grp[g].x1 + 2;
		if (grp[g].y1 + 2 > l.height)
			l.height = grp[g].y1 + 2;
	}

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

	cv = fymm_canvas_create_cfg(l.width, l.height, cfg, &theme);
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
				/*
				 * The gap it sits in may have been closed up
				 * to meet the width; what does not fit is cut
				 * rather than drawn over the node. Below two
				 * cells nothing of the label survives but the
				 * ellipsis, which in a rail reads as part of
				 * the line, so draw none of it.
				 */
				if (dx - 1 - x >= 2)
					fymm_rich_text_max(cv, x, dy, text,
							   dx - 1 - x,
							   FYMM_PAL_LABEL, 0);
			}
		}
	}

	/*
	 * Then the container frames, over the edges that run through them and
	 * under the boxes they hold. The frame is dim and rounded so that it
	 * does not read as another node.
	 */
	for (g = 0; g < ngroups; g++) {
		if (!grp[g].used)
			continue;
		color = FYMM_PAL_BRANCH0 + (grp[g].depth % 8);

		if (grp[g].framed) {
			fymm_canvas_hline(cv, grp[g].y0, grp[g].x0 + 1,
					  grp[g].x1 - 1, color, true);
			fymm_canvas_hline(cv, grp[g].y1, grp[g].x0 + 1,
					  grp[g].x1 - 1, color, true);
			fymm_canvas_vline(cv, grp[g].x0, grp[g].y0 + 1,
					  grp[g].y1 - 1, color, true);
			fymm_canvas_vline(cv, grp[g].x1, grp[g].y0 + 1,
					  grp[g].y1 - 1, color, true);
			fymm_canvas_put(cv, grp[g].x0, grp[g].y0,
					ascii ? '+' : 0x256d, color, 0);
			fymm_canvas_put(cv, grp[g].x1, grp[g].y0,
					ascii ? '+' : 0x256e, color, 0);
			fymm_canvas_put(cv, grp[g].x0, grp[g].y1,
					ascii ? '+' : 0x2570, color, 0);
			fymm_canvas_put(cv, grp[g].x1, grp[g].y1,
					ascii ? '+' : 0x256f, color, 0);
		}

		/* the top edge is a row a frame owns and a node never uses */
		if (grp[g].tw)
			fymm_rich_draw_line(cv, grp[g].x0 + 2, grp[g].y0,
					    grp[g].title, 0, color,
					    FYMM_ATTR_BOLD);
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
	for (g = 0; g < ngroups; g++)
		fymm_rich_destroy(grp[g].title);
	free(grp);
	free(order);
	free(l.box);
	return out;
}
