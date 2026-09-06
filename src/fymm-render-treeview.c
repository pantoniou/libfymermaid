/*
 * fymm-render-treeview.c - drawing a file tree
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

/* how deeply the rails are tracked */
#define TV_MAX_DEPTH 64

/*
 * The tree a reader already knows from `tree` and from a file browser: a rail
 * per open level, an elbow into each entry, and the comment beside it.
 */
char *fymm_render_treeview(const struct fymm_diagram *d, fy_generic model,
			   const struct fymm_render_cfg *cfg)
{
	bool rails[TV_MAX_DEPTH] = { 0 };
	fy_generic entries, entry;
	struct fymm_canvas *cv;
	struct fymm_theme theme;
	const char *title, *name, *comment;
	size_t nentries, i, j;
	int width, height, top, x, y, w, depth, next_depth;
	bool ascii, last;
	char *out;

	if (fymm_theme_resolve(&theme, cfg, fymm_diagram_builder(d), model))
		return NULL;

	entries = fy_get(model, "entries");
	title = fy_get(model, "title", (const char *)NULL);
	nentries = fy_is_sequence(entries) ? fy_len(entries) : 0;
	if (!nentries)
		return strdup("");

	width = 0;
	for (i = 0; i < nentries; i++) {
		entry = fy_get_at(entries, i);
		depth = (int)fy_get(entry, "depth", 0LL);
		w = depth * 3 + fymm_rich_measure(fy_get(entry, "name", "")) + 4;
		comment = fy_get(entry, "comment", (const char *)NULL);
		if (comment)
			w += fymm_rich_measure(comment) + 3;
		if (w > width)
			width = w;
	}
	top = title ? 2 : 0;
	height = top + (int)nentries;

	cv = fymm_canvas_create_cfg(width + 2, height, cfg, &theme);
	if (!cv)
		return NULL;
	ascii = cv->charset == FYMM_CHARSET_ASCII;

	if (title)
		fymm_rich_text(cv, 0, 0, title, FYMM_PAL_TITLE,
			       FYMM_ATTR_BOLD);

	for (i = 0; i < nentries; i++) {
		entry = fy_get_at(entries, i);
		depth = (int)fy_get(entry, "depth", 0LL);
		name = fy_get(entry, "name", "");
		y = top + (int)i;

		/* an entry is the last of its level when nothing at that same
		 * level follows before the level closes */
		last = true;
		for (j = i + 1; j < nentries; j++) {
			next_depth = (int)fy_get(fy_get_at(entries, j), "depth",
						 0LL);
			if (next_depth < depth)
				break;
			if (next_depth == depth) {
				last = false;
				break;
			}
		}

		/* the rails of the levels still open above this one */
		for (x = 0; x < depth - 1 && x < TV_MAX_DEPTH; x++) {
			if (rails[x])
				fymm_canvas_line(cv, x * 3, y,
						 FYMM_LN_N | FYMM_LN_S,
						 FYMM_PAL_LABEL, false);
		}
		if (depth > 0) {
			fymm_canvas_line(cv, (depth - 1) * 3, y,
					 (uint8_t)(FYMM_LN_N | FYMM_LN_E |
						   (last ? 0 : FYMM_LN_S)),
					 FYMM_PAL_LABEL, false);
			fymm_canvas_line(cv, (depth - 1) * 3 + 1, y,
					 FYMM_LN_W | FYMM_LN_E, FYMM_PAL_LABEL,
					 false);
		}
		if (depth > 0 && depth - 1 < TV_MAX_DEPTH)
			rails[depth - 1] = !last;

		x = depth * 3;
		w = fymm_rich_text(cv, x, y, name,
				   fy_get(entry, "directory", false) ?
					(int)(depth % 8) : FYMM_COLOR_DEFAULT,
				   fy_get(entry, "directory", false) ?
					FYMM_ATTR_BOLD : 0);

		comment = fy_get(entry, "comment", (const char *)NULL);
		if (comment && *comment) {
			fymm_canvas_text(cv, x + w + 1, y, ascii ? "#" : "·",
					 FYMM_PAL_LABEL, FYMM_ATTR_DIM);
			fymm_rich_text(cv, x + w + 3, y, comment,
				       FYMM_PAL_LABEL, FYMM_ATTR_DIM);
		}
	}

	out = fymm_canvas_emit(cv);
	fymm_canvas_destroy(cv);
	return out;
}
