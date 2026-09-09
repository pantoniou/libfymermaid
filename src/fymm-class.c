/*
 * fymm-class.c - the class diagram statement parser and model builder
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
 * The relation operators, longest first so that `<|--` is not read as `<`
 * followed by `|--`. @from and @to name the decoration at each end, and @line
 * says whether the connector is solid or dotted.
 */
static const struct {
	const char *text;
	const char *from;
	const char *to;
	const char *line;
} cd_relations[] = {
	{ "<|--",	"extension",	"none",		"solid" },
	{ "--|>",	"none",		"extension",	"solid" },
	{ "<|..",	"extension",	"none",		"dotted" },
	{ "..|>",	"none",		"extension",	"dotted" },
	{ "*--",	"composition",	"none",		"solid" },
	{ "--*",	"none",		"composition",	"solid" },
	{ "o--",	"aggregation",	"none",		"solid" },
	{ "--o",	"none",		"aggregation",	"solid" },
	{ "()--",	"lollipop",	"none",		"solid" },
	{ "--()",	"none",		"lollipop",	"solid" },
	{ "<-->",	"arrow",	"arrow",	"solid" },
	{ "<--",	"arrow",	"none",		"solid" },
	{ "-->",	"none",		"arrow",	"solid" },
	{ "<..>",	"arrow",	"arrow",	"dotted" },
	{ "<..",	"arrow",	"none",		"dotted" },
	{ "..>",	"none",		"arrow",	"dotted" },
	{ "--",		"none",		"none",		"solid" },
	{ "..",		"none",		"none",		"dotted" },
};

#define CD_RELATION_COUNT (sizeof(cd_relations) / sizeof(cd_relations[0]))

struct cd {
	struct fymm_parser *p;
	struct fy_generic_builder *gb;
	fy_generic classes;
	fy_generic relations;
	fy_generic notes;
	fy_generic namespace_name;
};

/* The first @ch outside a quoted string, or NULL. */
static const char *cd_unquoted(const char *s, const char *e, char ch)
{
	bool in_quote = false;

	for (; s < e; s++) {
		if (*s == '"' && (s[-1] != '\\'))
			in_quote = !in_quote;
		else if (*s == ch && !in_quote)
			return s;
	}
	return NULL;
}

/* Strip a `~T~` generic parameter off a class name, keeping it separately. */
static void cd_split_generic(const char *s, size_t len, size_t *namelen,
			     const char **generic, size_t *genlen)
{
	const char *tilde = memchr(s, '~', len);
	const char *close;

	*namelen = len;
	*generic = NULL;
	*genlen = 0;
	if (!tilde)
		return;
	close = memchr(tilde + 1, '~', len - (size_t)(tilde + 1 - s));
	if (!close)
		return;
	*namelen = (size_t)(tilde - s);
	*generic = tilde + 1;
	*genlen = (size_t)(close - (tilde + 1));
}

/* Find a class by name, or record it. */
static size_t cd_class(struct cd *c, const char *name, size_t len)
{
	const char *generic = NULL;
	const char *iname, *bracket;
	fy_generic label;
	size_t namelen, genlen, i, count;

	cd_split_generic(name, len, &namelen, &generic, &genlen);
	while (namelen && (name[namelen - 1] == ' ' || name[namelen - 1] == '\t'))
		namelen--;
	while (namelen && (*name == ' ' || *name == '\t')) {
		name++;
		namelen--;
	}
	if (!namelen)
		return (size_t)-1;

	/* `class Name["Label"]` displays the label in place of the name */
	label = fy_null;
	bracket = memchr(name, '[', namelen);
	if (bracket && name[namelen - 1] == ']') {
		const char *ls = bracket + 1, *le = name + namelen - 1;

		if (le - ls >= 2 && *ls == '"' && le[-1] == '"') {
			ls++;
			le--;
		}
		label = fymm_trim_text(c->gb, ls, le);
		namelen = (size_t)(bracket - name);
	}

	iname = fy_gb_intern_string_size(c->gb, name, namelen);
	count = fy_len(c->classes);
	for (i = 0; i < count; i++) {
		if (strcmp(fy_get(fy_get_at(c->classes, i), "name", ""), iname))
			continue;
		if (fy_is_valid(label) && !fy_is_null(label))
			c->classes = fy_replace(c->gb, c->classes, i,
				fy_assoc(c->gb, fy_get_at(c->classes, i),
					 "label", label));
		return i;
	}

	c->classes = fy_append(c->gb, c->classes,
		fy_mapping(c->gb,
			"name", iname,
			"label", label,
			"generic", generic ?
				fy_value(c->gb,
					 fy_gb_intern_string_size(c->gb,
								  generic,
								  genlen)) :
				fy_null,
			"namespace", c->namespace_name,
			"annotation", fy_null,
			"members", fy_seq_empty));
	return count;
}

