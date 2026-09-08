/*
 * fymm-render.c - terminal capability probing and the gitGraph renderer
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
#include <unistd.h>

#include <libfymd4c.h>

#include "fymm-canvas.h"
#include "fymm-legend.h"
#include "fymm-internal.h"

void fymm_render_cfg_default(struct fymm_render_cfg *cfg)
{
	if (!cfg)
		return;
	memset(cfg, 0, sizeof(*cfg));
	cfg->struct_size = sizeof(*cfg);
	cfg->width = FYMM_WIDTH_AUTO;
	cfg->fit = FYMM_FIT_SHRINK;
	cfg->color = FYMM_COLOR_AUTO;
	cfg->charset = FYMM_CHARSET_AUTO;
	cfg->options = fy_invalid;
	cfg->theme = NULL;
	cfg->theme_path = NULL;
	cfg->background = FYMM_BG_AUTO;
}

/*
 * Resolve what the configuration left to the terminal. @fd is what the render
 * is going to, so the probing asks about the right one.
 */
static void fymm_render_cfg_resolve(struct fymm_render_cfg *cfg, int fd)
{
	if (cfg->color == FYMM_COLOR_AUTO)
		cfg->color = fymm_detect_color_mode(fd);
	if (cfg->charset == FYMM_CHARSET_AUTO)
		cfg->charset = fymm_detect_charset();
	if (cfg->width == FYMM_WIDTH_AUTO)
		cfg->width = fymm_detect_width(fd);

	/* asking the terminal costs a round trip, so only ask when the answer
	 * could change anything */
	if (cfg->background == FYMM_BG_AUTO &&
	    cfg->color != FYMM_COLOR_NONE && !cfg->theme)
		cfg->background = fymm_detect_background(fd);

	/*
	 * A palette chosen for a dark terminal washes out on a light one, so
	 * a light terminal takes the theme that was made for it. A caller
	 * that named a theme has already said what it wants.
	 */
	if (cfg->background == FYMM_BG_LIGHT && !cfg->theme)
		cfg->theme = "light";
}

/*
 * The commit glyphs.  A merge is drawn as a ring so that it reads as a node
 * two lines arrive at, and a cherry-pick as a diamond, echoing the shapes
 * mermaid uses in SVG.
 */
static uint32_t fymm_commit_glyph(enum fymm_charset cs, const char *type)
{
	if (cs == FYMM_CHARSET_ASCII) {
		if (!strcmp(type, "REVERSE"))
			return 'x';
		if (!strcmp(type, "HIGHLIGHT"))
			return '#';
		if (!strcmp(type, "MERGE"))
			return 'o';
		if (!strcmp(type, "CHERRY_PICK"))
			return '%';
		return '*';
	}
	if (!strcmp(type, "REVERSE"))
		return 0x2297;		/* U+2297 circled times */
	if (!strcmp(type, "HIGHLIGHT"))
		return 0x25a3;		/* U+25A3 white square containing black */
	if (!strcmp(type, "MERGE"))
		return 0x25ce;		/* U+25CE bullseye */
	if (!strcmp(type, "CHERRY_PICK"))
		return 0x25c8;		/* U+25C8 white diamond containing black */
	return 0x25cf;			/* U+25CF black circle */
}

/* struct gg_geom - everything the drawing pass needs, computed up front */
struct gg_geom {
	int *col;		/* the column index of each commit */
	int *x;			/* the canvas column of each column index */
	int *slot;		/* the width of each column index */
	int *lane;		/* the lane of each commit */
	int ncols;
	int gutter;
	int top;		/* the first row of the first lane band */
	int width, height;
};

static void gg_geom_fini(struct gg_geom *g)
{
	free(g->col);
	free(g->x);
	free(g->slot);
	free(g->lane);
	memset(g, 0, sizeof(*g));
}

/* The three rows of a lane band: a tag row, the lane itself, a label row. */
static int gg_lane_row(const struct gg_geom *g, int lane)
{
	return g->top + lane * 3 + 1;
}

/*
 * A horizontal run that attaches to something at both ends, so every cell
 * reaches both ways; the junction glyph then falls out of the direction mask
 * where it meets a vertical run.
 */
