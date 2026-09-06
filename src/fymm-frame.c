/*
 * fymm-frame.c - the container frames a ranked drawing sits inside
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

#include <stdlib.h>
#include <string.h>

#include "fymm-frame.h"
#include "fymm-markdown.h"

/*
 * Does @g hold the node whose innermost container is @inner, at any depth?
 *
 * A container is entered by the nodes between its header and its end, so the
 * containers open at that point are the ones the node belongs to. Only the
 * innermost is recorded, and the enclosing ones are the entries before it that
 * are still open: those of a smaller depth that come earlier in the list.
 */
static bool frame_holds(const struct fymm_frame *fr, size_t g, size_t inner)
{
	size_t k;
	int depth;

	if (inner == (size_t)-1)
		return false;
	if (inner == g)
		return true;
	if (fr[g].depth >= fr[inner].depth || g > inner)
		return false;

	depth = fr[inner].depth;
	for (k = inner; k-- > 0; ) {
		if (fr[k].depth >= depth)
			continue;
		depth = fr[k].depth;
		if (k == g)
			return true;
		if (!depth)
			break;
	}
	return false;
}

/* The outermost container @inner sits in, which may be @inner itself. */
static size_t frame_outermost(const struct fymm_frame *fr, size_t inner)
{
	size_t k, out = inner;
	int depth;

	if (inner == (size_t)-1)
		return inner;
	depth = fr[inner].depth;
	for (k = inner; depth && k-- > 0; ) {
		if (fr[k].depth >= depth)
			continue;
		depth = fr[k].depth;
		out = k;
	}
	return out;
}

void fymm_frames_mark(struct fymm_frame *fr, size_t nfr,
		      const struct fymm_frame_node *node, size_t nnodes)
{
	size_t i, g;

	for (i = 0; i < nnodes; i++) {
		for (g = 0; g < nfr; g++) {
			if (frame_holds(fr, g, node[i].group))
				fr[g].used = true;
		}
	}
}

void fymm_frames_order(const struct fymm_frame *fr, size_t nfr,
		       size_t *key, const struct fymm_frame_node *node,
		       size_t nnodes)
{
	size_t i, j, g;

	(void)nfr;
	for (i = 0; i < nnodes; i++) {
		g = frame_outermost(fr, node[i].group);

		key[i] = i;
		for (j = 0; g != (size_t)-1 && j < i; j++) {
			if (frame_outermost(fr, node[j].group) != g)
				continue;
			key[i] = key[j];
			break;
		}
	}
}

bool fymm_frames_measure(struct fymm_frame *fr, size_t nfr,
			 struct fymm_frame_node *node, size_t nnodes,
			 int col_gap)
{
	size_t g, i, j, k;
	bool moved = false;

	for (g = nfr; g-- > 0; ) {
		bool first = true;

		if (!fr[g].used)
			continue;
		fr[g].framed = true;

		for (i = 0; i < nnodes; i++) {
			if (!frame_holds(fr, g, node[i].group))
				continue;
			if (first) {
				fr[g].x0 = node[i].x;
				fr[g].y0 = node[i].y;
				fr[g].x1 = node[i].x + node[i].w - 1;
				fr[g].y1 = node[i].y + node[i].h - 1;
				first = false;
				continue;
			}
			if (node[i].x < fr[g].x0)
				fr[g].x0 = node[i].x;
			if (node[i].y < fr[g].y0)
				fr[g].y0 = node[i].y;
			if (node[i].x + node[i].w - 1 > fr[g].x1)
				fr[g].x1 = node[i].x + node[i].w - 1;
			if (node[i].y + node[i].h - 1 > fr[g].y1)
				fr[g].y1 = node[i].y + node[i].h - 1;
		}
		if (first) {
			fr[g].used = false;
			continue;
		}

		/* the containers nested in this one follow it in the list */
		for (k = g + 1; k < nfr; k++) {
			if (!fr[k].used || fr[k].depth <= fr[g].depth)
				break;
			if (fr[k].x0 - 1 < fr[g].x0)
				fr[g].x0 = fr[k].x0 - 1;
			if (fr[k].y0 - 1 < fr[g].y0)
				fr[g].y0 = fr[k].y0 - 1;
			if (fr[k].x1 + 1 > fr[g].x1)
				fr[g].x1 = fr[k].x1 + 1;
			if (fr[k].y1 + 1 > fr[g].y1)
				fr[g].y1 = fr[k].y1 + 1;
		}

		fr[g].x0 -= FYMM_FRAME_PAD;
		fr[g].y0 -= FYMM_FRAME_PAD;
		fr[g].x1 += FYMM_FRAME_PAD;
		fr[g].y1 += FYMM_FRAME_PAD;

		/* the title sits on the top edge, between the corners */
		if (fr[g].tw && fr[g].x0 + fr[g].tw + 4 > fr[g].x1)
			fr[g].x1 = fr[g].x0 + fr[g].tw + 4;

		for (i = 0; i < nnodes; i++) {
			int shift;

			if (frame_holds(fr, g, node[i].group))
				continue;
			if (node[i].x + node[i].w - 1 < fr[g].x0 ||
			    node[i].x > fr[g].x1 ||
			    node[i].y + node[i].h - 1 < fr[g].y0 ||
			    node[i].y > fr[g].y1)
				continue;

			/* a node that starts left of the frame cannot be
			 * pushed clear of it without crossing it */
			if (node[i].x < fr[g].x0) {
				fr[g].framed = false;
				continue;
			}

			/*
			 * Past the frame, and past everything else in the
			 * rank that was already clear of it. A node the
			 * container holds never moves: the frame covers it,
			 * so clearing the frame clears it too.
			 */
			shift = fr[g].x1 + 1 + col_gap / 2 - node[i].x;
			for (j = 0; j < nnodes; j++) {
				if (node[j].rank != node[i].rank ||
				    node[j].x < node[i].x ||
				    frame_holds(fr, g, node[j].group))
					continue;
				node[j].x += shift;
			}
			moved = true;
		}
	}
	return moved;
}

