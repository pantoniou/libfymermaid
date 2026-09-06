/*
 * fymm-frame.h - the container frames a ranked drawing sits inside
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

#ifndef FYMM_FRAME_H
#define FYMM_FRAME_H

#include "fymm-canvas.h"
#include "fymm-markdown.h"

/*
 * A container drawn around the nodes it holds: a flowchart subgraph, an
 * agentflow flow, a composite state.
 *
 * The layered layout places each rank without regard to the frames another
 * rank's nodes sit inside, so a node a container does not hold can land inside
 * the rectangle that covers the ones it does. Measuring pushes such a node to
 * the right and reports that it must be measured again; a node only ever moves
 * right, so this settles.
 */
struct fymm_frame {
	const char *id;
	int depth;		/* 0 for an outermost container */
	int tw;			/* the drawn width of the title, 0 for none */
	int x0, y0, x1, y1;
	bool used;		/* it holds at least one node */
	bool framed;		/* and can be drawn without claiming another */
};

/* the cells a frame keeps clear around what it holds, in both axes */
#define FYMM_FRAME_PAD 2

/*
 * What measuring needs to know about a node. Each renderer keeps its own node
 * type, so it fills one of these for the frames and takes @x back afterwards:
 * measuring moves a node that a frame would wrongly enclose.
 */
struct fymm_frame_node {
	int x, y, w, h;
	int rank;
	size_t group;		/* innermost container, or (size_t)-1 */
};

/*
 * Which container each node sits in, innermost first, indexed like @node;
 * (size_t)-1 for a node in none. A container that holds only other containers
 * holds their nodes too.
 */
void fymm_frames_mark(struct fymm_frame *fr, size_t nfr,
		      const struct fymm_frame_node *node, size_t nnodes);

/*
 * Fill @key with the sort key that puts the nodes of one container together,
 * which is what keeps them contiguous within a rank: each node keys on the
 * first node of its outermost container, and a node in none keys on itself.
 * The caller sorts its own nodes by it, stably, before laying out.
 */
void fymm_frames_order(const struct fymm_frame *fr, size_t nfr,
		       size_t *key, const struct fymm_frame_node *node,
		       size_t nnodes);

/*
 * Measure every frame from the nodes it holds and the frames nested in it.
 * Returns true when a node was moved, so the caller measures again; eight
 * passes is more than enough for any drawing that settles at all.
 */
bool fymm_frames_measure(struct fymm_frame *fr, size_t nfr,
			 struct fymm_frame_node *node, size_t nnodes,
			 int col_gap);

/*
 * The cells available for a run of text starting at (@x, @y) before it would
 * reach a frame edge, or -1 when no frame is in the way. A label is a literal
 * glyph and a frame is a line, so a label drawn over an edge takes the cell
 * and leaves a hole in the frame.
 */
int fymm_frames_room(const struct fymm_frame *fr, size_t nfr, int x, int y);

/* Draw the frames, dashed and rounded so they do not read as another node. */
void fymm_frames_draw(struct fymm_canvas *cv, const struct fymm_frame *fr,
		      size_t nfr, struct fymm_rich *const *title);

#endif /* FYMM_FRAME_H */
