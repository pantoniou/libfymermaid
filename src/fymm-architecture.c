/*
 * fymm-architecture.c - the architecture statement parser and model builder
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

struct ar {
	struct fymm_parser *p;
	struct fy_generic_builder *gb;
	fy_generic nodes;
	fy_generic edges;
	fy_generic aligns;
};

/* Is @id already declared, and of which kind? */
static const char *ar_kind_of(struct ar *a, const char *id)
{
	fy_generic nd;

	fy_foreach(nd, a->nodes) {
		if (!strcmp(fy_get(nd, "id", ""), id))
			return fy_get(nd, "kind", "");
	}
	return NULL;
}

/*
 * Read `id(icon)[Label] in parent`. The icon and the label are optional, and
 * a junction has neither.
 */
static void ar_declare(struct ar *a, const char *s, const char *e,
		       const char *kind)
{
	const char *open, *close, *sq, *eq, *rest;
	fy_generic icon = fy_null, label = fy_null, parent = fy_null;
	const char *id_end = e;

	while (s < e && (*s == ' ' || *s == '\t'))
		s++;

	/* `in parent` names the group the node belongs to */
	for (rest = s; rest + 4 <= e; rest++) {
		if (strncasecmp(rest, " in ", 4))
			continue;
		parent = fymm_trim_text(a->gb, rest + 4, e);
		id_end = rest;
		e = rest;
		break;
	}

	sq = memchr(s, '[', (size_t)(e - s));
	eq = sq ? fymm_memrchr(sq, ']', (size_t)(e - sq)) : NULL;
	if (sq && eq) {
		label = fymm_trim_text(a->gb, sq + 1, eq);
		id_end = sq;
	}

	open = memchr(s, '(', (size_t)(id_end - s));
	close = open ? memchr(open, ')', (size_t)(id_end - open)) : NULL;
	if (open && close) {
		icon = fymm_trim_text(a->gb, open + 1, close);
		id_end = open;
	}

	if (fy_is_valid(parent) && !fy_is_null(parent) &&
	    !ar_kind_of(a, fy_str(parent)))
		fymm_diagf(a->p, true, a->p->lex.line, 1,
			   "'%s' is not a group that has been declared",
			   fy_str(parent));

	a->nodes = fy_append(a->gb, a->nodes,
		fy_mapping(a->gb,
			"id", fymm_trim_text(a->gb, s, id_end),
			"kind", kind,
			"icon", icon,
			"label", label,
			"parent", parent));
}

/* `a:R -- L:b` and `a:R --> L:b` join two nodes by a named side. */
static bool ar_edge(struct ar *a, const char *s, const char *e)
{
	const char *op = NULL, *q, *lcolon, *rcolon;
	size_t olen = 0;
	bool arrow_from = false, arrow_to = false;

	for (q = s; q + 2 <= e && !op; q++) {
		if (!memcmp(q, "<-->", 4) && q + 4 <= e) {
			op = q;
			olen = 4;
			arrow_from = arrow_to = true;
		} else if (!memcmp(q, "-->", 3) && q + 3 <= e) {
			op = q;
			olen = 3;
			arrow_to = true;
		} else if (!memcmp(q, "<--", 3) && q + 3 <= e) {
			op = q;
			olen = 3;
			arrow_from = true;
		} else if (!memcmp(q, "--", 2)) {
			op = q;
			olen = 2;
		}
	}
	if (!op)
		return false;

	lcolon = fymm_memrchr(s, ':', (size_t)(op - s));
	rcolon = memchr(op + olen, ':', (size_t)(e - op - olen));
	if (!lcolon || !rcolon)
		return false;

	a->edges = fy_append(a->gb, a->edges,
		fy_mapping(a->gb,
			"from", fymm_trim_text(a->gb, s, lcolon),
			"fromSide", fymm_trim_text(a->gb, lcolon + 1, op),
			"to", fymm_trim_text(a->gb, rcolon + 1, e),
			"toSide", fymm_trim_text(a->gb, op + olen, rcolon),
			"arrowFrom", arrow_from,
			"arrowTo", arrow_to));
	return true;
}

