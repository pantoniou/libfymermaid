/*
 * fymm-usecase.c - the use case statement parser and model builder
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

struct uc {
	struct fymm_parser *p;
	struct fy_generic_builder *gb;
	fy_generic nodes;
	fy_generic edges;
	fy_generic boundaries;
	fy_generic notes;
	fy_generic boundary;	/* the open systemBoundary, or null */
};

/* Strip a `:::class`, a `<<stereotype>>` and a `@{ ... }` off a node spec. */
static const char *uc_strip(struct uc *u, const char *s, const char **ep,
			    fy_generic *stereop, fy_generic *metap)
{
	const char *e = *ep, *at, *open, *close;
	fy_generic_sized_string input;

	*stereop = fy_null;
	*metap = fy_null;

	/* `<<Human>>` names a stereotype */
	open = memmem(s, (size_t)(e - s), "<<", 2);
	if (open) {
		close = memmem(open, (size_t)(e - open), ">>", 2);
		if (close) {
			*stereop = fymm_trim_text(u->gb, open + 2, close);
			if (close + 2 >= e)
				e = open;
		}
	}

	/* `@{ ... }` carries the node's own settings */
	at = memmem(s, (size_t)(e - s), "@{", 2);
	if (at) {
		close = memchr(at, '}', (size_t)(e - at));
		if (close) {
			input.data = at + 1;
			input.size = (size_t)(close + 1 - (at + 1));
			*metap = fy_parse(u->gb, input,
					  FYMM_YAML_PARSE_FLAGS |
					  FYOPPF_INPUT_TYPE_STRING, NULL);
			if (!fy_is_mapping(*metap))
				*metap = fy_null;
			e = at;
		}
	}

	/* `:::name` names a class, which a terminal has no use for */
	for (at = s; at + 3 <= e; at++) {
		if (!memcmp(at, ":::", 3)) {
			e = at;
			break;
		}
	}

	while (e > s && (e[-1] == ' ' || e[-1] == '\t'))
		e--;
	*ep = e;
	return s;
}

/*
 * A node is `Id`, `Id("Label")`, `Id[Label]` or `"Quoted Name"`. A quote that
 * never closes is an error, as it is upstream.
 */
static fy_generic uc_node(struct uc *u, const char *s, const char *e,
			  const char *kind, fy_generic stereo, fy_generic meta)
{
	static const struct { char open, close; } shapes[] = {
		{ '(', ')' }, { '[', ']' }, { '{', '}' },
	};
	fy_generic id, label = fy_null;
	const char *open = NULL, *close;
	size_t i, count;

	while (s < e && (*s == ' ' || *s == '\t'))
		s++;
	while (e > s && (e[-1] == ' ' || e[-1] == '\t'))
		e--;
	if (s >= e)
		return fy_null;

	if (*s == '"') {
		close = memchr(s + 1, '"', (size_t)(e - s - 1));
		if (!close) {
			fymm_diagf(u->p, true, u->p->lex.line, 1,
				   "a quoted name is missing its closing quote");
			return fy_null;
		}
		id = fymm_trim_text(u->gb, s + 1, close);
	} else {
		for (i = 0; i < sizeof(shapes) / sizeof(shapes[0]); i++) {
			open = memchr(s, shapes[i].open, (size_t)(e - s));
			if (!open)
				continue;
			if (e[-1] != shapes[i].close) {
				open = NULL;
				continue;
			}
			break;
		}
		if (open) {
			const char *ls = open + 1, *le = e - 1;

			if (le - ls >= 2 && *ls == '"' && le[-1] == '"') {
				ls++;
				le--;
			}
			label = fymm_trim_text(u->gb, ls, le);
			id = fymm_trim_text(u->gb, s, open);
		} else {
			id = fymm_trim_text(u->gb, s, e);
		}
	}
	if (!*fy_str(id))
		return fy_null;

	count = fy_len(u->nodes);
	for (i = 0; i < count; i++) {
		fy_generic nd = fy_get_at(u->nodes, i);

		if (strcmp(fy_get(nd, "id", ""), fy_str(id)))
			continue;
		/* a later mention may supply what the first did not */
		if (fy_is_valid(label) && !fy_is_null(label))
			nd = fy_assoc(u->gb, nd, "label", label);
		if (kind)
			nd = fy_assoc(u->gb, nd, "kind", kind);
		if (fy_is_valid(stereo) && !fy_is_null(stereo))
			nd = fy_assoc(u->gb, nd, "stereotype", stereo);
		if (fy_is_valid(meta) && !fy_is_null(meta))
			nd = fy_assoc(u->gb, nd, "metadata", meta);
		u->nodes = fy_replace(u->gb, u->nodes, i, nd);
		return id;
	}

	u->nodes = fy_append(u->gb, u->nodes,
		fy_mapping(u->gb,
			"id", id,
			"label", label,
			"kind", kind ? kind : "usecase",
			"stereotype", stereo,
			"metadata", meta,
			"boundary", u->boundary));
	return id;
}

/*
 * An edge is `A --> B`, `A -- B` or `A ..> B`, and may carry an id before the
 * operator, `A base@--> B`, and a label after a colon.
 */
