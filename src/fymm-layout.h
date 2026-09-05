/*
 * fymm-layout.h - the layered graph layout the graph renderers share
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

#ifndef FYMM_LAYOUT_H
#define FYMM_LAYOUT_H

#include <stdbool.h>
#include <stddef.h>

/*
 * struct fymm_lnode - one node of a graph to lay out
 *
 * The caller fills in @id, @w and @h; the layout fills in the rest.
 *
 * @id: what the edges name this node by
 * @w: the width of the drawn node, in cells
 * @h: its height, in rows
 * @rank: how far down the graph the node sits
 * @x: the leftmost cell of the node
 * @y: its top row
 */
struct fymm_lnode {
	const char *id;
	int w, h;
	int rank;
	int x, y;
};

/*
 * struct fymm_ledge - one edge of a graph to lay out
 *
 * The caller fills in @from and @to; the layout fills in @back.
 *
 * @from: the index of the node the edge leaves
 * @to: the index of the node it arrives at
 * @back: set when the edge closes a cycle, so ranking ignored it and the
 *        renderer has to route it as a return
 */
struct fymm_ledge {
	size_t from, to;
	bool back;
};

/*
 * struct fymm_layout - the result
 *
 * @nranks: how many ranks the graph needed
 * @width: the cells the widest rank occupies
 * @height: the rows every rank occupies together
 * @rank_gap: the rows left between two ranks, as asked for
 */
struct fymm_layout {
	int nranks;
	int width;
	int height;
	int rank_gap;
};

/*
 * Lay a graph out in ranks running down the page.
 *
 * Each node is placed one rank below its deepest predecessor, ignoring the
 * edges that close a cycle; those are marked in @edges so that the renderer
 * can draw them as returns. Within a rank the nodes keep the order they were
 * given, and a narrow rank is centred under the widest one.
 *
 * @top: the first row the graph may use
 * @col_gap: the cells left between two nodes of a rank
 * @rank_gap: the rows left between two ranks
 *
 * Returns 0, or -1 when it cannot allocate.
 */
int fymm_layout_layered(struct fymm_lnode *nodes, size_t nnodes,
			struct fymm_ledge *edges, size_t nedges, int top,
			int col_gap, int rank_gap, struct fymm_layout *out);

/* The index of @id in @nodes, or (size_t)-1. */
size_t fymm_layout_find(const struct fymm_lnode *nodes, size_t nnodes,
			const char *id);

#endif /* FYMM_LAYOUT_H */
