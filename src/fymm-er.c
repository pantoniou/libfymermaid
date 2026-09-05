/*
 * fymm-er.c - the entity relationship statement parser and model builder
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

/*
 * The symbolic cardinalities, as they are written at each end of a relation.
 * The left end reads outward and the right end inward, so the same meaning
 * has two spellings.
 */
static const struct {
	const char *left;
	const char *right;
	const char *name;
} er_cards[] = {
	{ "|o", "o|", "zero or one" },
	{ "||", "||", "exactly one" },
	{ "}o", "o{", "zero or more" },
	{ "}|", "|{", "one or more" },
};

#define ER_CARD_COUNT (sizeof(er_cards) / sizeof(er_cards[0]))

/* The words a cardinality may be spelled with, longest phrase first. */
static const struct {
	const char *text;
	const char *name;
} er_words[] = {
	{ "one or zero",	"zero or one" },
	{ "zero or one",	"zero or one" },
	{ "one or more",	"one or more" },
	{ "one or many",	"one or more" },
	{ "zero or more",	"zero or more" },
	{ "zero or many",	"zero or more" },
	{ "many(0)",		"zero or more" },
	{ "many(1)",		"one or more" },
	{ "only one",		"exactly one" },
	{ "many",		"zero or more" },
	{ "one",		"exactly one" },
	{ "0+",			"zero or more" },
	{ "1+",			"one or more" },
	{ "0",			"zero or one" },
	{ "1",			"exactly one" },
};

#define ER_WORD_COUNT (sizeof(er_words) / sizeof(er_words[0]))

struct er {
	struct fymm_parser *p;
	struct fy_generic_builder *gb;
	fy_generic entities;
	fy_generic relations;
};

/*
 * An entity name may be quoted. `%` would start a comment and `\` could
 * escape out of the label, so upstream refuses either inside one.
 */
static fy_generic er_entity(struct er *r, const char *s, const char *e)
{
	const char *id;
	size_t i, count;
	bool quoted = false;

	while (s < e && (*s == ' ' || *s == '\t'))
		s++;
	while (e > s && (e[-1] == ' ' || e[-1] == '\t'))
		e--;
	if (e - s >= 2 && *s == '"' && e[-1] == '"') {
		s++;
		e--;
		quoted = true;
	}
	if (s >= e)
		return fy_null;

	if (quoted && memchr(s, '%', (size_t)(e - s))) {
		fymm_diagf(r->p, true, r->p->lex.line, 1,
			   "an entity name cannot contain '%%'");
		return fy_null;
	}
	if (quoted && memchr(s, '\\', (size_t)(e - s))) {
		fymm_diagf(r->p, true, r->p->lex.line, 1,
			   "an entity name cannot contain a backslash");
		return fy_null;
	}

	id = fy_gb_intern_string_size(r->gb, s, (size_t)(e - s));
	count = fy_len(r->entities);
	for (i = 0; i < count; i++) {
		if (!strcmp(fy_get(fy_get_at(r->entities, i), "name", ""), id))
			return fy_value(r->gb, id);
	}
	r->entities = fy_append(r->gb, r->entities,
		fy_mapping(r->gb, "name", id, "attributes", fy_seq_empty));
	return fy_value(r->gb, id);
}

