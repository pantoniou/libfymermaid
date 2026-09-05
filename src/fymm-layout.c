/*
 * fymm-layout.c - the layered graph layout the graph renderers share
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

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "fymm-layout.h"

size_t fymm_layout_find(const struct fymm_lnode *nodes, size_t nnodes,
			const char *id)
{
	size_t i;

	if (!id)
		return (size_t)-1;
	for (i = 0; i < nnodes; i++) {
		if (nodes[i].id && !strcmp(nodes[i].id, id))
			return i;
	}
	return (size_t)-1;
}

/*
 * Mark the edges that close a cycle. A depth-first walk meets a back edge
 * when it reaches a node already on its own stack; ranking such an edge would
 * push its target below itself for ever.
 *
 * @state is 0 for unvisited, 1 for on the stack and 2 for done.
 */
static void fymm_mark_back(struct fymm_ledge *edges, size_t nedges,
			   uint8_t *state, size_t v)
{
	size_t i;

	state[v] = 1;
	for (i = 0; i < nedges; i++) {
		if (edges[i].from != v || edges[i].back)
			continue;
		if (state[edges[i].to] == 1)
			edges[i].back = true;
		else if (!state[edges[i].to])
			fymm_mark_back(edges, nedges, state, edges[i].to);
	}
	state[v] = 2;
}

int fymm_layout_layered(struct fymm_lnode *nodes, size_t nnodes,
			struct fymm_ledge *edges, size_t nedges, int top,
			int col_gap, int rank_gap, struct fymm_layout *out)
{
	uint8_t *state;
	size_t i, passes;
	int r, x, y, used, tall;
	bool moved;

	memset(out, 0, sizeof(*out));
	out->rank_gap = rank_gap;
	if (!nnodes)
		return 0;

	state = calloc(nnodes, sizeof(*state));
	if (!state)
		return -1;

	for (i = 0; i < nedges; i++) {
		nodes[edges[i].from].rank = nodes[edges[i].from].rank;
		if (edges[i].from >= nnodes || edges[i].to >= nnodes)
			edges[i].back = true;	/* nothing to rank */
	}
	for (i = 0; i < nnodes; i++) {
		if (!state[i])
			fymm_mark_back(edges, nedges, state, i);
	}
	free(state);

	/* longest-path layering over the edges that remain */
	moved = true;
	for (passes = 0; moved && passes <= nnodes; passes++) {
		moved = false;
		for (i = 0; i < nedges; i++) {
			if (edges[i].back || edges[i].from == edges[i].to)
				continue;
			if (nodes[edges[i].to].rank <=
			    nodes[edges[i].from].rank) {
				nodes[edges[i].to].rank =
					nodes[edges[i].from].rank + 1;
				moved = true;
			}
		}
	}

	for (i = 0; i < nnodes; i++) {
		if (nodes[i].rank + 1 > out->nranks)
			out->nranks = nodes[i].rank + 1;
	}

	/* place each rank left to right, then centre the narrow ones */
	y = top;
	for (r = 0; r < out->nranks; r++) {
		x = 0;
		tall = 0;
		for (i = 0; i < nnodes; i++) {
			if (nodes[i].rank != r)
				continue;
			nodes[i].x = x;
			nodes[i].y = y;
			x += nodes[i].w + col_gap;
			if (nodes[i].h > tall)
				tall = nodes[i].h;
		}
		used = x - col_gap;
		if (used > out->width)
			out->width = used;
		y += tall + rank_gap;
	}
	out->height = y - rank_gap - top;

	for (r = 0; r < out->nranks; r++) {
		used = 0;
		for (i = 0; i < nnodes; i++) {
			if (nodes[i].rank == r)
				used = nodes[i].x + nodes[i].w;
		}
		for (i = 0; i < nnodes; i++) {
			if (nodes[i].rank == r)
				nodes[i].x += (out->width - used) / 2;
		}
	}
	return 0;
}
