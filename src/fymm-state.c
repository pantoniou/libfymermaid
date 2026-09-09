/*
 * fymm-state.c - the state diagram statement parser and model builder
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fymm-internal.h"

/* how deeply composite states may nest */
#define ST_MAX_DEPTH 32

struct st {
	struct fymm_parser *p;
	struct fy_generic_builder *gb;
	fy_generic states;
	fy_generic transitions;
	fy_generic notes;
	/* the composite state each open brace belongs to, and the start and
	 * end pseudo-states of that scope */
	fy_generic scope[ST_MAX_DEPTH];
	fy_generic start[ST_MAX_DEPTH];
	fy_generic end[ST_MAX_DEPTH];
	int depth;
	int pseudo;		/* how many pseudo-states have been made */
};

/* Record a state the first time it is named, and return its id. */
static fy_generic st_state(struct st *s, const char *name, size_t len,
			   const char *label, const char *kind)
{
	const char *id;
	fy_generic st;
	size_t i, count;

	while (len && (name[len - 1] == ' ' || name[len - 1] == '\t'))
		len--;
	while (len && (*name == ' ' || *name == '\t')) {
		name++;
		len--;
	}
	if (len >= 2 && *name == '"' && name[len - 1] == '"') {
		name++;
		len -= 2;
	}
	if (!len)
		return fy_null;

	id = fy_gb_intern_string_size(s->gb, name, len);
	count = fy_len(s->states);
	for (i = 0; i < count; i++) {
		st = fy_get_at(s->states, i);
		if (strcmp(fy_get(st, "id", ""), id))
			continue;
		/* a later declaration may supply the label or the kind */
		if (label)
			st = fy_assoc(s->gb, st, "label", label);
		if (kind)
			st = fy_assoc(s->gb, st, "kind", kind);
		s->states = fy_replace(s->gb, s->states, i, st);
		return fy_value(s->gb, id);
	}

	s->states = fy_append(s->gb, s->states,
		fy_mapping(s->gb,
			"id", id,
			"label", label ? fy_value(s->gb, label) : fy_null,
			"kind", kind ? kind : "normal",
			"parent", s->depth ? s->scope[s->depth - 1] : fy_null));
	return fy_value(s->gb, id);
}

/*
 * `[*]` is the start of a scope when a transition leaves it and the end when
 * one arrives, so each scope carries one of each and they are made on first
 * use.
 */
static fy_generic st_pseudo(struct st *s, bool is_start)
{
	fy_generic *slot = is_start ? &s->start[s->depth] : &s->end[s->depth];
	char id[32];

	if (fy_is_valid(*slot) && !fy_is_null(*slot))
		return *slot;

	snprintf(id, sizeof(id), "__%s%d", is_start ? "start" : "end",
		 s->pseudo++);
	*slot = st_state(s, id, strlen(id), NULL, is_start ? "start" : "end");
	return *slot;
}

/* Read one end of a transition, which may be `[*]`. */
static fy_generic st_endpoint(struct st *s, const char *b, const char *e,
			      bool is_source)
{
	while (b < e && (*b == ' ' || *b == '\t'))
		b++;
	while (e > b && (e[-1] == ' ' || e[-1] == '\t'))
		e--;
	if (e - b == 3 && !memcmp(b, "[*]", 3))
		return st_pseudo(s, is_source);
	return st_state(s, b, (size_t)(e - b), NULL, NULL);
}

/* An odd number of unescaped quotes means the statement is still open. */
static bool st_quotes_balanced(const char *s, size_t len)
{
	size_t i;
	bool in_quote = false;

	for (i = 0; i < len; i++) {
		if (s[i] == '"' && (!i || s[i - 1] != '\\'))
			in_quote = !in_quote;
	}
	return !in_quote;
}

int fymm_parse_state(struct fymm_parser *p, fy_generic config, fy_generic title,
		     struct fymm_token *htoks, int hn)
{
	struct fy_generic_builder *gb = p->d->gb;
	struct fymm_token toks[FYMM_MAX_TOKENS];
	struct fymm_acc acc = { fy_null, fy_null };
	struct st s;
	const char *direction = "TB";
	const char *line, *e, *rest, *arrow, *colon, *brace, *q;
	fy_generic from, to;
	char *joined = NULL;
	size_t len;
	int i, n;

	memset(&s, 0, sizeof(s));
	s.p = p;
	s.gb = gb;
	s.states = fy_seq_empty;
	s.transitions = fy_seq_empty;
	s.notes = fy_seq_empty;
	for (i = 0; i < ST_MAX_DEPTH; i++) {
		s.scope[i] = fy_null;
		s.start[i] = fy_null;
		s.end[i] = fy_null;
	}