/* Record an `<<annotation>>` on class @idx. */
static void cd_annotate(struct cd *c, size_t idx, const char *s, const char *e)
{
	if (idx == (size_t)-1)
		return;
	c->classes = fy_replace(c->gb, c->classes, idx,
		fy_assoc(c->gb, fy_get_at(c->classes, idx), "annotation",
			 fymm_trim_text(c->gb, s, e)));
}

/* Add a member, or an `<<annotation>>`, to class @idx. */
static void cd_member(struct cd *c, size_t idx, const char *s, const char *e)
{
	fy_generic cls;

	if (idx == (size_t)-1)
		return;
	while (s < e && (*s == ' ' || *s == '\t'))
		s++;
	while (e > s && (e[-1] == ' ' || e[-1] == '\t'))
		e--;
	if (s >= e)
		return;

	cls = fy_get_at(c->classes, idx);
	if ((size_t)(e - s) > 4 && !memcmp(s, "<<", 2) &&
	    !memcmp(e - 2, ">>", 2)) {
		c->classes = fy_replace(c->gb, c->classes, idx,
			fy_assoc(c->gb, cls, "annotation",
				 fymm_trim_text(c->gb, s + 2, e - 2)));
		return;
	}
	(void)cls;

	c->classes = fy_replace(c->gb, c->classes, idx,
		fy_assoc(c->gb, cls, "members",
			 fy_append(c->gb, fy_get(cls, "members"),
				   fymm_trim_text(c->gb, s, e))));
}

/* Read a class body written on one line: `class X { a() b() }`. */
static void cd_body(struct cd *c, size_t idx, const char *s, const char *e)
{
	const char *nl;

	while (s < e) {
		nl = memchr(s, '\n', (size_t)(e - s));
		cd_member(c, idx, s, nl ? nl : e);
		if (!nl)
			break;
		s = nl + 1;
	}
}

/*
 * A relation is `A <|-- B`, optionally with a cardinality in quotes at either
 * end and a label after a colon: `A "1" *-- "many" B : contains`.
 */
static bool cd_stmt_relation(struct cd *c, const char *s, const char *e)
{
	const char *at = NULL, *q, *colon, *limit;
	const char *left_end, *right_start;
	fy_generic label = fy_null;
	size_t i, found = 0, alen = 0;
	size_t from, to;

	colon = fymm_split_colon(s, e);
	limit = colon ? colon : e;

	for (q = s; q < limit && !at; q++) {
		for (i = 0; i < CD_RELATION_COUNT; i++) {
			alen = strlen(cd_relations[i].text);
			if ((size_t)(limit - q) >= alen &&
			    !memcmp(q, cd_relations[i].text, alen)) {
				at = q;
				found = i;
				break;
			}
		}
	}
	if (!at)
		return false;

	left_end = at;
	right_start = at + strlen(cd_relations[found].text);

	/* a quoted cardinality sits between the name and the operator */
	{
		const char *lq = memchr(s, '"', (size_t)(left_end - s));

		if (lq)
			left_end = lq;
	}
	{
		const char *rq = memchr(right_start, '"',
					(size_t)(limit - right_start));

		if (rq) {
			const char *rq2 = memchr(rq + 1, '"',
						 (size_t)(limit - rq - 1));

			if (rq2)
				right_start = rq2 + 1;
		}
	}

	if (colon)
		label = fymm_trim_text(c->gb, colon + 1, e);

	from = cd_class(c, s, (size_t)(left_end - s));
	to = cd_class(c, right_start, (size_t)(limit - right_start));
	if (from == (size_t)-1 || to == (size_t)-1)
		return true;

	c->relations = fy_append(c->gb, c->relations,
		fy_mapping(c->gb,
			"from", fy_get(fy_get_at(c->classes, from), "name", ""),
			"to", fy_get(fy_get_at(c->classes, to), "name", ""),
			"fromEnd", cd_relations[found].from,
			"toEnd", cd_relations[found].to,
			"line", cd_relations[found].line,
			"label", label));
	return true;
}

int fymm_parse_class(struct fymm_parser *p, fy_generic config, fy_generic title,
		     struct fymm_token *htoks, int hn)
{
	struct fy_generic_builder *gb = p->d->gb;
	struct fymm_token toks[FYMM_MAX_TOKENS];
	struct fymm_acc acc = { fy_null, fy_null };
	struct cd c;
	const char *direction = "TB";
	const char *line, *e, *rest, *brace;
	size_t len, open_class = (size_t)-1;
	bool in_namespace = false;
	int i, n;