int fymm_parse_architecture(struct fymm_parser *p, fy_generic config,
			    fy_generic title, struct fymm_token *htoks, int hn)
{
	struct fy_generic_builder *gb = p->d->gb;
	struct fymm_token toks[FYMM_MAX_TOKENS];
	struct fymm_acc acc = { fy_null, fy_null };
	struct ar a;
	const char *line, *e, *rest, *q, *ws;
	size_t len;
	int i, n;

	memset(&a, 0, sizeof(a));
	a.p = p;
	a.gb = gb;
	a.nodes = fy_seq_empty;
	a.edges = fy_seq_empty;
	a.aligns = fy_seq_empty;

	/* `architecture-beta title Some Title` puts the title on the header */
	if (hn > 1 && fymm_token_ieq(&htoks[1], "title"))
		title = fy_value(gb, fymm_rest_text(p, htoks[1].col +
						    (int)htoks[1].len));
	else {
		for (i = 1; i < hn; i++) {
			if (htoks[i].type == FYMM_TOK_COLON)
				continue;
			fymm_diagf(p, false, htoks[i].line, htoks[i].col,
				   "ignoring unknown architecture option '%.*s'",
				   (int)htoks[i].len, htoks[i].text);
		}
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

		if (fymm_line_keyword(line, len, "title", &rest)) {
			if (fy_is_invalid(title))
				title = fymm_trim_text(gb, rest, e);
			continue;
		}
		if (fymm_line_keyword(line, len, "group", &rest)) {
			ar_declare(&a, rest, e, "group");
			continue;
		}
		if (fymm_line_keyword(line, len, "service", &rest)) {
			ar_declare(&a, rest, e, "service");
			continue;
		}
		if (fymm_line_keyword(line, len, "junction", &rest)) {
			ar_declare(&a, rest, e, "junction");
			continue;
		}

		/*
		 * `align row a b` lines up the members it names. Every member
		 * must be a service that exists, and naming one twice asks
		 * for it to sit in two places at once.
		 */
		if (fymm_line_keyword(line, len, "align", &rest)) {
			fy_generic members = fy_seq_empty, axis;
			fy_generic m, other;
			bool bad = false;

			for (q = rest; q < e && *q != ' ' && *q != '\t'; q++)
				;
			axis = fymm_trim_text(gb, rest, q);

			while (q < e) {
				while (q < e && (*q == ' ' || *q == '\t'))
					q++;
				for (ws = q; ws < e && *ws != ' ' &&
					     *ws != '\t'; ws++)
					;
				if (ws == q)
					break;
				m = fymm_trim_text(gb, q, ws);
				if (!ar_kind_of(&a, fy_str(m)) ||
				    strcmp(ar_kind_of(&a, fy_str(m)),
					   "service")) {
					fymm_diagf(p, true, p->lex.line, 1,
						   "'%s' is not a service, so it cannot be aligned",
						   fy_str(m));
					bad = true;
					break;
				}
				fy_foreach(other, members) {
					if (!fy_equal(other, m))
						continue;
					fymm_diagf(p, true, p->lex.line, 1,
						   "'%s' is aligned twice in one directive",
						   fy_str(m));
					bad = true;
					break;
				}
				if (bad)
					break;
				members = fy_append(gb, members, m);
				q = ws;
			}
			if (!bad)
				a.aligns = fy_append(gb, a.aligns,
					fy_mapping(gb, "axis", axis,
						   "members", members));
			continue;
		}

		if (ar_edge(&a, line, e))
			continue;

		fymm_diagf(p, true, p->lex.line, 1,
			   "unknown architecture statement '%.*s'",
			   (int)len, line);
	}

	if (fy_is_invalid(title))
		title = acc.title;

	p->d->model = fy_mapping(gb,
		"type", "architecture",
		"title", title,
		"accTitle", acc.title,
		"accDescr", acc.descr,
		"config", config,
		"nodes", a.nodes,
		"edges", a.edges,
		"aligns", a.aligns);
	return 0;
}
