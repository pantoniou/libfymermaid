/*
 * fymm-render-er.c - drawing an entity relationship diagram
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
#include "fymm-legend.h"


/*
 * Crow's foot notation, as two cells at each end of the connector. The many
 * end is the foot; the one end is a bar; the optional end adds a ring.
 */
static void er_foot(struct fymm_canvas *cv, int x, int y, const char *card,
		    int color, bool down)
{
	bool many = !strcmp(card, "zero or more") ||
		    !strcmp(card, "one or more");
	bool optional = !strcmp(card, "zero or one") ||
			!strcmp(card, "zero or more");
	uint32_t g;

	if (cv->charset == FYMM_CHARSET_ASCII)
		g = many ? (down ? 'V' : 'A') : (optional ? 'o' : '=');
	else if (many)
		g = down ? 0x2963 : 0x2965;	/* upward/downward harpoons */
	else
		g = optional ? 0x25cb : 0x2550;

	fymm_canvas_put(cv, x, y, g, color, FYMM_ATTR_BOLD);
}

struct fymm_canvas *fymm_render_er(const struct fymm_diagram *d,
				   fy_generic model,
				   const struct fymm_render_cfg *cfg)
{
	fy_generic entities, relations, ent, rel, attr;
	struct fymm_canvas *cv = NULL;
	struct fymm_theme theme;
	struct fymm_legend *legend = NULL;
	struct fymm_lnode *node = NULL;
	struct fymm_ledge *edge = NULL;
	struct fymm_layout lay;
	struct fymm_metrics met;
	struct fymm_layout_cfg lcfg = { 0, 0, 0, 0, 0, 0, FYMM_LAYOUT_DOWN };
	fy_generic *attrs = NULL;
	const char *title, *label;
	size_t nent, nrel, i, j, idx, best;
	size_t *node_marker = NULL, *rel_marker = NULL;
	int *min_width = NULL, *label_x = NULL;
	int width, height, top, x, y, w, color, sx, sy, dx, dy, ymid;
	bool ascii;
	int legend_y = 0, row, count, used, saving, best_saving, marker_width;
	bool progressive = false;
	char buf[256];

	fymm_metrics_resolve(&met, fymm_diagram_type(d), cfg);

	if (fymm_theme_resolve(&theme, cfg, fymm_diagram_builder(d), model))
		return NULL;

	entities = fy_get(model, "entities");
	relations = fy_get(model, "relations");
	title = fy_get(model, "title", (const char *)NULL);
	nent = fy_is_sequence(entities) ? fy_len(entities) : 0;
	nrel = fy_is_sequence(relations) ? fy_len(relations) : 0;
	if (!nent)
		return fymm_canvas_empty(cfg);

	node = calloc(nent, sizeof(*node));
	attrs = calloc(nent, sizeof(*attrs));
	edge = calloc(nrel ? nrel : 1, sizeof(*edge));
	node_marker = malloc(nent * sizeof(*node_marker));
	min_width = calloc(nent, sizeof(*min_width));
	rel_marker = malloc((nrel ? nrel : 1) * sizeof(*rel_marker));
	label_x = calloc(nrel ? nrel : 1, sizeof(*label_x));
	if (!node || !attrs || !edge || !node_marker || !min_width ||
	    !rel_marker || !label_x)
		goto out;

	/* an entity is a box of its name over its attributes */
	for (i = 0; i < nent; i++) {
		ent = fy_get_at(entities, i);
		node[i].id = fy_get(ent, "name", "");
		attrs[i] = fy_get(ent, "attributes");

		node_marker[i] = (size_t)-1;
		min_width[i] = 4;
		w = fymm_rich_measure(node[i].id);
		if (fy_is_sequence(attrs[i])) {
			fy_foreach(attr, attrs[i]) {
				snprintf(buf, sizeof(buf), "%s %s",
					 fy_get(attr, "type", ""),
					 fy_get(attr, "name", ""));
				if (fymm_rich_measure(buf) + 4 > min_width[i])
					min_width[i] = fymm_rich_measure(buf) + 4;
				if (fymm_rich_measure(buf) > w)
					w = fymm_rich_measure(buf);
			}
		}
		node[i].w = w + 4;
		node[i].h = 3;
		if (fy_is_sequence(attrs[i]) && fy_len(attrs[i]))
			node[i].h += 1 + (int)fy_len(attrs[i]);
	}

	for (i = 0, j = 0; i < nrel; i++) {
		rel = fy_get_at(relations, i);
		edge[j].from = fymm_layout_find(node, nent,
						fy_get(rel, "from", ""));
		edge[j].to = fymm_layout_find(node, nent, fy_get(rel, "to", ""));
		if (edge[j].from == (size_t)-1 || edge[j].to == (size_t)-1)
			continue;
		j++;
	}
	nrel = j;

	top = title ? 2 : 0;
	lcfg.top = top;
	lcfg.col_gap = met.col_gap;
	lcfg.rank_gap = met.rank_gap;
	lcfg.dir = FYMM_LAYOUT_DOWN;
	if (cfg && (cfg->fit == FYMM_FIT_SHRINK ||
		    cfg->fit == FYMM_FIT_LEGEND))
		lcfg.max_width = met.max_width;
	if (fymm_layout_layered(node, nent, edge, nrel, &lcfg, &lay))
		goto out;

	if (lay.tight && cfg && cfg->fit == FYMM_FIT_LEGEND) {
		legend = fymm_legend_create();
		if (!legend)
			goto out;
		progressive = true;
		/* Shorten only names in ranks that still exceed the width. */
		while (lay.tight) {
			best = (size_t)-1;
			best_saving = 0;
			snprintf(buf, sizeof(buf), "%zu", fymm_legend_count(legend) + 1);
			marker_width = fymm_rich_measure(buf) + 4;
			for (i = 0; i < nent; i++) {
				if (node_marker[i] != (size_t)-1)
					continue;
				used = -lay.col_gap;
				for (j = 0; j < nent; j++) {
					if (node[j].rank == node[i].rank)
						used += node[j].w + lay.col_gap;
				}
				if (used <= lcfg.max_width)
					continue;
				w = min_width[i] > marker_width ? min_width[i] : marker_width;
				saving = node[i].w - w;
				if (saving > best_saving) {
					best = i;
					best_saving = saving;
				}
			}
			if (best == (size_t)-1)
				break;
			idx = fymm_legend_add(legend, node[best].id);
			if (idx == (size_t)-1)
				goto out;
			node_marker[best] = idx;
			node[best].w = min_width[best] > marker_width ?
				min_width[best] : marker_width;
			if (fymm_layout_layered(node, nent, edge, nrel, &lcfg, &lay))
				goto out;
		}
		/* Each relation starts on a separate row of its source rank. */
		for (i = 0; i < nent; i++) {
			count = 0;
			for (j = 0; j < nrel; j++) {
				if (node[edge[j].from].rank == node[i].rank)
					count++;
			}
			if (count + 3 > lcfg.rank_gap)
				lcfg.rank_gap = count + 3;
		}
		if (fymm_layout_layered(node, nent, edge, nrel, &lcfg, &lay))
			goto out;
	}

	/* Place each relation label on a side with sufficient space. */
	for (i = 0; i < nrel; i++) {
		rel_marker[i] = (size_t)-1;
		dx = node[edge[i].to].x + node[edge[i].to].w / 2;
		label_x[i] = dx + 2;
		if (!cfg || cfg->fit != FYMM_FIT_LEGEND || met.max_width <= 0)
			continue;
		label = fy_get(fy_get_at(relations, i), "label", "");
		w = fymm_rich_measure(label);
		if (label_x[i] + w <= met.max_width)
			continue;
		if (dx - w - 1 >= 0) {
			label_x[i] = dx - w - 1;
			continue;
		}
		if (!legend) {
			legend = fymm_legend_create();
			if (!legend)
				goto out;
		}
		idx = fymm_legend_add(legend, label);
		if (idx == (size_t)-1)
			goto out;
		rel_marker[i] = idx;
		w = fymm_rich_measure(fymm_legend_marker(legend, idx));
		if (label_x[i] + w > met.max_width && dx - w - 1 >= 0)
			label_x[i] = dx - w - 1;
	}

	width = lay.width + 2;
	height = top + lay.height + lay.rank_gap;
	for (i = 0; i < nrel; i++) {
		label = fy_get(fy_get_at(relations, i), "label",
			       (const char *)NULL);
		if (!label || !*label)
			continue;
		w = label_x[i] + fymm_rich_measure(rel_marker[i] != (size_t)-1 ?
			fymm_legend_marker(legend, rel_marker[i]) : label);
		if (w > width)
			width = w;
	}
	if (title && fymm_rich_measure(title) + 2 > width)
		width = fymm_rich_measure(title) + 2;

	if (legend) {
		legend_y = top + lay.height;
		height = legend_y + fymm_legend_rows(legend);
		if (fymm_legend_width(legend) > width)
			width = fymm_legend_width(legend);
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

	/* the relations first, so an entity sits on top of its connectors */
	for (i = 0; i < nrel; i++) {
		rel = fy_get_at(relations, i);
		color = node[edge[i].from].rank % 8;
		sx = node[edge[i].from].x + node[edge[i].from].w / 2;
		dx = node[edge[i].to].x + node[edge[i].to].w / 2;
		sy = node[edge[i].from].y + node[edge[i].from].h;
		dy = node[edge[i].to].y - 1;
		if (dy < sy + 1)
			dy = sy + 1;

		ymid = sy + 1 + (dy - sy - 2) / 2;
		if (progressive) {
			row = 0;
			for (j = 0; j < i; j++) {
				if (node[edge[j].from].rank == node[edge[i].from].rank)
					row++;
			}
			ymid = sy + 1 + row;
		}
		fymm_canvas_route_v(cv, sx, sy + 1, dx, dy - 1, ymid, color,
				    !strcmp(fy_get(rel, "line", ""),
					    "non-identifying"));

		er_foot(cv, sx, sy, fy_get(rel, "fromCardinality", ""), color,
			false);
		er_foot(cv, dx, dy, fy_get(rel, "toCardinality", ""), color,
			true);

		label = fy_get(rel, "label", (const char *)NULL);
		if (label && *label) {
			color = FYMM_PAL_LABEL;
			if (rel_marker[i] != (size_t)-1) {
				idx = rel_marker[i];
				label = fymm_legend_marker(legend, idx);
				color = fymm_legend_color(legend, idx);
			}
			fymm_rich_text(cv, label_x[i], ymid, label, color, 0);
		}
	}

	/* then the entity boxes */
	for (i = 0; i < nent; i++) {
		x = node[i].x;
		y = node[i].y;
		w = node[i].w;
		color = node[i].rank % 8;

		fymm_canvas_elem_begin(cv, FYMM_EL_NODE,
				       fy_get_at(entities, i),
				       "entities/%zu", i);

		for (j = 1; j + 1 < (size_t)w; j++) {
			fymm_canvas_put(cv, x + (int)j, y,
					ascii ? '-' : 0x2500, color, 0);
			fymm_canvas_put(cv, x + (int)j, y + node[i].h - 1,
					ascii ? '-' : 0x2500, color, 0);
		}
		fymm_canvas_put(cv, x, y, ascii ? '+' : 0x250c, color, 0);
		fymm_canvas_put(cv, x + w - 1, y, ascii ? '+' : 0x2510, color, 0);
		fymm_canvas_put(cv, x, y + node[i].h - 1, ascii ? '+' : 0x2514,
				color, 0);
		fymm_canvas_put(cv, x + w - 1, y + node[i].h - 1,
				ascii ? '+' : 0x2518, color, 0);
		for (j = 1; j + 1 < (size_t)node[i].h; j++) {
			fymm_canvas_put(cv, x, y + (int)j, ascii ? '|' : 0x2502,
					color, 0);
			fymm_canvas_put(cv, x + w - 1, y + (int)j,
					ascii ? '|' : 0x2502, color, 0);
		}

		label = node[i].id;
		if (node_marker[i] != (size_t)-1) {
			idx = node_marker[i];
			label = fymm_legend_marker(legend, idx);
			color = fymm_legend_color(legend, idx);
		}
		fymm_rich_text(cv, x + (w - fymm_rich_measure(label)) / 2,
			       y + 1, label, color, FYMM_ATTR_BOLD);

		if (fy_is_sequence(attrs[i]) && fy_len(attrs[i])) {
			int r = 2;

			for (j = 1; j + 1 < (size_t)w; j++)
				fymm_canvas_put(cv, x + (int)j, y + r,
						ascii ? '-' : 0x2500, color, 0);
			fymm_canvas_put(cv, x, y + r, ascii ? '+' : 0x251c,
					color, 0);
			fymm_canvas_put(cv, x + w - 1, y + r,
					ascii ? '+' : 0x2524, color, 0);
			r++;
			fy_foreach(attr, attrs[i]) {
				snprintf(buf, sizeof(buf), "%s %s",
					 fy_get(attr, "type", ""),
					 fy_get(attr, "name", ""));
				fymm_rich_text(cv, x + 2, y + r, buf,
					       FYMM_COLOR_DEFAULT, 0);
				r++;
			}
		}
		fymm_canvas_elem_end(cv);
	}

	if (legend)
		fymm_legend_draw(cv, 0, legend_y, legend);

out:
	fymm_legend_destroy(legend);
	free(node);
	free(edge);
	free(attrs);
	free(node_marker);
	free(min_width);
	free(rel_marker);
	free(label_x);
	return cv;
}
