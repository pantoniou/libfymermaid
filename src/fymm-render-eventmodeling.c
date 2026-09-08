/*
 * fymm-render-eventmodeling.c - drawing an event model as its frames
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

/* The mark a frame kind is drawn with, and the column it sits in. */
static int em_lane_of(const char *kind)
{
	if (!strcmp(kind, "ui") || !strcmp(kind, "view"))
		return 0;
	if (!strcmp(kind, "command") || !strcmp(kind, "trigger"))
		return 1;
	if (!strcmp(kind, "event"))
		return 2;
	return 3;
}

/*
 * An event model reads as a sequence of frames, each in the lane its kind
 * belongs to: what the user touches, what that commands, what happened as a
 * result, and what processes it. Laying the lanes out as columns keeps that
 * reading, and the frames stay in the order they were written.
 */
struct fymm_canvas *fymm_render_eventmodeling(const struct fymm_diagram *d,
					      fy_generic model,
					      const struct fymm_render_cfg *cfg)
{
	static const char *const lane_names[4] = {
		"interface", "command", "event", "processor",
	};
	fy_generic frames, frame, follows;
	struct fymm_canvas *cv = NULL;
	struct fymm_theme theme;
	const char *title, *name, *ref;
	size_t nframes, i;
	int width, height, top, y, w, lane, colw[4] = { 0, 0, 0, 0 };
	int colx[4], x;
	bool ascii;
	char buf[128];

	if (fymm_theme_resolve(&theme, cfg, fymm_diagram_builder(d), model))
		return NULL;

	frames = fy_get(model, "frames");
	title = fy_get(model, "title", (const char *)NULL);
	nframes = fy_is_sequence(frames) ? fy_len(frames) : 0;
	if (!nframes)
		return fymm_canvas_empty(cfg);

	for (i = 0; i < 4; i++)
		colw[i] = fymm_text_width(lane_names[i]) + 2;
	for (i = 0; i < nframes; i++) {
		frame = fy_get_at(frames, i);
		lane = em_lane_of(fy_get(frame, "kind", ""));
		snprintf(buf, sizeof(buf), "%s %s",
			 fy_get(frame, "number", ""),
			 fy_get(frame, "name", ""));
		w = fymm_rich_measure(buf) + 3;
		if (w > colw[lane])
			colw[lane] = w;
	}

	x = 0;
	for (i = 0; i < 4; i++) {
		colx[i] = x;
		x += colw[i] + 2;
	}
	width = x;
	top = (title ? 2 : 0) + 2;
	height = top + (int)nframes;

	cv = fymm_canvas_create_cfg(width + 2, height, cfg, &theme);
	if (!cv)
		return NULL;
	ascii = cv->charset == FYMM_CHARSET_ASCII;

	if (title)
		fymm_rich_text(cv, 0, 0, title, FYMM_PAL_TITLE,
			       FYMM_ATTR_BOLD);

	/* the lane headings */
	for (i = 0; i < 4; i++) {
		fymm_canvas_text(cv, colx[i], top - 2, lane_names[i],
				 (int)(i % 8), FYMM_ATTR_BOLD);
		fymm_canvas_hline(cv, top - 1, colx[i],
				  colx[i] + colw[i] - 1, (int)(i % 8), false);
	}

	for (i = 0; i < nframes; i++) {
		frame = fy_get_at(frames, i);
		lane = em_lane_of(fy_get(frame, "kind", ""));
		name = fy_get(frame, "name", "");
		y = top + (int)i;

		fymm_canvas_put(cv, colx[lane], y, ascii ? '*' : 0x25cf, lane,
				FYMM_ATTR_BOLD);
		snprintf(buf, sizeof(buf), "%s %s", fy_get(frame, "number", ""),
			 name);
		w = fymm_rich_text(cv, colx[lane] + 2, y, buf,
				   FYMM_COLOR_DEFAULT, 0);

		/* what it carries, and what it follows */
		x = colx[lane] + 2 + w;
		ref = fy_get(frame, "ref", (const char *)NULL);
		if (ref)
			x += fymm_canvas_text(cv, x + 1, y, ref, FYMM_PAL_TAG,
					      0) + 1;
		follows = fy_get(frame, "follows");
		if (fy_is_sequence(follows) && fy_len(follows)) {
			size_t k;

			x += fymm_canvas_text(cv, x + 1, y,
					      ascii ? "<-" : "←",
					      FYMM_PAL_LABEL, FYMM_ATTR_DIM) + 1;
			for (k = 0; k < fy_len(follows); k++)
				x += fymm_canvas_text(cv, x + 1, y,
						      fy_get_at(follows, k, ""),
						      FYMM_PAL_LABEL,
						      FYMM_ATTR_DIM) + 1;
		}
	}

	return cv;
}