	for (i = 1; i < hn; i++) {
		if (htoks[i].type == FYMM_TOK_COLON)
			continue;
		fymm_diagf(p, false, htoks[i].line, htoks[i].col,
			   "ignoring unknown stateDiagram option '%.*s'",
			   (int)htoks[i].len, htoks[i].text);
	}

	while (fymm_lex_next_line(&p->lex)) {
		line = fymm_lex_line(&p->lex, &len);
		if (!len)
			continue;

		/* a `//` line is a comment in a state diagram */
		if (len >= 2 && !memcmp(line, "//", 2))
			continue;

		/* a state description may be quoted across two lines */
		free(joined);
		joined = NULL;
		if (!st_quotes_balanced(line, len)) {
			size_t cap = len * 2 + 64, used = len;

			joined = malloc(cap);
			if (!joined)
				break;
			memcpy(joined, line, len);
			joined[used] = '\0';
			while (!st_quotes_balanced(joined, used) &&
			       fymm_lex_next_line(&p->lex)) {
				const char *more;
				size_t mlen;
				char *nj;

				more = fymm_lex_line(&p->lex, &mlen);
				if (used + mlen + 2 > cap) {
					cap = (used + mlen + 2) * 2;
					nj = realloc(joined, cap);
					if (!nj)
						break;
					joined = nj;
				}
				joined[used++] = '\n';
				memcpy(joined + used, more, mlen);
				used += mlen;
				joined[used] = '\0';
			}
			line = joined;
			len = used;
		}
		e = line + len;

		n = fymm_line_tokens(&p->lex, toks, FYMM_MAX_TOKENS);
		if (fymm_stmt_acc(p, toks, n, &acc)) {
			fymm_tokens_reset(toks, n);
			continue;
		}
		fymm_tokens_reset(toks, n);

		if (len == 1 && *line == '}') {
			if (!s.depth)
				fymm_diagf(p, true, p->lex.line, 1,
					   "'}' with no composite state open");
			else
				s.depth--;
			continue;
		}
		if (fymm_line_keyword(line, len, "direction", &rest)) {
			direction = fy_gb_intern_string_size(gb, rest,
							     (size_t)(e - rest));
			continue;
		}
		/* a note runs to its `end note` */
		if (fymm_line_keyword(line, len, "note", &rest)) {
			fy_generic target = fy_null, text;
			const char *ns;

			if (fymm_line_keyword(rest, (size_t)(e - rest), "left",
					      &ns) ||
			    fymm_line_keyword(rest, (size_t)(e - rest), "right",
					      &ns)) {
				if (fymm_line_keyword(ns, (size_t)(e - ns),
						      "of", &q))
					ns = q;
				for (q = ns; q < e && *q != ':'; q++)
					;
				target = fymm_trim_text(gb, ns, q);
				rest = q < e ? q + 1 : e;
			}
			text = fymm_trim_text(gb, rest, e);
			if (!*fy_str(text)) {
				/* the block form: lines until `end note` */
				char *buf = NULL;
				size_t pos = 0;

				while (fymm_lex_next_line(&p->lex)) {
					const char *b;
					size_t bl;
					char *nb;

					b = fymm_lex_line(&p->lex, &bl);
					if (bl == 8 &&
					    !strncasecmp(b, "end note", 8))
						break;
					nb = realloc(buf, pos + bl + 2);
					if (!nb)
						break;
					buf = nb;
					if (pos)
						buf[pos++] = '\n';
					memcpy(buf + pos, b, bl);
					pos += bl;
					buf[pos] = '\0';
				}
				if (buf) {
					text = fy_value(gb,
						fy_gb_intern_string(gb, buf));
					free(buf);
				}
			}
			s.notes = fy_append(gb, s.notes,
				fy_mapping(gb, "for", target, "text", text));
			continue;
		}
		/* the display directives, recorded for a consumer to read */
		if (fymm_line_keyword(line, len, "scale", &rest)) {
			config = fy_assoc(gb, config, "scale",
					  fymm_trim_text(gb, rest, e));
			continue;
		}
		if (len >= 4 && !strncasecmp(line, "hide", 4) &&
		    (len == 4 || line[4] == ' ')) {
			config = fy_assoc(gb, config, "hide",
					  fymm_trim_text(gb, line + 4, e));
			continue;
		}
		if (fymm_line_keyword(line, len, "classDef", &rest) ||
		    fymm_line_keyword(line, len, "class", &rest) ||
		    fymm_line_keyword(line, len, "click", &rest) ||
		    fymm_line_keyword(line, len, "style", &rest))
			continue;

		/* a transition, which may carry a label after a colon */
		arrow = NULL;
		for (q = line; q + 3 <= e; q++) {
			if (!memcmp(q, "-->", 3)) {
				arrow = q;
				break;
			}
		}
		if (arrow) {
			colon = fymm_split_colon(arrow + 3, e);
			from = st_endpoint(&s, line, arrow, true);
			to = st_endpoint(&s, arrow + 3, colon ? colon : e,
					 false);
			s.transitions = fy_append(gb, s.transitions,
				fy_mapping(gb,
					"from", from,
					"to", to,
					"label", colon ?
						fymm_trim_text(gb, colon + 1, e) :
						fy_null));
			continue;
		}

		/* `state <name>`, with a description, a kind or a body */
		if (fymm_line_keyword(line, len, "state", &rest)) {
			const char *label = NULL, *kind = NULL;
			const char *nend;
			fy_generic id;

			brace = memchr(rest, '{', (size_t)(e - rest));
			nend = brace ? brace : e;

			/* `state "Description" as Name` */
			if (*rest == '"') {
				const char *close = memchr(rest + 1, '"',
							   (size_t)(nend - rest - 1));
				const char *after = close ? close + 1 : NULL;

				while (after && after < nend &&
				       (*after == ' ' || *after == '\t'))
					after++;
				if (close &&
				    fymm_line_keyword(after,
						      (size_t)(nend - after),
						      "as", &q)) {
					label = fy_gb_intern_string_size(gb,
						rest + 1,
						(size_t)(close - rest - 1));
					rest = q;
				}
			}

			/* `state Name <<fork>>` */
			for (q = rest; q + 2 <= nend; q++) {
				if (memcmp(q, "<<", 2))
					continue;
				{
					const char *close = fymm_memmem(q, (size_t)(nend - q),
								   ">>", 2);

					if (close) {
						kind = fy_gb_intern_string_size(
							gb, q + 2,
							(size_t)(close - q - 2));
						nend = q;
					}
				}
				break;
			}

			/*
			 * What is left is the state's name, and a name is one
			 * word: `state invalid syntax { Y }` names nothing
			 * that could be referred to, and upstream refuses it.
			 */
			{
				const char *ns = rest, *ne2 = nend;

				while (ns < ne2 && (*ns == ' ' || *ns == '\t'))
					ns++;
				while (ne2 > ns && (ne2[-1] == ' ' ||
						    ne2[-1] == '\t'))
					ne2--;
				for (q = ns; q < ne2; q++) {
					if (*q != ' ' && *q != '\t')
						continue;
					fymm_diagf(p, true, p->lex.line, 1,
						   "a state name is one word, not '%.*s'",
						   (int)(ne2 - ns), ns);
					break;
				}
				if (q < ne2)
					continue;
			}

			id = st_state(&s, rest, (size_t)(nend - rest), label,
				      kind);
			/* `state valid { X }` opens and closes on one line;
			 * each word of the body declares a state inside it */
			if (brace && e[-1] == '}') {
				const char *b = brace + 1, *we;

				s.scope[s.depth] = id;
				s.depth++;
				for (q = b; q < e - 1; ) {
					while (q < e - 1 && (*q == ' ' ||
							     *q == '\t'))
						q++;
					for (we = q; we < e - 1 &&
					     *we != ' ' && *we != '\t'; we++)
						;
					if (we > q)
						st_state(&s, q,
							 (size_t)(we - q),
							 NULL, NULL);
					q = we;
				}
				s.depth--;
				continue;
			}
			if (brace) {
				if (s.depth + 1 >= ST_MAX_DEPTH) {
					fymm_diagf(p, true, p->lex.line, 1,
						   "composite states nest too deeply");
					continue;
				}
				s.scope[s.depth] = id;
				s.depth++;
				s.start[s.depth] = fy_null;
				s.end[s.depth] = fy_null;
			}
			continue;
		}

		/* `Name : description` describes a state */
		colon = fymm_split_colon(line, e);
		if (colon) {
			st_state(&s, line, (size_t)(colon - line),
				 fy_gb_intern_string_size(gb, colon + 1,
					(size_t)(e - colon - 1)), NULL);
			continue;
		}

		/* a bare word declares a state */
		for (q = line; q < e && *q != ' ' && *q != '\t'; q++)
			;
		if (q == e) {
			st_state(&s, line, len, NULL, NULL);
			continue;
		}

		fymm_diagf(p, true, p->lex.line, 1,
			   "unknown stateDiagram statement '%.*s'",
			   (int)len, line);
	}

	free(joined);

	if (s.depth)
		fymm_diagf(p, true, p->lex.line, 1,
			   "a composite state was left open at the end of the diagram");

	if (fy_is_invalid(title))
		title = acc.title;

	p->d->model = fy_mapping(gb,
		"type", "stateDiagram",
		"direction", direction,
		"title", title,
		"accTitle", acc.title,
		"accDescr", acc.descr,
		"config", config,
		"states", s.states,
		"transitions", s.transitions,
		"notes", s.notes);
	return 0;
}