static void gg_run_h(struct fymm_canvas *cv, int y, int x0, int x1, int color,
		     bool dashed)
{
	int x;

	for (x = x0; x <= x1; x++)
		fymm_canvas_line(cv, x, y, FYMM_LN_W | FYMM_LN_E, color,
				 dashed);
}

static void gg_run_v(struct fymm_canvas *cv, int x, int y0, int y1, int color,
		     bool dashed)
{
	int y;

	for (y = y0; y <= y1; y++)
		fymm_canvas_line(cv, x, y, FYMM_LN_N | FYMM_LN_S, color,
				 dashed);
}

/*
 * Connect a parent commit to a child commit.  Within one lane that is a
 * straight run; across lanes the line leaves the parent, turns at the column
 * just before the child, and comes in horizontally, which is the shape a
 * terminal can draw without the curve mermaid gets in SVG.
 */
static void gg_draw_edge(struct fymm_canvas *cv, const struct gg_geom *g,
			 int parent, int child, int color, bool dashed)
{
	int xp = g->x[g->col[parent]];
	int xc = g->x[g->col[child]];
	int yp = gg_lane_row(g, g->lane[parent]);
	int yc = gg_lane_row(g, g->lane[child]);
	int xt, ytop, ybot;

	if (xc <= xp)
		return;			/* nothing sensible to draw backwards */

	if (yp == yc) {
		gg_run_h(cv, yp, xp + 1, xc - 1, color, dashed);
		return;
	}

	xt = xc - 1;
	if (xt <= xp)
		xt = xp + 1;

	ytop = yp < yc ? yp : yc;
	ybot = yp < yc ? yc : yp;

	gg_run_h(cv, yp, xp + 1, xt - 1, color, dashed);
	fymm_canvas_line(cv, xt, yp,
			 (uint8_t)(FYMM_LN_W | (yc > yp ? FYMM_LN_S : FYMM_LN_N)),
			 color, dashed);
	gg_run_v(cv, xt, ytop + 1, ybot - 1, color, dashed);
	fymm_canvas_line(cv, xt, yc,
			 (uint8_t)(FYMM_LN_E | (yc > yp ? FYMM_LN_N : FYMM_LN_S)),
			 color, dashed);
	gg_run_h(cv, yc, xt + 1, xc - 1, color, dashed);
}

/* Wrap a tag in the brackets that stand in for mermaid's tag flag. */
static char *gg_tag_text(fy_generic tags)
{
	size_t i, count, len = 0;
	char *out, *p;

	if (!fy_is_sequence(tags))
		return NULL;
	count = fy_generic_sequence_get_item_count(tags);
	if (!count)
		return NULL;

	for (i = 0; i < count; i++)
		len += strlen(fy_get_at(tags, i, "")) + 3;
	out = malloc(len + 1);
	if (!out)
		return NULL;
	for (i = 0, p = out; i < count; i++)
		p += sprintf(p, "[%s]", fy_get_at(tags, i, ""));
	return out;
}