int fymm_frames_room(const struct fymm_frame *fr, size_t nfr, int x, int y)
{
	size_t g;
	int room = -1;

	for (g = 0; g < nfr; g++) {
		if (!fr[g].used || !fr[g].framed)
			continue;
		if (y < fr[g].y0 || y > fr[g].y1 || x > fr[g].x1)
			continue;
		/* the edge the run reaches first: the right edge of a frame
		 * it starts inside, or the left edge of one it starts before */
		if (x >= fr[g].x0) {
			if (room < 0 || fr[g].x1 - x < room)
				room = fr[g].x1 - x;
		} else if (room < 0 || fr[g].x0 - x < room) {
			room = fr[g].x0 - x;
		}
	}
	return room;
}

void fymm_frames_draw(struct fymm_canvas *cv, const struct fymm_frame *fr,
		      size_t nfr, struct fymm_rich *const *title)
{
	bool ascii = cv->charset == FYMM_CHARSET_ASCII;
	size_t g;
	int color;

	for (g = 0; g < nfr; g++) {
		if (!fr[g].used)
			continue;
		color = FYMM_PAL_BRANCH0 + (fr[g].depth % 8);

		if (fr[g].framed) {
			fymm_canvas_hline(cv, fr[g].y0, fr[g].x0 + 1,
					  fr[g].x1 - 1, color, true);
			fymm_canvas_hline(cv, fr[g].y1, fr[g].x0 + 1,
					  fr[g].x1 - 1, color, true);
			fymm_canvas_vline(cv, fr[g].x0, fr[g].y0 + 1,
					  fr[g].y1 - 1, color, true);
			fymm_canvas_vline(cv, fr[g].x1, fr[g].y0 + 1,
					  fr[g].y1 - 1, color, true);
			fymm_canvas_put(cv, fr[g].x0, fr[g].y0,
					ascii ? '+' : 0x256d, color, 0);
			fymm_canvas_put(cv, fr[g].x1, fr[g].y0,
					ascii ? '+' : 0x256e, color, 0);
			fymm_canvas_put(cv, fr[g].x0, fr[g].y1,
					ascii ? '+' : 0x2570, color, 0);
			fymm_canvas_put(cv, fr[g].x1, fr[g].y1,
					ascii ? '+' : 0x256f, color, 0);
		}

		/* the top edge is a row a frame owns and a node never uses */
		if (fr[g].tw && title[g])
			fymm_rich_draw_line(cv, fr[g].x0 + 2, fr[g].y0,
					    title[g], 0, color,
					    FYMM_ATTR_BOLD);
	}
}