/* Add an attribute, `type name [key] ["comment"]`, to an entity. */
static void er_attribute(struct er *r, fy_generic entity, const char *s,
			 const char *e)
{
	fy_generic attrs, keys = fy_seq_empty, comment = fy_null;
	const char *fields[6], *ends[6], *quote;
	size_t i, count, nf = 0;

	while (s < e && (*s == ' ' || *s == '\t'))
		s++;
	while (e > s && (e[-1] == ' ' || e[-1] == '\t'))
		e--;
	if (s >= e)
		return;

	/* a trailing quoted comment is not a field */
	quote = memchr(s, '"', (size_t)(e - s));
	if (quote) {
		const char *close = memchr(quote + 1, '"',
					   (size_t)(e - quote - 1));

		if (close) {
			comment = fymm_trim_text(r->gb, quote + 1, close);
			e = quote;
			while (e > s && (e[-1] == ' ' || e[-1] == '\t'))
				e--;
		}
	}

	while (s < e && nf < 6) {
		const char *q = s;

		while (q < e && *q != ' ' && *q != '\t')
			q++;
		fields[nf] = s;
		ends[nf] = q;
		nf++;
		while (q < e && (*q == ' ' || *q == '\t'))
			q++;
		s = q;
	}
	if (nf < 2)
		return;

	/* whatever follows the type and the name is a key marker */
	for (i = 2; i < nf; i++)
		keys = fy_append(r->gb, keys,
				 fymm_trim_text(r->gb, fields[i], ends[i]));

	count = fy_len(r->entities);
	for (i = 0; i < count; i++) {
		fy_generic ent = fy_get_at(r->entities, i);

		if (strcmp(fy_get(ent, "name", ""), fy_str(entity)))
			continue;
		attrs = fy_append(r->gb, fy_get(ent, "attributes"),
			fy_mapping(r->gb,
				"type", fymm_trim_text(r->gb, fields[0], ends[0]),
				"name", fymm_trim_text(r->gb, fields[1], ends[1]),
				"keys", keys,
				"comment", comment));
		r->entities = fy_replace(r->gb, r->entities, i,
					 fy_assoc(r->gb, ent, "attributes",
						  attrs));
		return;
	}
}

/* Match a cardinality written in words at the head of [s, e). */
static const char *er_word_card(const char *s, const char *e, size_t *lenp)
{
	size_t i, l;

	for (i = 0; i < ER_WORD_COUNT; i++) {
		l = strlen(er_words[i].text);
		if ((size_t)(e - s) >= l && !strncasecmp(s, er_words[i].text, l) &&
		    (s + l == e || s[l] == ' ')) {
			*lenp = l;
			return er_words[i].name;
		}
	}
	return NULL;
}

/*
 * A relation is `A ||--|{ B : label` or the same in words,
 * `A one or many optionally to zero or one B : has`.
 */
static bool er_relation(struct er *r, const char *s, const char *e)
{
	const char *colon = fymm_split_colon(s, e);
	const char *limit = colon ? colon : e;
	const char *q, *to, *lend, *rstart;
	const char *lcard = NULL, *rcard = NULL, *line_kind = "identifying";
	fy_generic label = fy_null;
	size_t i, l;

	/*
	 * The symbolic form: a cardinality, a connector of `--` or `..`, and
	 * a second cardinality. The left one is two characters, except for
	 * `u`, which mermaid uses for a markdown parent and which is one.
	 */
	for (q = s; q + 5 <= limit; q++) {
		const char *body;
		size_t lwidth;
		bool dotted;

		lwidth = 0;
		for (i = 0; i < ER_CARD_COUNT; i++) {
			if (q + 2 > limit || memcmp(q, er_cards[i].left, 2))
				continue;
			lcard = er_cards[i].name;
			lwidth = 2;
			break;
		}
		if (!lwidth && *q == 'u') {
			lcard = "parent";
			lwidth = 1;
		}
		if (!lwidth)
			continue;

		body = q + lwidth;
		if (body + 4 > limit ||
		    (memcmp(body, "--", 2) && memcmp(body, "..", 2))) {
			lcard = NULL;
			continue;
		}
		dotted = !memcmp(body, "..", 2);
		for (i = 0; i < ER_CARD_COUNT; i++) {
			if (memcmp(body + 2, er_cards[i].right, 2))
				continue;
			rcard = er_cards[i].name;
			break;
		}
		if (!rcard) {
			lcard = NULL;
			continue;
		}
		line_kind = dotted ? "non-identifying" : "identifying";
		lend = q;
		rstart = body + 4;
		goto found;
	}

	/* the verbal form, which always carries a `to` */
	to = NULL;
	for (q = s; q + 4 <= limit; q++) {
		if (!strncasecmp(q, " to ", 4)) {
			to = q;
			break;
		}
	}
	if (!to)
		return false;

	/* the left cardinality runs back from `to`, and `optionally` may sit
	 * between it and the keyword */
	q = to;
	while (q > s && q[-1] == ' ')
		q--;
	if (q - s > 10 && !strncasecmp(q - 10, "optionally", 10)) {
		q -= 10;
		line_kind = "non-identifying";
		while (q > s && q[-1] == ' ')
			q--;
	}
	for (lend = s; lend < q; lend++) {
		if (*lend != ' ')
			continue;
		lcard = er_word_card(lend + 1, q, &l);
		if (lcard && lend + 1 + l == q)
			break;
	}
	if (!lcard)
		return false;

	rstart = to + 4;
	while (rstart < limit && *rstart == ' ')
		rstart++;
	rcard = er_word_card(rstart, limit, &l);
	if (!rcard)
		return false;
	rstart += l;

found:
	if (colon)
		label = fymm_trim_text(r->gb, colon + 1, e);

	r->relations = fy_append(r->gb, r->relations,
		fy_mapping(r->gb,
			"from", er_entity(r, s, lend),
			"to", er_entity(r, rstart, limit),
			"fromCardinality", lcard,
			"toCardinality", rcard,
			"line", line_kind,
			"label", label));
	return true;
}

