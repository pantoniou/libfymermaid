/*
 * fymm-render-wardley.c - plotting a Wardley map
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

/* the stages a map is drawn against when it does not name its own */
static const char *const wl_default_stages[4] = {
	"Genesis", "Custom", "Product", "Commodity",
};

#define WL_PLOT_H 16

static int wl_clampi(int v, int lo, int hi)
{
	return v < lo ? lo : v > hi ? hi : v;
}

/*
 * A Wardley map is a scatter plot: evolution runs left to right, visibility to
 * the user runs bottom to top. The terminal gives few enough cells that the
 * labels cannot sit inside the plot without colliding, so each component gets
 * a numbered mark on the plot and its name in the legend beneath it. Links are
 * drawn as right-angled routes between the marks they join, which is a
 * distortion of the straight lines a rendered map uses but keeps them on the
 * cell grid.
 */
char *fymm_render_wardley(const struct fymm_diagram *d, fy_generic model,
			  const struct fymm_render_cfg *cfg)
{
	fy_generic nodes, links, notes, stages, pipelines, node, link;
	struct fymm_canvas *cv;
	struct fymm_theme theme;
	const char *title, *name, *label;
	size_t nnodes, nstages, nlinks, nnotes, npipes, i, k;
	int width, height, plot_w, plot_x, plot_y, legend_y, y;
	int *nx = NULL, *ny = NULL;
	bool ascii;
	char buf[160];
	char *out;

	if (fymm_theme_resolve(&theme, cfg, fymm_diagram_builder(d), model))
		return NULL;

	nodes = fy_get(model, "nodes");
	links = fy_get(model, "links");
	notes = fy_get(model, "notes");
	stages = fy_get(model, "stages");
	pipelines = fy_get(model, "pipelines");
	title = fy_get(model, "title", (const char *)NULL);

	nnodes = fy_is_sequence(nodes) ? fy_len(nodes) : 0;
	nlinks = fy_is_sequence(links) ? fy_len(links) : 0;
	nnotes = fy_is_sequence(notes) ? fy_len(notes) : 0;
	npipes = fy_is_sequence(pipelines) ? fy_len(pipelines) : 0;
	nstages = fy_is_sequence(stages) ? fy_len(stages) : 0;
	if (!nstages)
		nstages = 4;

	width = cfg && cfg->width > 0 ? cfg->width : 80;
	if (width < 40)
		width = 40;
	if (width > 100)
		width = 100;

	/* the plot leaves a gutter for the visibility axis and its ticks */
	plot_x = 8;
	plot_w = width - plot_x - 1;
	plot_y = title ? 2 : 0;
	legend_y = plot_y + WL_PLOT_H + 2;
	height = legend_y + (int)(nnodes + nnotes + npipes) + 1;

	cv = fymm_canvas_create_cfg(width, height, cfg, &theme);
	if (!cv)
		goto err;
	ascii = cv->charset == FYMM_CHARSET_ASCII;

	if (nnodes) {
		nx = malloc(nnodes * sizeof(*nx));
		ny = malloc(nnodes * sizeof(*ny));
		if (!nx || !ny)
			goto err;
	}

	if (title)
		fymm_rich_text(cv, 0, 0, title, FYMM_PAL_TITLE,
			       FYMM_ATTR_BOLD);

	/* the axes: visibility up the left, evolution along the bottom */
	fymm_canvas_vline(cv, plot_x - 1, plot_y, plot_y + WL_PLOT_H - 1,
			  FYMM_PAL_LABEL, false);
	fymm_canvas_hline(cv, plot_y + WL_PLOT_H, plot_x - 1,
			  plot_x + plot_w - 1, FYMM_PAL_LABEL, false);
	fymm_canvas_text(cv, 0, plot_y, "visible", FYMM_PAL_LABEL,
			 FYMM_ATTR_DIM);
	fymm_canvas_text(cv, 0, plot_y + WL_PLOT_H - 1, " buried",
			 FYMM_PAL_LABEL, FYMM_ATTR_DIM);

	for (i = 0; i < nstages; i++) {
		int x0 = plot_x + (int)(i * (size_t)plot_w / nstages);
		int x1 = plot_x + (int)((i + 1) * (size_t)plot_w / nstages) - 1;

		name = fy_is_sequence(stages) && i < fy_len(stages) ?
			fy_get(fy_get_at(stages, i), "name", "") :
			wl_default_stages[i];
		if (i)
			fymm_canvas_vline(cv, x0 - 1, plot_y,
					  plot_y + WL_PLOT_H, (int)(i % 8),
					  false);
		if (x1 > x0)
			fymm_canvas_text(cv, x0, plot_y + WL_PLOT_H + 1, name,
					 (int)(i % 8), FYMM_ATTR_DIM);
	}

	/* the marks, numbered so the legend can name them */
	for (i = 0; i < nnodes; i++) {
		double vis, evo;

		node = fy_get_at(nodes, i);
		vis = fy_number(fy_get(node, "visibility"), 0.5);
		evo = fy_number(fy_get(node, "evolution"), 0.5);

		nx[i] = wl_clampi(plot_x + (int)(evo * (plot_w - 1)),
				  plot_x, plot_x + plot_w - 1);
		ny[i] = wl_clampi(plot_y + (int)((1.0 - vis) *
						 (WL_PLOT_H - 1)),
				  plot_y, plot_y + WL_PLOT_H - 1);
	}

	for (i = 0; i < nlinks; i++) {
		size_t from = nnodes, to = nnodes;

		link = fy_get_at(links, i);
		for (k = 0; k < nnodes; k++) {
			name = fy_get(fy_get_at(nodes, k), "name", "");
			if (!strcmp(name, fy_get(link, "from", "")))
				from = k;
			if (!strcmp(name, fy_get(link, "to", "")))
				to = k;
		}
		if (from == nnodes || to == nnodes)
			continue;
		fymm_canvas_route_v(cv, nx[from], ny[from], nx[to], ny[to],
				    (ny[from] + ny[to]) / 2, FYMM_PAL_LABEL,
				    false);
	}

	for (i = 0; i < nnodes; i++) {
		snprintf(buf, sizeof(buf), "%zu", i + 1);
		fymm_canvas_put(cv, nx[i], ny[i], ascii ? 'o' : 0x25cf,
				(int)(i % 8), FYMM_ATTR_BOLD);
		fymm_canvas_text(cv, nx[i] + 1, ny[i], buf, (int)(i % 8), 0);
	}

	/* the legend, then the notes and pipelines that have no mark */
	y = legend_y;
	for (i = 0; i < nnodes; i++) {
		node = fy_get_at(nodes, i);
		snprintf(buf, sizeof(buf), "%zu. %s", i + 1,
			 fy_get(node, "name", ""));
		fymm_rich_text(cv, 0, y, buf, (int)(i % 8), 0);
		label = fy_get(node, "kind", "component");
		if (strcmp(label, "component"))
			fymm_canvas_text(cv, fymm_rich_measure(buf) + 1, y,
					 label, FYMM_PAL_TAG, FYMM_ATTR_DIM);
		y++;
	}

	for (i = 0; i < npipes; i++) {
		fy_generic members = fy_get(fy_get_at(pipelines, i),
					    "members");
		size_t nm = fy_is_sequence(members) ? fy_len(members) : 0;
		int x;

		snprintf(buf, sizeof(buf), "%s %s",
			 ascii ? "|" : "\xe2\x94\x82",
			 fy_get(fy_get_at(pipelines, i), "name", ""));
		x = fymm_canvas_text(cv, 0, y, buf, FYMM_PAL_TAG,
				     FYMM_ATTR_BOLD);
		for (k = 0; k < nm; k++)
			x += fymm_canvas_text(cv, x + 1, y,
					      fy_get(fy_get_at(members, k),
						     "name", ""),
					      FYMM_PAL_LABEL, 0) + 1;
		y++;
	}

	for (i = 0; i < nnotes; i++) {
		snprintf(buf, sizeof(buf), "%s %s", ascii ? "*" : "\xe2\x97\x87",
			 fy_get(fy_get_at(notes, i), "text", ""));
		fymm_rich_text(cv, 0, y, buf, FYMM_PAL_LABEL, FYMM_ATTR_DIM);
		y++;
	}

	out = fymm_canvas_emit(cv);
	fymm_canvas_destroy(cv);
	free(nx);
	free(ny);
	return out;

err:
	if (cv)
		fymm_canvas_destroy(cv);
	free(nx);
	free(ny);
	return NULL;
}
