/*
 * fymm-block.c - the block diagram statement parser and model builder
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

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "fymm-internal.h"

/* how deeply compound blocks may nest */
#define BK_MAX_DEPTH 32

/* The shapes a block may be written with, longest delimiter first. */
static const struct {
	const char *open;
	const char *close;
	const char *shape;
} bk_shapes[] = {
	{ "(((",	")))",	"doublecircle" },
	{ "((",		"))",	"circle" },
	{ "{{",		"}}",	"hexagon" },
	{ "([",		"])",	"stadium" },
	{ "[[",		"]]",	"subroutine" },
	{ "[(",		")]",	"cylinder" },
	{ "[/",		"/]",	"parallelogram" },
	{ "[",		"]",	"rect" },
	{ "(",		")",	"round" },
	{ "{",		"}",	"rhombus" },
	{ ">",		"]",	"odd" },
};

#define BK_SHAPE_COUNT (sizeof(bk_shapes) / sizeof(bk_shapes[0]))

/*
 * Read one block: an id, an optional shape with its label, and an optional
 * `:span` saying how many columns it covers.
 */
static fy_generic bk_block(struct fy_generic_builder *gb, const char *s,
			   const char *e, fy_generic parent)
{
	fy_generic label = fy_null;
	const char *shape = "rect";
	const char *colon, *open, *close, *ts, *te;
	long long span = 1;
	size_t i, ol, cl;
	char buf[32], *end;

	while (s < e && (*s == ' ' || *s == '\t'))
		s++;
	while (e > s && (e[-1] == ' ' || e[-1] == '\t'))
		e--;
	if (s >= e)
		return fy_null;

	/* `A:2` spans two columns; a colon inside a label is not a span */
	colon = fymm_memrchr(s, ':', (size_t)(e - s));
	if (colon && colon + 1 < e) {
		snprintf(buf, sizeof(buf), "%.*s", (int)(e - colon - 1),
			 colon + 1);
		end = NULL;
		span = strtoll(buf, &end, 10);
		if (end && end != buf && !*end)
			e = colon;
		else
			span = 1;
	}

	for (i = 0; i < BK_SHAPE_COUNT; i++) {
		ol = strlen(bk_shapes[i].open);
		cl = strlen(bk_shapes[i].close);
		open = fymm_memmem(s, (size_t)(e - s), bk_shapes[i].open, ol);
		if (!open || (size_t)(e - open) < ol + cl ||
		    memcmp(e - cl, bk_shapes[i].close, cl))
			continue;
		close = e - cl;
		ts = open + ol;
		te = close;
		if (te - ts >= 2 && *ts == '"' && te[-1] == '"') {
			ts++;
			te--;
		}
		label = fymm_trim_text(gb, ts, te);
		shape = bk_shapes[i].shape;
		e = open;
		break;
	}

	return fy_mapping(gb,
		"id", fymm_trim_text(gb, s, e),
		"label", label,
		"shape", shape,
		"span", span,
		"parent", parent);
}

int fymm_parse_block(struct fymm_parser *p, fy_generic config, fy_generic title,
		     struct fymm_token *htoks, int hn)
{
	struct fy_generic_builder *gb = p->d->gb;
	struct fymm_token toks[FYMM_MAX_TOKENS];
	struct fymm_acc acc = { fy_null, fy_null };
	fy_generic blocks = fy_seq_empty, arrows = fy_seq_empty;
	fy_generic scope[BK_MAX_DEPTH], block;
	const char *line, *e, *rest, *q, *ws;
	int depth = 0, anon = 0;
	size_t len;
	int i, n;

	for (i = 0; i < BK_MAX_DEPTH; i++)
		scope[i] = fy_null;

	for (i = 1; i < hn; i++) {
		if (htoks[i].type == FYMM_TOK_COLON)
			continue;
		fymm_diagf(p, false, htoks[i].line, htoks[i].col,
			   "ignoring unknown block option '%.*s'",
			   (int)htoks[i].len, htoks[i].text);
	}

