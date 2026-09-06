/*
 * fymm-metrics.c - the spacing each diagram type is drawn with
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

#include <string.h>

#include "fymm-internal.h"

/*
 * The spacing each type was drawn with when these values were constants in
 * the renderers. A zero means the type has no use for the field.
 *
 * The gaps differ between types because the drawings do: a class box carries
 * compartments and needs room around it, an er relation needs a row for its
 * crow's foot, a flowchart is mostly boxes and arrows and reads better close
 * together.
 */
static const struct {
	enum fymm_diagram_type type;
	int col_gap;
	int rank_gap;
	int plot_height;
} fymm_type_metrics[] = {
	{ FYMM_DT_FLOWCHART,	2, 2,  0 },
	{ FYMM_DT_CLASS,	3, 3,  0 },
	{ FYMM_DT_ER,		3, 4,  0 },
	{ FYMM_DT_STATE,	3, 3,  0 },
	{ FYMM_DT_C4,		3, 3,  0 },
	{ FYMM_DT_USECASE,	3, 3,  0 },
	{ FYMM_DT_ARCHITECTURE,	3, 3,  0 },
	{ FYMM_DT_AGENTFLOW,	2, 2,  0 },
	{ FYMM_DT_BLOCK,	2, 0,  0 },
	{ FYMM_DT_KANBAN,	2, 0,  0 },
	{ FYMM_DT_SEQUENCE,	4, 0,  0 },
	{ FYMM_DT_XYCHART,	0, 0, 16 },
	{ FYMM_DT_WARDLEY,	0, 0, 16 },
	{ FYMM_DT_QUADRANT,	0, 0, 21 },
};

void fymm_metrics_default(struct fymm_metrics *m, enum fymm_diagram_type type)
{
	size_t i;

	if (!m)
		return;
	memset(m, 0, sizeof(*m));
	m->struct_size = sizeof(*m);

	/* what a graph uses, which is also the answer for a type that has no
	 * entry of its own */
	m->col_gap = 2;
	m->rank_gap = 2;

	for (i = 0; i < sizeof(fymm_type_metrics) /
		    sizeof(fymm_type_metrics[0]); i++) {
		if (fymm_type_metrics[i].type != type)
			continue;
		m->col_gap = fymm_type_metrics[i].col_gap;
		m->rank_gap = fymm_type_metrics[i].rank_gap;
		m->plot_height = fymm_type_metrics[i].plot_height;
		break;
	}
}

/*
 * What this render is to use: the defaults of the type, and then each field
 * the caller set. A field left at zero keeps the default, so a caller that
 * wants one thing changed says only that thing. Zero is the default of every
 * field that has a useful zero, so nothing is lost by reading it that way.
 *
 * A caller who sets no width limit of its own inherits the one the render
 * configuration carries.
 */
void fymm_metrics_resolve(struct fymm_metrics *m, enum fymm_diagram_type type,
			  const struct fymm_render_cfg *cfg)
{
	const struct fymm_metrics *o = cfg ? cfg->metrics : NULL;

	fymm_metrics_default(m, type);

	if (o && o->struct_size >= sizeof(*o)) {
		if (o->margin)
			m->margin = o->margin;
		if (o->col_gap)
			m->col_gap = o->col_gap;
		if (o->rank_gap)
			m->rank_gap = o->rank_gap;
		if (o->plot_height)
			m->plot_height = o->plot_height;
		if (o->max_width)
			m->max_width = o->max_width;
		if (o->max_height)
			m->max_height = o->max_height;
	}

	if (!m->max_width && cfg && cfg->width > 0 &&
	    cfg->fit != FYMM_FIT_NONE)
		m->max_width = cfg->width;
}