struct fymm_canvas *fymm_render_gitgraph(const struct fymm_diagram *d,
					 fy_generic model,
					 const struct fymm_render_cfg *cfg)
{
	fy_generic config, commits, branches, commit, tags, parents;
	struct fymm_canvas *cv = NULL;
	struct fymm_theme theme;
	struct gg_geom g;
	struct fymm_legend *legend = NULL;
	const char *title, *label, *type, *orientation;
	bool show_branches, show_labels, parallel;
	size_t ncommits, nbranches, i, j, nparents;
	int k, w, color, x, y, cherry;
	char *tag_text;

	memset(&g, 0, sizeof(g));

	if (fymm_theme_resolve(&theme, cfg, fymm_diagram_builder(d), model))
		return NULL;

	config = fy_get(model, "config");
	commits = fy_get(model, "commits");
	branches = fy_get(model, "branches");
	orientation = fy_get(model, "orientation", "LR");
	title = fy_get(model, "title", (const char *)NULL);

	ncommits = fy_is_sequence(commits) ?
		   fy_generic_sequence_get_item_count(commits) : 0;
	nbranches = fy_is_sequence(branches) ?
		    fy_generic_sequence_get_item_count(branches) : 0;
	if (!nbranches)
		return fymm_canvas_empty(cfg);

	show_branches = fy_get(config, "showBranches", true);
	show_labels = fy_get(config, "showCommitLabel", true);
	parallel = fy_get(config, "parallelCommits", false);

	/* the caller's overrides win over whatever the source asked for */
	if (cfg && fy_is_mapping(cfg->options)) {
		show_branches = fy_get(cfg->options, "showBranches",
				       show_branches);
		show_labels = fy_get(cfg->options, "showCommitLabel",
				     show_labels);
		parallel = fy_get(cfg->options, "parallelCommits", parallel);
	}

	g.col = calloc(ncommits ? ncommits : 1, sizeof(*g.col));
	g.lane = calloc(ncommits ? ncommits : 1, sizeof(*g.lane));
	if (!g.col || !g.lane)
		goto err_out;

	/* lane of each commit, through its branch */
	for (i = 0; i < ncommits; i++) {
		long long bi = fy_get(fy_get_at(commits, i), "branch", 0LL);
		fy_generic b = fy_get_at(branches, (size_t)bi);

		g.lane[i] = (int)fy_get(b, "lane", 0LL);
	}

	/*
	 * Columns.  By default mermaid lays commits out temporally, one
	 * column per commit in the order they were written; parallelCommits
	 * instead puts every commit one step past its furthest parent, so
	 * that work done in parallel lines up.
	 */
	g.ncols = 0;
	for (i = 0; i < ncommits; i++) {
		if (!parallel) {
			g.col[i] = (int)i;
		} else {
			parents = fy_get(fy_get_at(commits, i), "parents");
			nparents = fy_is_sequence(parents) ?
				   fy_generic_sequence_get_item_count(parents) : 0;
			g.col[i] = 0;
			for (j = 0; j < nparents; j++) {
				int pi = (int)fy_get_at(parents, j, 0LL);

				if (pi >= 0 && g.col[pi] + 1 > g.col[i])
					g.col[i] = g.col[pi] + 1;
			}
		}
		if (g.col[i] + 1 > g.ncols)
			g.ncols = g.col[i] + 1;
	}
	if (!g.ncols)
		g.ncols = 1;

	g.slot = calloc((size_t)g.ncols, sizeof(*g.slot));
	g.x = calloc((size_t)g.ncols, sizeof(*g.x));
	if (!g.slot || !g.x)
		goto err_out;

	/* Each column is wide enough for its own commit label and tags, so
	 * that neighbouring labels never run into one another. */
	for (k = 0; k < g.ncols; k++)
		g.slot[k] = 4;
	for (i = 0; i < ncommits; i++) {
		commit = fy_get_at(commits, i);
		if (show_labels) {
			label = fy_get(commit, "label", "");
			w = fymm_text_width(label) + 2;
			if (w > g.slot[g.col[i]])
				g.slot[g.col[i]] = w;
		}
		tag_text = gg_tag_text(fy_get(commit, "tags"));
		if (tag_text) {
			w = fymm_text_width(tag_text) + 2;
			if (w > g.slot[g.col[i]])
				g.slot[g.col[i]] = w;
			free(tag_text);
		}
	}

	g.gutter = 1;
	if (show_branches) {
		for (i = 0; i < nbranches; i++) {
			w = fymm_text_width(fy_get(fy_get_at(branches, i),
						   "name", "")) + 2;
			if (w > g.gutter)
				g.gutter = w;
		}
	}

	/*
	 * A column is as wide as the label it carries, so a graph of long
	 * messages runs off a narrow terminal. Take the widest column down a
	 * cell at a time until the graph fits or every column is down to the
	 * four cells the lane glyphs need. What no longer fits its column is
	 * moved into a legend, when the caller asked for one.
	 */
	if (cfg && (cfg->fit == FYMM_FIT_SHRINK ||
		    cfg->fit == FYMM_FIT_LEGEND) && cfg->width > 0) {
		int budget = cfg->width - g.gutter - 1;

		for (;;) {
			int total = 0, widest = 0;

			for (k = 0; k < g.ncols; k++) {
				total += g.slot[k];
				if (g.slot[k] > g.slot[widest])
					widest = k;
			}
			if (total <= budget || g.slot[widest] <= 4)
				break;
			g.slot[widest]--;
		}
	}

	g.x[0] = g.gutter;
	for (k = 1; k < g.ncols; k++)
		g.x[k] = g.x[k - 1] + g.slot[k - 1];

	g.top = title ? 2 : 0;
	g.width = g.x[g.ncols - 1] + g.slot[g.ncols - 1] + 1;
	g.height = g.top + (int)nbranches * 3;

	if (cfg && cfg->fit == FYMM_FIT_LEGEND) {
		legend = fymm_legend_create();
		if (!legend)
			goto err_out;
		for (i = 0; i < ncommits; i++) {
			commit = fy_get_at(commits, i);
			label = fy_get(commit, "label", "");
			if (!show_labels || !label || !*label)
				continue;
			if (fymm_text_width(label) + 2 <= g.slot[g.col[i]])
				continue;
			if (fymm_legend_add(legend, label) == (size_t)-1)
				goto err_out;
		}
		g.height += fymm_legend_rows(legend);
		if (fymm_legend_width(legend) + 1 > g.width)
			g.width = fymm_legend_width(legend) + 1;
	}

	cv = fymm_canvas_create_cfg(g.width, g.height, cfg, &theme);
	if (!cv)
		goto err_out;

	if (title) {
		fymm_canvas_elem_begin(cv, FYMM_EL_TITLE, fy_invalid, "title");
		fymm_canvas_text(cv, 0, 0, title, FYMM_PAL_TITLE,
				 FYMM_ATTR_BOLD);
		fymm_canvas_elem_end(cv);
	}

	/* the branch names, right aligned in the gutter */
	if (show_branches) {
		for (i = 0; i < nbranches; i++) {
			fy_generic b = fy_get_at(branches, i);
			const char *name = fy_get(b, "name", "");
			int lane = (int)fy_get(b, "lane", 0LL);

			x = g.gutter - 1 - fymm_text_width(name);
			fymm_canvas_elem_begin(cv, FYMM_EL_NODE, b,
					       "branches/%zu", i);
			fymm_canvas_text(cv, x < 0 ? 0 : x,
					 gg_lane_row(&g, lane), name,
					 lane % 8, 0);
			fymm_canvas_elem_end(cv);
		}
	}

	/* the edges first, so that a node always sits on top of its lines */
	for (i = 0; i < ncommits; i++) {
		commit = fy_get_at(commits, i);
		parents = fy_get(commit, "parents");
		nparents = fy_is_sequence(parents) ?
			   fy_generic_sequence_get_item_count(parents) : 0;
		for (j = 0; j < nparents; j++) {
			int pi = (int)fy_get_at(parents, j, -1LL);

			if (pi < 0 || (size_t)pi >= ncommits)
				continue;
			/* a merge's second parent keeps the colour of the
			 * branch it came from; everything else follows the
			 * branch the line is arriving at */
			color = (j == 1 ? g.lane[pi] : g.lane[i]) % 8;
			gg_draw_edge(cv, &g, pi, (int)i, color, false);
		}
		cherry = (int)fy_get(commit, "cherryFrom", -1LL);
		if (cherry >= 0 && (size_t)cherry < ncommits)
			gg_draw_edge(cv, &g, cherry, (int)i,
				     g.lane[cherry] % 8, true);
	}

	/* then the nodes, their labels and their tags */
	for (i = 0; i < ncommits; i++) {
		commit = fy_get_at(commits, i);
		type = fy_get(commit, "type", "NORMAL");
		x = g.x[g.col[i]];
		y = gg_lane_row(&g, g.lane[i]);
		color = g.lane[i] % 8;

		fymm_canvas_elem_begin(cv, FYMM_EL_NODE, commit,
				       "commits/%zu", i);
		fymm_canvas_put(cv, x, y,
				fymm_commit_glyph(cv->charset, type), color,
				FYMM_ATTR_BOLD);

		if (show_labels) {
			label = fy_get(commit, "label", "");
			if (label && *label) {
				size_t idx = (size_t)-1;

				if (legend)
					idx = fymm_legend_find(legend, label);
				if (idx != (size_t)-1)
					fymm_canvas_text(cv, x,	y + 1,
						fymm_legend_marker(legend,
								   idx),
						fymm_legend_color(legend, idx),
						FYMM_ATTR_BOLD);
				else
					fymm_canvas_text_max(cv, x, y + 1,
						label,
						g.slot[g.col[i]] - 1,
						FYMM_PAL_LABEL, 0);
			}
		}

		tags = fy_get(commit, "tags");
		tag_text = gg_tag_text(tags);
		if (tag_text) {
			fymm_canvas_text(cv, x, y - 1, tag_text, FYMM_PAL_TAG,
					 0);
			free(tag_text);
		}
		fymm_canvas_elem_end(cv);
	}

	if (legend)
		fymm_legend_draw(cv, 0, g.top + (int)nbranches * 3 - 1,
				 legend);

	fymm_legend_destroy(legend);
	gg_geom_fini(&g);

	/* TB and BT transpose the whole layout; the parser has already warned
	 * that this renderer only draws left to right. */
	(void)orientation;
	return cv;

err_out:
	fymm_legend_destroy(legend);
	gg_geom_fini(&g);
	return NULL;
}