	while (fymm_lex_next_line(&p->lex)) {
		line = fymm_lex_line(&p->lex, &len);
		if (!len)
			continue;
		e = line + len;

		n = fymm_line_tokens(&p->lex, toks, FYMM_MAX_TOKENS);
		if (fymm_stmt_acc(p, toks, n, &acc)) {
			fymm_tokens_reset(toks, n);
			continue;
		}
		fymm_tokens_reset(toks, n);

		if (len == 3 && !strncasecmp(line, "end", 3)) {
			if (!depth)
				fymm_diagf(p, true, p->lex.line, 1,
					   "'end' with no block open");
			else
				depth--;
			continue;
		}
		if (fymm_line_keyword(line, len, "columns", &rest)) {
			config = fy_assoc(gb, config, "columns",
					  fymm_trim_text(gb, rest, e));
			continue;
		}
		if (fymm_line_keyword(line, len, "classDef", &rest) ||
		    fymm_line_keyword(line, len, "class", &rest) ||
		    fymm_line_keyword(line, len, "style", &rest) ||
		    fymm_line_keyword(line, len, "click", &rest))
			continue;

		/* `blockArrow<[...]>(up, down)` is an arrow, not a block */
		if (fymm_line_keyword(line, len, "blockArrow", &rest) ||
		    (len >= 10 && !strncasecmp(line, "blockArrow", 10) &&
		     (rest = line + 10) != NULL)) {
			const char *dirs = memchr(rest, '(',
						  (size_t)(e - rest));

			arrows = fy_append(gb, arrows,
				fy_mapping(gb,
					"directions", dirs ?
						fymm_trim_text(gb, dirs + 1,
							       e - 1) :
						fy_null,
					"parent", depth ? scope[depth - 1] :
							  fy_null));
			continue;
		}

		/* `block` alone, or `block:id`, opens a compound block */
		if (len >= 5 && !strncasecmp(line, "block", 5) &&
		    (len == 5 || line[5] == ':' || line[5] == ' ')) {
			char id[32];

			if (depth + 1 >= BK_MAX_DEPTH) {
				fymm_diagf(p, true, p->lex.line, 1,
					   "blocks nest too deeply");
				continue;
			}
			if (len > 6 && line[5] == ':') {
				block = bk_block(gb, line + 6, e,
						 depth ? scope[depth - 1] :
							 fy_null);
			} else {
				snprintf(id, sizeof(id), "__block%d", anon++);
				block = fy_mapping(gb,
					"id", id,
					"label", fy_null,
					"shape", "group",
					"span", 1LL,
					"parent", depth ? scope[depth - 1] :
							  fy_null);
			}
			blocks = fy_append(gb, blocks,
				fy_assoc(gb, block, "shape", "group"));
			scope[depth] = fy_get(block, "id");
			depth++;
			continue;
		}

		/* every other line holds one or more blocks, space separated,
		 * unless the spaces sit inside a label */
		for (q = line; q < e; ) {
			int nest = 0;
			bool quote = false;

			while (q < e && (*q == ' ' || *q == '\t'))
				q++;
			for (ws = q; ws < e; ws++) {
				if (*ws == '"')
					quote = !quote;
				else if (quote)
					continue;
				else if (strchr("([{<", *ws))
					nest++;
				else if (strchr(")]}>", *ws))
					nest -= nest > 0;
				else if ((*ws == ' ' || *ws == '\t') && !nest)
					break;
			}
			if (ws > q) {
				block = bk_block(gb, q, ws,
						 depth ? scope[depth - 1] :
							 fy_null);
				if (fy_is_mapping(block))
					blocks = fy_append(gb, blocks, block);
			}
			q = ws;
		}
	}

	if (depth)
		fymm_diagf(p, true, p->lex.line, 1,
			   "a block was left open at the end of the diagram");

	if (fy_is_invalid(title))
		title = acc.title;

	p->d->model = fy_mapping(gb,
		"type", "block",
		"title", title,
		"accTitle", acc.title,
		"accDescr", acc.descr,
		"config", config,
		"blocks", blocks,
		"arrows", arrows);
	return 0;
}
