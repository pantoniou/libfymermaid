/*
 * fymm-render-ishikawa.c - drawing a cause and effect diagram
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

/*
 * A fishbone is a spine with its categories angled off it. The angle is what
 * character cells cannot draw: two categories a few rows apart would share
 * the same cells. The spine runs down the page instead, each category a rib
 * off it with its causes beneath, which keeps what the diagram is for -- the
 * effect, and what is grouped under what.
 */
char *fymm_render_ishikawa(const struct fymm_diagram *d, fy_generic model,
			   const struct fymm_render_cfg *cfg)
{
	fy_generic categories, category, causes, cause;
	struct fymm_canvas *cv;
	struct fymm_theme theme;
	const char *title, *effect, *name;
	size_t ncategories, ncauses, ci, i;
	int width, height, y, w, color, spine;
	bool ascii;
	char *out;

	if (fymm_theme_resolve(&theme, cfg, fymm_diagram_builder(d), model))
		return NULL;

	categories = fy_get(model, "categories");
	effect = fy_get(model, "effect", (const char *)NULL);
	title = fy_get(model, "title", (const char *)NULL);
	ncategories = fy_is_sequence(categories) ? fy_len(categories) : 0;
	if (!effect)
		return strdup("");

	/* the spine sits past the widest cause */
	spine = 2;
	width = fymm_rich_measure(effect) + 6;
	height = (title ? 2 : 0) + 2;
	for (ci = 0; ci < ncategories; ci++) {
		category = fy_get_at(categories, ci);
		w = fymm_rich_measure(fy_get(category, "name", "")) + spine + 4;
		if (w > width)
			width = w;
		causes = fy_get(category, "causes");
		ncauses = fy_is_sequence(causes) ? fy_len(causes) : 0;
		for (i = 0; i < ncauses; i++) {
			w = fymm_rich_measure(fy_get_at(causes, i, "")) +
			    spine + 7;
			if (w > width)
				width = w;
		}
		height += 1 + (int)ncauses + 1;
	}

	cv = fymm_canvas_create(width + 2, height + 1,
				cfg && cfg->charset != FYMM_CHARSET_AUTO ?
					cfg->charset : FYMM_CHARSET_UNICODE,
				cfg ? cfg->color : FYMM_COLOR_NONE, &theme);
	if (!cv)
		return NULL;
	ascii = cv->charset == FYMM_CHARSET_ASCII;

	y = 0;
	if (title) {
		fymm_rich_text(cv, 0, y, title, FYMM_PAL_TITLE,
			       FYMM_ATTR_BOLD);
		y += 2;
	}

	/* the effect, which is what the whole diagram points at */
	w = fymm_rich_text(cv, spine + 3, y, effect, FYMM_PAL_TITLE,
			   FYMM_ATTR_BOLD);
	fymm_canvas_put(cv, spine, y, ascii ? '#' : 0x25c6, FYMM_PAL_TITLE,
			FYMM_ATTR_BOLD);
	fymm_canvas_hline(cv, y, spine + 1, spine + 1, FYMM_PAL_LABEL, false);
	y++;

	for (ci = 0; ci < ncategories; ci++) {
		category = fy_get_at(categories, ci);
		color = (int)(ci % 8);
		name = fy_get(category, "name", "");
		causes = fy_get(category, "causes");
		ncauses = fy_is_sequence(causes) ? fy_len(causes) : 0;

		/* the rib, joined to the spine */
		/* the spine continues past a rib only while ribs remain */
		fymm_canvas_line(cv, spine, y,
				 (uint8_t)(FYMM_LN_N | FYMM_LN_E |
					   (ci + 1 < ncategories ?
					    FYMM_LN_S : 0)),
				 color, false);
		fymm_canvas_hline(cv, y, spine + 1, spine + 2, color, false);
		fymm_rich_text(cv, spine + 4, y, name, color, FYMM_ATTR_BOLD);
		y++;

		for (i = 0; i < ncauses; i++) {
			cause = fy_get_at(causes, i);
			if (ci + 1 < ncategories)
				fymm_canvas_line(cv, spine, y,
						 FYMM_LN_N | FYMM_LN_S, color,
						 false);
			fymm_canvas_put(cv, spine + 4, y,
					ascii ? '-' : 0x00b7, color, 0);
			fymm_rich_text(cv, spine + 6, y, fy_str(cause),
				       FYMM_COLOR_DEFAULT, 0);
			y++;
		}
		if (ci + 1 < ncategories) {
			fymm_canvas_line(cv, spine, y, FYMM_LN_N | FYMM_LN_S,
					 FYMM_PAL_LABEL, false);
			y++;
		}
	}

	out = fymm_canvas_emit(cv);
	fymm_canvas_destroy(cv);
	return out;
}
