/*
 * fymm-legend.c - labels moved out of a drawing that cannot hold them
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

#include "fymm-legend.h"
#include "fymm-markdown.h"

struct fymm_legend_entry {
	char *label;
	char marker[8];
};

struct fymm_legend {
	struct fymm_legend_entry *entry;
	size_t n, alloc;
};

struct fymm_legend *fymm_legend_create(void)
{
	return calloc(1, sizeof(struct fymm_legend));
}

void fymm_legend_destroy(struct fymm_legend *lg)
{
	size_t i;

	if (!lg)
		return;
	for (i = 0; i < lg->n; i++)
		free(lg->entry[i].label);
	free(lg->entry);
	free(lg);
}

size_t fymm_legend_add(struct fymm_legend *lg, const char *label)
{
	struct fymm_legend_entry *ne;
	size_t i;

	if (!lg || !label)
		return (size_t)-1;

	/* one marker per distinct label, so a repeated name reads as one */
	for (i = 0; i < lg->n; i++) {
		if (!strcmp(lg->entry[i].label, label))
			return i;
	}

	if (lg->n == lg->alloc) {
		size_t na = lg->alloc ? lg->alloc * 2 : 8;

		ne = realloc(lg->entry, na * sizeof(*ne));
		if (!ne)
			return (size_t)-1;
		lg->entry = ne;
		lg->alloc = na;
	}

	lg->entry[lg->n].label = strdup(label);
	if (!lg->entry[lg->n].label)
		return (size_t)-1;
	snprintf(lg->entry[lg->n].marker, sizeof(lg->entry[lg->n].marker),
		 "%zu", lg->n + 1);
	lg->n++;
	return lg->n - 1;
}

size_t fymm_legend_find(const struct fymm_legend *lg, const char *label)
{
	size_t i;

	if (!lg || !label)
		return (size_t)-1;
	for (i = 0; i < lg->n; i++) {
		if (!strcmp(lg->entry[i].label, label))
			return i;
	}
	return (size_t)-1;
}

const char *fymm_legend_marker(const struct fymm_legend *lg, size_t idx)
{
	return lg && idx < lg->n ? lg->entry[idx].marker : "";
}

int fymm_legend_color(const struct fymm_legend *lg, size_t idx)
{
	if (!lg || idx >= lg->n)
		return FYMM_PAL_LABEL;
	return (int)(FYMM_PAL_BRANCH0 + idx % 8);
}

size_t fymm_legend_count(const struct fymm_legend *lg)
{
	return lg ? lg->n : 0;
}

int fymm_legend_rows(const struct fymm_legend *lg)
{
	/* a blank row separates the legend from the drawing above it */
	return lg && lg->n ? (int)lg->n + 1 : 0;
}

int fymm_legend_width(const struct fymm_legend *lg)
{
	size_t i;
	int w = 0, n;

	if (!lg)
		return 0;
	for (i = 0; i < lg->n; i++) {
		n = (int)strlen(lg->entry[i].marker) + 2 +
		    fymm_rich_measure(lg->entry[i].label);
		if (n > w)
			w = n;
	}
	return w;
}

int fymm_legend_draw(struct fymm_canvas *cv, int x, int y,
		     const struct fymm_legend *lg)
{
	size_t i;
	int n;

	if (!lg || !lg->n)
		return 0;

	for (i = 0; i < lg->n; i++) {
		fymm_canvas_elem_begin(cv, FYMM_EL_LEGEND, fy_invalid,
				       "legend/%zu", i);
		n = fymm_canvas_text(cv, x, y + 1 + (int)i,
				     lg->entry[i].marker,
				     fymm_legend_color(lg, i),
				     FYMM_ATTR_BOLD);
		n += fymm_canvas_text(cv, x + n, y + 1 + (int)i, " ",
				      FYMM_PAL_LABEL, 0);
		fymm_rich_text(cv, x + n, y + 1 + (int)i, lg->entry[i].label,
			       FYMM_PAL_LABEL, 0);
		fymm_canvas_elem_end(cv);
	}
	return fymm_legend_rows(lg);
}