	memset(&c, 0, sizeof(c));
	c.p = p;
	c.gb = gb;
	c.classes = fy_seq_empty;
	c.relations = fy_seq_empty;
	c.notes = fy_seq_empty;
	c.namespace_name = fy_null;

	for (i = 1; i < hn; i++) {
		if (htoks[i].type == FYMM_TOK_COLON)
			continue;
		fymm_diagf(p, false, htoks[i].line, htoks[i].col,
			   "ignoring unknown classDiagram option '%.*s'",
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

		/* inside a class body every line is a member until the brace */
		if (open_class != (size_t)-1) {
			if (*line == '}') {
				open_class = (size_t)-1;
				continue;
			}
			cd_member(&c, open_class, line, e);
			continue;
		}

		if (len == 1 && *line == '}') {
			in_namespace = false;
			c.namespace_name = fy_null;
			continue;
		}

		if (fymm_line_keyword(line, len, "namespace", &rest)) {
			brace = cd_unquoted(rest, e, '{');
			c.namespace_name = fymm_trim_text(gb, rest,
							  brace ? brace : e);
			in_namespace = true;
			continue;
		}
		if (fymm_line_keyword(line, len, "direction", &rest)) {
			direction = fy_gb_intern_string_size(gb, rest,
							     (size_t)(e - rest));
			continue;
		}
		if (fymm_line_keyword(line, len, "note", &rest)) {
			const char *forkw = NULL;
			fy_generic target = fy_null;

			if (fymm_line_keyword(rest, (size_t)(e - rest), "for",
					      &forkw)) {
				const char *q = forkw;

				while (q < e && *q != '"' && *q != ' ')
					q++;
				target = fymm_trim_text(gb, forkw, q);
				rest = q;
			}
			c.notes = fy_append(gb, c.notes,
				fy_mapping(gb, "for", target,
					   "text", fymm_trim_text(gb, rest, e)));
			continue;
		}
		/* the statements a terminal has no use for */
		if (fymm_line_keyword(line, len, "click", &rest) ||
		    fymm_line_keyword(line, len, "link", &rest) ||
		    fymm_line_keyword(line, len, "callback", &rest) ||
		    fymm_line_keyword(line, len, "style", &rest) ||
		    fymm_line_keyword(line, len, "cssClass", &rest))
			continue;

		/* `<<interface>> Shape` annotates a class from outside its
		 * body, which is the other spelling upstream accepts */
		if (len > 4 && !memcmp(line, "<<", 2)) {
			const char *close = fymm_memmem(line, len, ">>", 2);

			if (close) {
				const char *who = close + 2;

				while (who < e && (*who == ' ' || *who == '\t'))
					who++;
				cd_annotate(&c,
					    cd_class(&c, who, (size_t)(e - who)),
					    line + 2, close);
				continue;
			}
		}

		if (fymm_line_keyword(line, len, "class", &rest)) {
			size_t idx;

			brace = cd_unquoted(rest, e, '{');
			idx = cd_class(&c, rest,
				       (size_t)((brace ? brace : e) - rest));
			if (!brace)
				continue;
			/* the body may be closed on the same line, and may
			 * hold its members there too */
			if (e[-1] == '}') {
				cd_body(&c, idx, brace + 1, e - 1);
				continue;
			}
			open_class = idx;
			continue;
		}

		if (cd_stmt_relation(&c, line, e))
			continue;

		/* `Name : +member()` names a class and one of its members */
		{
			const char *colon = fymm_split_colon(line, e);

			if (colon) {
				cd_member(&c,
					  cd_class(&c, line,
						   (size_t)(colon - line)),
					  colon + 1, e);
				continue;
			}
		}

		fymm_diagf(p, true, p->lex.line, 1,
			   "unknown classDiagram statement '%.*s'",
			   (int)len, line);
	}

	if (open_class != (size_t)-1)
		fymm_diagf(p, true, p->lex.line, 1,
			   "a class body was left open at the end of the diagram");
	if (in_namespace)
		fymm_diagf(p, true, p->lex.line, 1,
			   "a namespace was left open at the end of the diagram");

	if (fy_is_invalid(title))
		title = acc.title;

	p->d->model = fy_mapping(gb,
		"type", "classDiagram",
		"direction", direction,
		"title", title,
		"accTitle", acc.title,
		"accDescr", acc.descr,
		"config", config,
		"classes", c.classes,
		"relations", c.relations,
		"notes", c.notes);
	return 0;
}
