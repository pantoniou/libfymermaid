/*
 * fymm-render-mindmap.c - drawing a mindmap as an indented tree
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
 * Mermaid radiates a mindmap out from its root. A terminal has one useful
 * axis for nesting, so the same tree is drawn indented, the way a directory
 * listing is: the shape of the hierarchy survives, and a long label does not
 * have to fit in a box.
 *
 * The node shape becomes the bullet, which is the one part of a mermaid shape
 * that carries meaning at this size.
 */
static uint32_t mm_bullet(enum fymm_charset cs, const char *shape)
{
	if (cs == FYMM_CHARSET_ASCII) {
		if (!strcmp(shape, "square"))
			return '#';
		if (!strcmp(shape, "circle"))
			return 'O';
		if (!strcmp(shape, "bang"))
			return '!';
		if (!strcmp(shape, "cloud"))
			return '~';
		if (!strcmp(shape, "hexagon"))
			return '<';
		if (!strcmp(shape, "rounded"))
			return 'o';
		return '*';
	}
	if (!strcmp(shape, "square"))
		return 0x25aa;		/* U+25AA black small square */
	if (!strcmp(shape, "circle"))
		return 0x25c9;		/* U+25C9 fisheye */
	if (!strcmp(shape, "bang"))
		return 0x2739;		/* U+2739 twelve pointed star */
	if (!strcmp(shape, "cloud"))
		return 0x25cc;		/* U+25CC dotted circle */
	if (!strcmp(shape, "hexagon"))
		return 0x2b21;		/* U+2B21 white hexagon */
	if (!strcmp(shape, "rounded"))
		return 0x25cf;		/* U+25CF black circle */
	return 0x00b7;			/* U+00B7 middle dot */
}

/* Count the rows a subtree occupies. */
static int mm_rows(fy_generic node)
{
	fy_generic children, child;
	int rows = 1;

	children = fy_get(node, "children");
	if (fy_is_sequence(children)) {
		fy_foreach(child, children)
			rows += mm_rows(child);
	}
	return rows;
}

/* The width a subtree needs, given the column it starts at. */
static int mm_width(fy_generic node, int depth)
{
	fy_generic children, child;
	const char *icon, *cls;
	int w, best;

	w = depth * 3 + 2 + fymm_rich_measure(fy_get(node, "text", ""));
	icon = fy_get(node, "icon", (const char *)NULL);
	if (icon)
		w += fymm_rich_measure(icon) + 3;
	cls = fy_get(node, "class", (const char *)NULL);
	if (cls)
		w += fymm_rich_measure(cls) + 3;

	best = w;
	children = fy_get(node, "children");
	if (fy_is_sequence(children)) {
		fy_foreach(child, children) {
			w = mm_width(child, depth + 1);
			if (w > best)
				best = w;
		}
	}
	return best;
}

/*
 * Draw @node and its subtree. @y is the row it lands on; the return value is
 * the row after the subtree. @last says whether the node is the final child
 * of its parent, which decides the elbow glyph and whether the parent's
 * vertical rail continues past it.
 */
static int mm_draw(struct fymm_canvas *cv, fy_generic node, int x, int y,
		   int depth, bool last, bool *rails)
{
	fy_generic children, child;
	const char *icon, *cls;
	size_t nchildren, i;
	int col, tx, d;

	col = depth * 3;

	/* the rails of every ancestor that still has children below */
	for (d = 0; d < depth - 1; d++) {
		if (rails[d])
			fymm_canvas_line(cv, x + d * 3, y,
					 FYMM_LN_N | FYMM_LN_S,
					 FYMM_PAL_LABEL, false);
	}

	/* the elbow into this node */
	if (depth > 0) {
		fymm_canvas_line(cv, x + col - 3, y,
				 (uint8_t)(FYMM_LN_N | FYMM_LN_E |
					   (last ? 0 : FYMM_LN_S)),
				 FYMM_PAL_LABEL, false);
		fymm_canvas_line(cv, x + col - 2, y, FYMM_LN_W | FYMM_LN_E,
				 FYMM_PAL_LABEL, false);
	}

	tx = x + col;
	fymm_canvas_put(cv, tx, y, mm_bullet(cv->charset,
					     fy_get(node, "shape", "default")),
			depth % 8, depth ? 0 : FYMM_ATTR_BOLD);
	tx += 2;
	tx += fymm_rich_text(cv, tx, y, fy_get(node, "text", ""),
			     depth ? FYMM_COLOR_DEFAULT : FYMM_PAL_TITLE,
			     depth ? 0 : FYMM_ATTR_BOLD);

	icon = fy_get(node, "icon", (const char *)NULL);
	if (icon) {
		tx += fymm_canvas_text(cv, tx + 1, y, "[", FYMM_PAL_TAG, 0) + 1;
		tx += fymm_canvas_text(cv, tx, y, icon, FYMM_PAL_TAG, 0);
		tx += fymm_canvas_text(cv, tx, y, "]", FYMM_PAL_TAG, 0);
	}
	cls = fy_get(node, "class", (const char *)NULL);
	if (cls) {
		tx += fymm_canvas_text(cv, tx + 1, y, ":", FYMM_PAL_LABEL,
				       FYMM_ATTR_DIM) + 1;
		fymm_canvas_text(cv, tx, y, cls, FYMM_PAL_LABEL, FYMM_ATTR_DIM);
	}
	y++;

	children = fy_get(node, "children");
	nchildren = fy_is_sequence(children) ? fy_len(children) : 0;
	for (i = 0; i < nchildren; i++) {
		child = fy_get_at(children, i);
		/* this node's rail continues while a sibling follows */
		if (depth < FYMM_MINDMAP_MAX_DEPTH)
			rails[depth] = i + 1 < nchildren;
		y = mm_draw(cv, child, x, y, depth + 1, i + 1 == nchildren,
			    rails);
	}
	return y;
}

struct fymm_canvas *fymm_render_mindmap(const struct fymm_diagram *d,
					fy_generic model,
					const struct fymm_render_cfg *cfg)
{
	bool rails[FYMM_MINDMAP_MAX_DEPTH + 1] = { 0 };
	struct fymm_canvas *cv = NULL;
	struct fymm_theme theme;
	const char *title;
	fy_generic root;
	int width, height, top;

	if (fymm_theme_resolve(&theme, cfg, fymm_diagram_builder(d), model))
		return NULL;

	root = fy_get(model, "root");
	if (!fy_is_mapping(root))
		return fymm_canvas_empty(cfg);
	title = fy_get(model, "title", (const char *)NULL);

	top = title ? 2 : 0;
	width = mm_width(root, 0) + 2;
	if (title && fymm_rich_measure(title) + 2 > width)
		width = fymm_rich_measure(title) + 2;
	height = top + mm_rows(root);

	cv = fymm_canvas_create_cfg(width, height, cfg, &theme);
	if (!cv)
		return NULL;

	if (title)
		fymm_rich_text(cv, 0, 0, title, FYMM_PAL_TITLE,
				 FYMM_ATTR_BOLD);

	mm_draw(cv, root, 1, top, 0, true, rails);

	return cv;
}