/*
 * The size a render takes, which is the size of what it emits: the widest
 * line in cells and the number of lines. Measuring by rendering is the only
 * way that cannot drift from the render itself; every alternative is a second
 * copy of every renderer's arithmetic, and it would be wrong the first time a
 * renderer changed.
 */
int fymm_measure(const struct fymm_diagram *d,
		 const struct fymm_render_cfg *cfg, int *wp, int *hp)
{
	const char *p, *e;
	char *out;
	int w = 0, h = 0, n;

	out = fymm_render(d, cfg);
	if (!out)
		return -1;

	for (p = out; *p; p = e) {
		e = strchr(p, '\n');
		if (!e)
			e = p + strlen(p);
		n = (int)fymd_str_width(p, (size_t)(e - p));
		if (n > w)
			w = n;
		h++;
		if (*e)
			e++;
	}

	fymm_free(out);
	if (wp)
		*wp = w;
	if (hp)
		*hp = h;
	return 0;
}

/*
 * A render result keeps the canvas the renderer drew on, so that the place of
 * each element stays known and a new selection costs an emission rather than
 * a layout.
 */
struct fymm_render_result {
	struct fymm_canvas *cv;
	char *text;
	struct fymm_element *elems;
	size_t nelems;
	const char *selection;
};

/*
 * Resolve the configuration and draw the diagram. The result holds the canvas
 * and nothing else; the caller takes the elements and the text out of it.
 */
