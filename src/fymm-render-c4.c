/*
 * fymm-render-c4.c - drawing a C4 model as ranked boxes
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
 * Each element is a box of its label over its kind, and its description
 * beneath when it has one. The relations rank them down the page, which puts
 * the people at the top and what they use below, the way a C4 diagram reads.
 */
char *fymm_render_c4(const struct fymm_diagram *d, fy_generic model,
		     const struct fymm_render_cfg *cfg)
{
	fy_generic elements, relations, el, rel;
	struct fymm_canvas *cv;
	struct fymm_theme theme;
	struct fymm_lnode *node = NULL;
	struct fymm_ledge *edge = NULL;
	struct fymm_layout lay;
	struct fymm_metrics met;
	struct fymm_layout_cfg lcfg = { 0, 0, 0, 0, 0, 0, FYMM_LAYOUT_DOWN };
	const char **label = NULL, **kindp = NULL, **descr = NULL;
	const char *title, *text;
	size_t nel, nrel, i, j;
	int width, height, top, x, y, w, color, sx, sy, dx, dy, ymid;
	bool ascii;
	char buf[128];
	char *out = NULL;

	fymm_metrics_resolve(&met, fymm_diagram_type(d), cfg);

	if (fymm_theme_resolve(&theme, cfg, fymm_diagram_builder(d), model))
		return NULL;

	elements = fy_get(model, "elements");
	relations = fy_get(model, "relations");
	title = fy_get(model, "title", (const char *)NULL);
	nel = fy_is_sequence(elements) ? fy_len(elements) : 0;
	nrel = fy_is_sequence(relations) ? fy_len(relations) : 0;
	if (!nel)
		return strdup("");

	node = calloc(nel, sizeof(*node));
	label = calloc(nel, sizeof(*label));
	kindp = calloc(nel, sizeof(*kindp));
	descr = calloc(nel, sizeof(*descr));
	edge = calloc(nrel ? nrel : 1, sizeof(*edge));
	if (!node || !label || !kindp || !descr || !edge)
		goto out;

	for (i = 0; i < nel; i++) {
		el = fy_get_at(elements, i);
		node[i].id = fy_get(el, "id", "");
		label[i] = fy_get(el, "label", node[i].id);
		kindp[i] = fy_get(el, "kind", "system");
		descr[i] = fy_get(el, "description", (const char *)NULL);

		w = fymm_rich_measure(label[i]);
		snprintf(buf, sizeof(buf), "[%s]", kindp[i]);
		if (fymm_text_width(buf) > w)
			w = fymm_text_width(buf);
		if (descr[i] && fymm_rich_measure(descr[i]) > w)
			w = fymm_rich_measure(descr[i]);
		/* a description can be a sentence; clip the box, not the page */
		if (w > 40)
			w = 40;
		node[i].w = w + 4;
		node[i].h = descr[i] ? 5 : 4;
	}

	for (i = 0, j = 0; i < nrel; i++) {
		rel = fy_get_at(relations, i);
		edge[j].from = fymm_layout_find(node, nel, fy_get(rel, "from", ""));
		edge[j].to = fymm_layout_find(node, nel, fy_get(rel, "to", ""));
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
	if (cfg && cfg->fit == FYMM_FIT_SHRINK)
		lcfg.max_width = met.max_width;
	if (fymm_layout_layered(node, nel, edge, nrel, &lcfg, &lay))
		goto out;

	width = lay.width + 2;
	height = top + lay.height + lay.rank_gap;
	for (i = 0; i < nrel; i++) {
		text = fy_get(fy_get_at(relations, i), "label",
			      (const char *)NULL);
		if (!text || !*text)
			continue;
		w = node[edge[i].to].x + node[edge[i].to].w / 2 + 3 +
		    fymm_rich_measure(text);
		if (w > width)
			width = w;
	}
	if (title && fymm_rich_measure(title) + 2 > width)
		width = fymm_rich_measure(title) + 2;

	cv = fymm_canvas_create_cfg(width, height, cfg, &theme);
	if (!cv)
		goto out;
	ascii = cv->charset == FYMM_CHARSET_ASCII;

	if (title)
		fymm_rich_text(cv, 0, 0, title, FYMM_PAL_TITLE,
				 FYMM_ATTR_BOLD);

	for (i = 0; i < nrel; i++) {
		color = node[edge[i].from].rank % 8;
		sx = node[edge[i].from].x + node[edge[i].from].w / 2;
		dx = node[edge[i].to].x + node[edge[i].to].w / 2;
		sy = node[edge[i].from].y + node[edge[i].from].h;
		dy = node[edge[i].to].y - 1;
		if (dy < sy)
			dy = sy;
		ymid = sy + (dy - sy) / 2;
		fymm_canvas_route_v(cv, sx, sy, dx, dy, ymid, color, false);
		fymm_canvas_put(cv, dx, dy, ascii ? 'v' : 0x25bc, color, 0);

		text = fy_get(fy_get_at(relations, i), "label",
			      (const char *)NULL);
		if (text && *text)
			fymm_canvas_text(cv, dx + 2, ymid, text, FYMM_PAL_LABEL,
					 0);
	}

	for (i = 0; i < nel; i++) {
		bool person = !strcmp(kindp[i], "person");

		x = node[i].x;
		y = node[i].y;
		w = node[i].w;
		color = node[i].rank % 8;

		for (j = 1; j + 1 < (size_t)w; j++) {
			fymm_canvas_put(cv, x + (int)j, y,
					ascii ? '-' : 0x2500, color, 0);
			fymm_canvas_put(cv, x + (int)j, y + node[i].h - 1,
					ascii ? '-' : 0x2500, color, 0);
		}
		fymm_canvas_put(cv, x, y, ascii ? '+' : (person ? 0x256d : 0x250c),
				color, 0);
		fymm_canvas_put(cv, x + w - 1, y,
				ascii ? '+' : (person ? 0x256e : 0x2510), color, 0);
		fymm_canvas_put(cv, x, y + node[i].h - 1,
				ascii ? '+' : (person ? 0x2570 : 0x2514), color, 0);
		fymm_canvas_put(cv, x + w - 1, y + node[i].h - 1,
				ascii ? '+' : (person ? 0x256f : 0x2518), color, 0);
		for (j = 1; j + 1 < (size_t)node[i].h; j++) {
			fymm_canvas_put(cv, x, y + (int)j, ascii ? '|' : 0x2502,
					color, 0);
			fymm_canvas_put(cv, x + w - 1, y + (int)j,
					ascii ? '|' : 0x2502, color, 0);
		}

		fymm_rich_text(cv, x + 2, y + 1, label[i], color,
			       FYMM_ATTR_BOLD);
		snprintf(buf, sizeof(buf), "[%s]", kindp[i]);
		fymm_canvas_text(cv, x + 2, y + 2, buf, FYMM_PAL_LABEL,
				 FYMM_ATTR_DIM);
		if (descr[i])
			fymm_rich_text(cv, x + 2, y + 3, descr[i],
				       FYMM_COLOR_DEFAULT, 0);
	}

	out = fymm_canvas_emit(cv);
	fymm_canvas_destroy(cv);

out:
	free(node);
	free(edge);
	free(label);
	free(kindp);
	free(descr);
	return out;
}
