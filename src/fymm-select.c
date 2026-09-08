/*
 * fymm-select.c - hit testing and spatial navigation over a render
 *
 * Copyright (c) 2026 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdbool.h>
#include <stddef.h>

#include "fymm-internal.h"

/* Is the element on the screen at all? A clipped element keeps its path and
 * loses its place, and nothing can be aimed at it. */
static bool fymm_elem_placed(const struct fymm_element *e)
{
	return e->width > 0 && e->height > 0;
}

static bool fymm_elem_covers(const struct fymm_element *e, int row, int col)
{
	return fymm_elem_placed(e) &&
	       row >= e->row && row < e->row + e->height &&
	       col >= e->col && col < e->col + e->width;
}

const struct fymm_element *fymm_hit_test(const struct fymm_render_result *r,
					 int row, int col)
{
	const struct fymm_element *e, *best = NULL;
	size_t i, count;

	count = fymm_render_result_count(r);
	for (i = 0; i < count; i++) {
		e = fymm_render_result_element(r, i);
		if (!fymm_elem_covers(e, row, col))
			continue;
		/* the smaller element wins, so a label inside a node answers
		 * for the cells it covers and the node for the rest */
		if (best && best->width * best->height <= e->width * e->height)
			continue;
		best = e;
	}
	return best;
}

/*
 * How well a move from @s arrives at @e. A move is answered in three tiers,
 * and a candidate in a lower tier always wins:
 *
 *   0: @e overlaps @s across the move, so it is straight ahead;
 *   1: @e lies more along the move than across it;
 *   2: @e is anywhere ahead at all.
 *
 * The tiers keep the natural answer where there is one and keep every
 * element reachable where there is not: a diagram whose ranks are offset,
 * a gitGraph among them, has no element straight above another, and a key
 * that answered nothing there would strand the lanes.
 *
 * Within a tier the distance along the move decides, and the distance across
 * it breaks a tie. The coordinates are doubled, so the centre of a box of any
 * size is a whole number.
 *
 * Returns: the cost, or -1 when the move does not arrive at @e.
 */
static long fymm_move_cost(const struct fymm_element *s,
			   const struct fymm_element *e,
			   enum fymm_direction dir)
{
	int sr = s->row * 2 + s->height, sc = s->col * 2 + s->width;
	int er = e->row * 2 + e->height, ec = e->col * 2 + e->width;
	int along, across, tier;
	bool overlap;

	switch (dir) {
	case FYMM_DIR_UP:
		along = sr - er;
		across = ec - sc;
		break;
	case FYMM_DIR_DOWN:
		along = er - sr;
		across = ec - sc;
		break;
	case FYMM_DIR_LEFT:
		along = sc - ec;
		across = er - sr;
		break;
	default:
		along = ec - sc;
		across = er - sr;
		break;
	}
	/* the candidate must clear the source along the move: two elements
	 * that share a row are not above one another, whatever their
	 * centres say */
	switch (dir) {
	case FYMM_DIR_UP:
		if (e->row + e->height > s->row)
			return -1;
		break;
	case FYMM_DIR_DOWN:
		if (e->row < s->row + s->height)
			return -1;
		break;
	case FYMM_DIR_LEFT:
		if (e->col + e->width > s->col)
			return -1;
		break;
	default:
		if (e->col < s->col + s->width)
			return -1;
		break;
	}
	if (along <= 0)
		return -1;
	if (across < 0)
		across = -across;

	if (dir == FYMM_DIR_UP || dir == FYMM_DIR_DOWN)
		overlap = e->col < s->col + s->width &&
			  s->col < e->col + e->width;
	else
		overlap = e->row < s->row + s->height &&
			  s->row < e->row + e->height;

	tier = overlap ? 0 : (along >= across ? 1 : 2);
	return (long)tier * 0x1000000 + (long)along * 16 + across;
}

const struct fymm_element *fymm_navigate(const struct fymm_render_result *r,
					 const char *from,
					 enum fymm_direction dir)
{
	const struct fymm_element *s, *e, *best = NULL;
	long cost, best_cost = 0;
	size_t i, count, at = 0;

	count = fymm_render_result_count(r);
	if (!count)
		return NULL;

	s = fymm_render_result_find(r, from);
	if (!s || !fymm_elem_placed(s)) {
		for (i = 0; i < count; i++) {
			e = fymm_render_result_element(r, i);
			if (fymm_elem_placed(e))
				return e;
		}
		return NULL;
	}

	for (i = 0; i < count; i++) {
		if (fymm_render_result_element(r, i) == s) {
			at = i;
			break;
		}
	}

	if (dir == FYMM_DIR_NEXT || dir == FYMM_DIR_PREV) {
		for (i = 1; i < count; i++) {
			size_t j = dir == FYMM_DIR_NEXT ?
				   (at + i) % count :
				   (at + count - i) % count;

			e = fymm_render_result_element(r, j);
			if (fymm_elem_placed(e))
				return e;
		}
		return NULL;
	}

	for (i = 0; i < count; i++) {
		e = fymm_render_result_element(r, i);
		if (e == s || !fymm_elem_placed(e))
			continue;
		cost = fymm_move_cost(s, e, dir);
		if (cost < 0)
			continue;
		if (best && cost >= best_cost)
			continue;
		best = e;
		best_cost = cost;
	}
	return best;
}