static struct fymm_render_result *
fymm_render_prepare(const struct fymm_diagram *d,
		    const struct fymm_render_cfg *cfg,
		    struct fymm_render_cfg *lcfg)
{
	const struct fymm_diagram_ops *ops;
	struct fymm_render_result *r;

	if (!d || fymm_diagram_has_errors(d))
		return NULL;

	if (cfg)
		*lcfg = *cfg;
	else
		fymm_render_cfg_default(lcfg);
	fymm_render_cfg_resolve(lcfg, STDOUT_FILENO);

	ops = fymm_diagram_ops_by_type(d->type);
	if (!ops || !ops->render)
		return NULL;

	r = calloc(1, sizeof(*r));
	if (!r)
		return NULL;
	r->cv = ops->render(d, d->model, lcfg);
	if (!r->cv) {
		free(r);
		return NULL;
	}
	return r;
}

/*
 * Take the elements of the canvas into the result, in the coordinates of the
 * emitted text: emission drops the blank rows above the drawing and writes
 * the margin, and it stops at the clip. An element that the clip reaches is
 * reported as the part of it that is on the screen.
 */
static int fymm_result_elements(struct fymm_render_result *r)
{
	const struct fymm_canvas *cv = r->cv;
	const struct fymm_element *se;
	struct fymm_element *de;
	int x0, x1, y0, y1;
	size_t i;

	if (!cv->nelems)
		return 0;

	r->elems = calloc(cv->nelems, sizeof(*r->elems));
	if (!r->elems)
		return -1;

	for (i = 0; i < cv->nelems; i++) {
		se = &cv->elems[i];
		de = &r->elems[r->nelems++];
		*de = *se;

		y0 = se->row;
		y1 = se->row + se->height;
		x0 = se->col;
		x1 = se->col + se->width;

		if (y0 < cv->row0)
			y0 = cv->row0;
		if (y1 > cv->rowN)
			y1 = cv->rowN;
		if (cv->clip_w > 0 && x1 > cv->clip_w)
			x1 = cv->clip_w;

		de->clipped = y0 != se->row || y1 != se->row + se->height ||
			      x1 != se->col + se->width;
		if (y1 <= y0 || x1 <= x0) {
			/* the element is drawn, but nothing of it is on the
			 * screen; it keeps its path and loses its place */
			de->row = de->col = de->width = de->height = 0;
			continue;
		}
		de->row = y0 - cv->row0 + cv->margin;
		de->col = x0 + cv->margin;
		de->width = x1 - x0;
		de->height = y1 - y0;
	}
	return 0;
}