int fymm_parse_er(struct fymm_parser *p, fy_generic config, fy_generic title,
		  struct fymm_token *htoks, int hn)
{
	struct fy_generic_builder *gb = p->d->gb;
	struct fymm_token toks[FYMM_MAX_TOKENS];
	struct fymm_acc acc = { fy_null, fy_null };
	struct er r;
	fy_generic open_entity = fy_null;
	const char *line, *e, *rest, *brace;
	const char *direction = "TB";
	size_t len;
	int i, n;

	memset(&r, 0, sizeof(r));
	r.p = p;
	r.gb = gb;
	r.entities = fy_seq_empty;
	r.relations = fy_seq_empty;

	for (i = 1; i < hn; i++) {
		if (htoks[i].type == FYMM_TOK_COLON)
			continue;
		fymm_diagf(p, false, htoks[i].line, htoks[i].col,
			   "ignoring unknown erDiagram option '%.*s'",
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

		/* inside an entity body every line is an attribute */
		if (fy_is_valid(open_entity) && !fy_is_null(open_entity)) {
			if (*line == '}') {
				open_entity = fy_null;
				continue;
			}
			er_attribute(&r, open_entity, line, e);
			continue;
		}

		if (fymm_line_keyword(line, len, "direction", &rest)) {
			direction = fy_gb_intern_string_size(gb, rest,
							     (size_t)(e - rest));
			continue;
		}
		if (fymm_line_keyword(line, len, "title", &rest)) {
			if (fy_is_invalid(title))
				title = fymm_trim_text(gb, rest, e);
			continue;
		}
		if (fymm_line_keyword(line, len, "click", &rest) ||
		    fymm_line_keyword(line, len, "style", &rest) ||
		    fymm_line_keyword(line, len, "classDef", &rest) ||
		    fymm_line_keyword(line, len, "class", &rest))
			continue;

		if (er_relation(&r, line, e))
			continue;

		/* `ENTITY {` opens a block of attributes; a bare name just
		 * declares the entity */
		brace = memchr(line, '{', len);
		if (brace) {
			open_entity = er_entity(&r, line, brace);
			continue;
		}
		er_entity(&r, line, e);
	}

	if (fy_is_valid(open_entity) && !fy_is_null(open_entity))
		fymm_diagf(p, true, p->lex.line, 1,
			   "an entity block was left open at the end of the diagram");

	if (fy_is_invalid(title))
		title = acc.title;

	p->d->model = fy_mapping(gb,
		"type", "erDiagram",
		"direction", direction,
		"title", title,
		"accTitle", acc.title,
		"accDescr", acc.descr,
		"config", config,
		"entities", r.entities,
		"relations", r.relations);
	return 0;
}
