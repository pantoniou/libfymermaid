/*
 * fymm-render-architecture.c - drawing an architecture diagram
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


/*
 * Services are boxed and ranked by the links between them; a group becomes a
 * heading over the services that belong to it, since a terminal cannot nest a
 * box inside a box and keep both readable.
 */
struct fymm_canvas *fymm_render_architecture(const struct fymm_diagram *d,
					     fy_generic model,
					     const struct fymm_render_cfg *cfg)
{
	fy_generic nodes, edges, nd, edge;
	struct fymm_canvas *cv = NULL;
	struct fymm_theme theme;
	struct fymm_lnode *node = NULL;
	struct fymm_ledge *ledge = NULL;
	struct fymm_layout lay;
	struct fymm_metrics met;
	struct fymm_layout_cfg lcfg = { 0, 0, 0, 0, 0, 0, FYMM_LAYOUT_DOWN };
	const char **text = NULL, **icon = NULL;
	size_t *index = NULL;
	const char *title, *group;
	size_t nnodes, nedges, nsvc = 0, ngroup = 0, i, j;
	int width, height, top, x, y, w, color, sx, sy, dx, dy, ymid;
	bool ascii;

	fymm_metrics_resolve(&met, fymm_diagram_type(d), cfg);

	if (fymm_theme_resolve(&theme, cfg, fymm_diagram_builder(d), model))
		return NULL;

	nodes = fy_get(model, "nodes");
	edges = fy_get(model, "edges");
	title = fy_get(model, "title", (const char *)NULL);
	nnodes = fy_is_sequence(nodes) ? fy_len(nodes) : 0;
	nedges = fy_is_sequence(edges) ? fy_len(edges) : 0;
	if (!nnodes)
		return fymm_canvas_empty(cfg);

	node = calloc(nnodes, sizeof(*node));
	text = calloc(nnodes, sizeof(*text));
	icon = calloc(nnodes, sizeof(*icon));
	index = calloc(nnodes, sizeof(*index));
	ledge = calloc(nedges ? nedges : 1, sizeof(*ledge));
	if (!node || !text || !icon || !index || !ledge)
		goto out;

	/* only the services and junctions are laid out; a group is a heading */
	for (i = 0; i < nnodes; i++) {
		nd = fy_get_at(nodes, i);
		if (!strcmp(fy_get(nd, "kind", ""), "group")) {
			ngroup++;
			continue;
		}
		index[nsvc] = i;
		node[nsvc].id = fy_get(nd, "id", "");
		text[nsvc] = fy_get(nd, "label", (const char *)NULL);
		if (!text[nsvc])
			text[nsvc] = node[nsvc].id;
		icon[nsvc] = fy_get(nd, "icon", (const char *)NULL);
		node[nsvc].w = fymm_rich_measure(text[nsvc]) + 4;
		node[nsvc].h = 3;
		nsvc++;
	}
	if (!nsvc)
		return fymm_canvas_empty(cfg);

	for (i = 0, j = 0; i < nedges; i++) {
		edge = fy_get_at(edges, i);
		ledge[j].from = fymm_layout_find(node, nsvc,
						 fy_get(edge, "from", ""));
		ledge[j].to = fymm_layout_find(node, nsvc,
					       fy_get(edge, "to", ""));
		if (ledge[j].from == (size_t)-1 || ledge[j].to == (size_t)-1)
			continue;
		j++;
	}
	nedges = j;

	top = (title ? 2 : 0) + (int)(ngroup ? ngroup + 1 : 0);
	lcfg.top = top;
	lcfg.col_gap = met.col_gap;
	lcfg.rank_gap = met.rank_gap;
	lcfg.dir = FYMM_LAYOUT_DOWN;
	if (cfg && (cfg->fit == FYMM_FIT_SHRINK ||
		    cfg->fit == FYMM_FIT_LEGEND))
		lcfg.max_width = met.max_width;
	if (fymm_layout_layered(node, nsvc, ledge, nedges, &lcfg, &lay))
		goto out;

	width = lay.width + 2;
	height = top + lay.height + lay.rank_gap;
	if (title && fymm_rich_measure(title) + 2 > width)
		width = fymm_rich_measure(title) + 2;

	cv = fymm_canvas_create_cfg(width, height, cfg, &theme);
	if (!cv)
		goto out;
	ascii = cv->charset == FYMM_CHARSET_ASCII;

	y = 0;
	if (title) {
		fymm_canvas_elem_begin(cv, FYMM_EL_TITLE, fy_invalid, "title");
		fymm_rich_text(cv, 0, y, title, FYMM_PAL_TITLE,
				 FYMM_ATTR_BOLD);
		fymm_canvas_elem_end(cv);
		y += 2;
	}

	/* the groups, listed above the diagram with what they contain */
	for (i = 0; i < nnodes && ngroup; i++) {
		nd = fy_get_at(nodes, i);
		if (strcmp(fy_get(nd, "kind", ""), "group"))
			continue;
		group = fy_get(nd, "label", (const char *)NULL);
		if (!group)
			group = fy_get(nd, "id", "");
		x = fymm_canvas_text(cv, 0, y, group, (int)(i % 8),
				     FYMM_ATTR_BOLD);
		if (width - x - 2 > 0)
			fymm_canvas_hline(cv, y, x + 1, width - 2,
					  (int)(i % 8), false);
		y++;
	}

	for (i = 0; i < nedges; i++) {
		color = node[ledge[i].from].rank % 8;
		sx = node[ledge[i].from].x + node[ledge[i].from].w / 2;
		dx = node[ledge[i].to].x + node[ledge[i].to].w / 2;
		sy = node[ledge[i].from].y + node[ledge[i].from].h;
		dy = node[ledge[i].to].y - 1;
		if (dy < sy)
			dy = sy;
		ymid = sy + (dy - sy) / 2;
		fymm_canvas_route_v(cv, sx, sy, dx, dy, ymid, color, false);
		if (fy_get(fy_get_at(edges, i), "arrowTo", false))
			fymm_canvas_put(cv, dx, dy, ascii ? 'v' : 0x25bc,
					color, 0);
	}

	for (i = 0; i < nsvc; i++) {
		x = node[i].x;
		y = node[i].y;
		w = node[i].w;
		color = node[i].rank % 8;

		fymm_canvas_elem_begin(cv, FYMM_EL_NODE,
				       fy_get_at(nodes, index[i]),
				       "nodes/%zu", index[i]);

		for (j = 1; j + 1 < (size_t)w; j++) {
			fymm_canvas_put(cv, x + (int)j, y,
					ascii ? '-' : 0x2500, color, 0);
			fymm_canvas_put(cv, x + (int)j, y + 2,
					ascii ? '-' : 0x2500, color, 0);
		}
		fymm_canvas_put(cv, x, y, ascii ? '+' : 0x250c, color, 0);
		fymm_canvas_put(cv, x + w - 1, y, ascii ? '+' : 0x2510, color, 0);
		fymm_canvas_put(cv, x, y + 2, ascii ? '+' : 0x2514, color, 0);
		fymm_canvas_put(cv, x + w - 1, y + 2, ascii ? '+' : 0x2518,
				color, 0);
		fymm_canvas_put(cv, x, y + 1, ascii ? '|' : 0x2502, color, 0);
		fymm_canvas_put(cv, x + w - 1, y + 1, ascii ? '|' : 0x2502,
				color, 0);
		fymm_rich_text(cv, x + 2, y + 1, text[i], FYMM_COLOR_DEFAULT,
			       0);
		fymm_canvas_elem_end(cv);
	}

out:
	free(node);
	free(ledge);
	free(text);
	free(icon);
	free(index);
	return cv;
}