/* Emit the canvas again, which is what a changed selection needs. */
static int fymm_result_emit(struct fymm_render_result *r)
{
	char *text;

	text = fymm_canvas_emit(r->cv);
	if (!text)
		return -1;
	free(r->text);
	r->text = text;
	return 0;
}

struct fymm_render_result *fymm_render_ex(const struct fymm_diagram *d,
					  const struct fymm_render_cfg *cfg)
{
	struct fymm_render_result *r;
	struct fymm_render_cfg lcfg;

	r = fymm_render_prepare(d, cfg, &lcfg);
	if (!r)
		return NULL;

	/* emission settles which rows and columns reach the screen, so the
	 * elements are placed after it */
	if (fymm_result_emit(r) || fymm_result_elements(r)) {
		fymm_render_result_destroy(r);
		return NULL;
	}
	if (lcfg.selection)
		fymm_render_result_select(r, lcfg.selection,
					  lcfg.selection_style);
	return r;
}

void fymm_render_result_destroy(struct fymm_render_result *r)
{
	if (!r)
		return;
	fymm_canvas_destroy(r->cv);
	free(r->elems);
	free(r->text);
	free(r);
}

const char *fymm_render_result_text(const struct fymm_render_result *r)
{
	return r ? r->text : NULL;
}

size_t fymm_render_result_count(const struct fymm_render_result *r)
{
	return r ? r->nelems : 0;
}

const struct fymm_element *
fymm_render_result_element(const struct fymm_render_result *r, size_t i)
{
	if (!r || i >= r->nelems)
		return NULL;
	return &r->elems[i];
}

const struct fymm_element *
fymm_render_result_find(const struct fymm_render_result *r, const char *path)
{
	size_t i;

	if (!r || !path)
		return NULL;
	for (i = 0; i < r->nelems; i++) {
		if (!strcmp(r->elems[i].path, path))
			return &r->elems[i];
	}
	return NULL;
}

bool fymm_render_result_select(struct fymm_render_result *r, const char *path,
			       enum fymm_selection_style style)
{
	const struct fymm_element *e;

	if (!r)
		return false;

	e = fymm_render_result_find(r, path);
	fymm_canvas_select(r->cv, e ? e->path : NULL, style);
	r->selection = e ? e->path : NULL;
	if (fymm_result_emit(r))
		return false;
	return e != NULL;
}

const char *fymm_render_result_selection(const struct fymm_render_result *r)
{
	return r ? r->selection : NULL;
}

char *fymm_render(const struct fymm_diagram *d,
		  const struct fymm_render_cfg *cfg)
{
	struct fymm_render_result *r;
	char *out;

	r = fymm_render_ex(d, cfg);
	if (!r)
		return NULL;
	out = r->text;
	r->text = NULL;
	fymm_render_result_destroy(r);
	return out;
}

int fymm_render_fp(const struct fymm_diagram *d,
		   const struct fymm_render_cfg *cfg, FILE *fp)
{
	struct fymm_render_cfg lcfg;
	char *text;

	if (!d || !fp)
		return -1;

	if (cfg) {
		lcfg = *cfg;
	} else {
		fymm_render_cfg_default(&lcfg);
	}

	/* probe against the stream we are about to write to, not stdout */
	fymm_render_cfg_resolve(&lcfg, fileno(fp));

	text = fymm_render(d, &lcfg);
	if (!text)
		return -1;
	if (fputs(text, fp) == EOF) {
		free(text);
		return -1;
	}
	free(text);
	return 0;
}