static bool uc_edge(struct uc *u, const char *s, const char *e)
{
	static const char *const ops[] = { "-->", "..>", "-.->", "--", ".." };
	const char *colon = fymm_split_colon(s, e);
	const char *limit = colon ? colon : e;
	const char *at = NULL, *lend, *rstart;
	fy_generic stereo, meta, id = fy_null;
	size_t i, olen = 0;

	for (i = 0; i < sizeof(ops) / sizeof(ops[0]) && !at; i++) {
		olen = strlen(ops[i]);
		at = memmem(s, (size_t)(limit - s), ops[i], olen);
	}
	if (!at)
		return false;

	lend = at;
	rstart = at + olen;

	/* `base@-->` names the edge */
	if (lend > s && lend[-1] == '@') {
		const char *q = lend - 1;

		while (q > s && q[-1] != ' ' && q[-1] != '\t')
			q--;
		id = fymm_trim_text(u->gb, q, lend - 1);
		lend = q;
	}

	u->edges = fy_append(u->gb, u->edges,
		fy_mapping(u->gb,
			"id", id,
			"from", uc_node(u, uc_strip(u, s, &lend, &stereo, &meta),
					lend, NULL, stereo, meta),
			"to", uc_node(u, uc_strip(u, rstart, &limit, &stereo,
						  &meta),
				      limit, NULL, stereo, meta),
			"label", colon ? fymm_trim_text(u->gb, colon + 1, e) :
					 fy_null));
	return true;
}

int fymm_parse_usecase(struct fymm_parser *p, fy_generic config,
		       fy_generic title, struct fymm_token *htoks, int hn)
{
	struct fy_generic_builder *gb = p->d->gb;
	struct fymm_token toks[FYMM_MAX_TOKENS];
	struct fymm_acc acc = { fy_null, fy_null };
	struct uc u;
	fy_generic stereo, meta;
	const char *line, *e, *rest, *comma, *nend;
	size_t len;
	int i, n;

	memset(&u, 0, sizeof(u));
	u.p = p;
	u.gb = gb;
	u.nodes = fy_seq_empty;
	u.edges = fy_seq_empty;
	u.boundaries = fy_seq_empty;
	u.notes = fy_seq_empty;
	u.boundary = fy_null;

	for (i = 1; i < hn; i++) {
		if (htoks[i].type == FYMM_TOK_COLON)
			continue;
		fymm_diagf(p, false, htoks[i].line, htoks[i].col,
			   "ignoring unknown usecase option '%.*s'",
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
			if (fy_is_null(u.boundary))
				fymm_diagf(p, true, p->lex.line, 1,
					   "'end' with no systemBoundary open");
			u.boundary = fy_null;
			continue;
		}
		if (fymm_line_keyword(line, len, "style", &rest) ||
		    fymm_line_keyword(line, len, "classDef", &rest) ||
		    fymm_line_keyword(line, len, "class", &rest) ||
		    fymm_line_keyword(line, len, "click", &rest))
			continue;

		if (fymm_line_keyword(line, len, "note", &rest)) {
			fy_generic target = fy_null;
			const char *q;

			if (fymm_line_keyword(rest, (size_t)(e - rest), "for",
					      &q)) {
				const char *w = q;

				while (w < e && *w != '"' && *w != ' ')
					w++;
				target = fymm_trim_text(gb, q, w);
				rest = w;
			}
			u.notes = fy_append(gb, u.notes,
				fy_mapping(gb, "for", target,
					   "text", fymm_trim_text(gb, rest, e)));
			continue;
		}

		if (fymm_line_keyword(line, len, "systemBoundary", &rest)) {
			nend = e;
			rest = uc_strip(&u, rest, &nend, &stereo, &meta);
			u.boundary = uc_node(&u, rest, nend, "boundary",
					     stereo, meta);
			u.boundaries = fy_append(gb, u.boundaries, u.boundary);
			continue;
		}

		/* `json Name@{ ... }` is a payload node */
		if (fymm_line_keyword(line, len, "json", &rest)) {
			nend = e;
			rest = uc_strip(&u, rest, &nend, &stereo, &meta);
			uc_node(&u, rest, nend, "json", stereo, meta);
			continue;
		}

		/* `actor A, B` declares one or more actors */
		if (fymm_line_keyword(line, len, "actor", &rest)) {
			while (rest < e) {
				comma = memchr(rest, ',', (size_t)(e - rest));
				nend = comma ? comma : e;
				{
					const char *ns = rest;

					ns = uc_strip(&u, ns, &nend, &stereo,
						      &meta);
					uc_node(&u, ns, nend, "actor", stereo,
						meta);
				}
				if (!comma)
					break;
				rest = comma + 1;
			}
			continue;
		}

		if (uc_edge(&u, line, e))
			continue;

		/* anything else declares a use case */
		nend = e;
		rest = uc_strip(&u, line, &nend, &stereo, &meta);
		uc_node(&u, rest, nend, NULL, stereo, meta);
	}

	if (!fy_is_null(u.boundary))
		fymm_diagf(p, true, p->lex.line, 1,
			   "a systemBoundary was left open at the end of the diagram");

	if (fy_is_invalid(title))
		title = acc.title;

	p->d->model = fy_mapping(gb,
		"type", "usecase",
		"title", title,
		"accTitle", acc.title,
		"accDescr", acc.descr,
		"config", config,
		"nodes", u.nodes,
		"edges", u.edges,
		"boundaries", u.boundaries,
		"notes", u.notes);
	return 0;
}
