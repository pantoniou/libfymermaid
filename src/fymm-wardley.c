/*
 * fymm-wardley.c - the Wardley map statement parser
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
 * Names may be quoted so that they can hold anything; the quotes are notation,
 * not part of the name.
 */
static fy_generic wl_name(struct fy_generic_builder *gb, const char *s,
			  const char *e)
{
	while (s < e && (*s == ' ' || *s == '\t'))
		s++;
	while (e > s && (e[-1] == ' ' || e[-1] == '\t'))
		e--;
	if (e - s >= 2 && *s == '"' && e[-1] == '"') {
		s++;
		e--;
	}
	return fymm_trim_text(gb, s, e);
}

/*
 * A coordinate is `[visibility, evolution]`, or inside a pipeline just
 * `[evolution]`. Returns how many numbers were found, or -1 when there is no
 * bracket at all.
 */
static int wl_coords(const char *s, const char *e, double *a, double *b,
		     const char **endp)
{
	const char *open, *close;
	char buf[64], *p;
	size_t len;
	int n;

	open = memchr(s, '[', (size_t)(e - s));
	close = open ? memchr(open, ']', (size_t)(e - open)) : NULL;
	if (!open || !close)
		return -1;

	len = (size_t)(close - open - 1);
	if (len >= sizeof(buf))
		return -1;
	memcpy(buf, open + 1, len);
	buf[len] = '\0';

	*a = strtod(buf, &p);
	if (p == buf)
		return -1;
	n = 1;
	while (*p == ' ' || *p == ',')
		p++;
	if (*p) {
		char *q;
		double v = strtod(p, &q);

		if (q != p) {
			*b = v;
			n = 2;
		}
	}
	if (endp)
		*endp = close + 1;
	return n;
}

/* the placement statements all share the shape `<kind> <name> [x, y]` */
static fy_generic wl_placed(struct fy_generic_builder *gb, const char *kind,
			    const char *s, const char *e)
{
	const char *open = memchr(s, '[', (size_t)(e - s));
	double vis = 0.0, evo = 0.0;
	int n;

	n = wl_coords(s, e, &vis, &evo, NULL);
	if (n < 2) {
		evo = vis;
		vis = 0.5;
	}
	return fy_mapping(gb,
		"kind", kind,
		"name", wl_name(gb, s, open ? open : e),
		"visibility", fy_float(vis),
		"evolution", fy_float(evo));
}

/*
 * A link is any line carrying one of the map's connectors. The names around it
 * may hold spaces and hyphens, so the connector is found by scanning for the
 * longest match rather than by tokenizing, and `foo-bar->baz` stays one link
 * between two hyphenated names.
 */
static const char *const wl_ops[] = {
	"+<>", "+>", "+<", "->", "<-", "--",
};

#define WL_OP_COUNT (sizeof(wl_ops) / sizeof(wl_ops[0]))

static const char *wl_find_op(const char *s, const char *e, size_t *oplen)
{
	const char *p;
	size_t i, len;

	for (p = s; p < e; p++) {
		for (i = 0; i < WL_OP_COUNT; i++) {
			len = strlen(wl_ops[i]);
			if ((size_t)(e - p) < len ||
			    memcmp(p, wl_ops[i], len))
				continue;
			*oplen = len;
			return p;
		}
	}
	return NULL;
}

/*
 * Everything in a Wardley map is a statement of one line, save the pipeline
 * block, whose braces hold components given only their evolution.
 */
int fymm_parse_wardley(struct fymm_parser *p, fy_generic config,
		       fy_generic title, struct fymm_token *htoks, int hn)
{
	struct fy_generic_builder *gb = p->d->gb;
	struct fymm_token toks[FYMM_MAX_TOKENS];
	struct fymm_acc acc = { fy_null, fy_null };
	fy_generic nodes = fy_seq_empty, links = fy_seq_empty;
	fy_generic notes = fy_seq_empty, stages = fy_seq_empty;
	fy_generic pipelines = fy_seq_empty, pipe = fy_null;
	fy_generic pipe_members = fy_seq_empty;
	const char *line, *e, *rest, *op, *q, *semi;
	double vis, evo;
	size_t len, oplen;
	int n;

	for (n = 1; n < hn; n++) {
		if (htoks[n].type == FYMM_TOK_COLON)
			continue;
		fymm_diagf(p, false, htoks[n].line, htoks[n].col,
			   "ignoring unknown wardley option '%.*s'",
			   (int)htoks[n].len, htoks[n].text);
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

		/* the close of a pipeline block */
		if (*line == '}') {
			if (!fy_is_invalid(pipe) && !fy_is_null(pipe)) {
				pipelines = fy_append(gb, pipelines,
					fy_assoc(gb, pipe, "members",
						 pipe_members));
				pipe = fy_null;
				pipe_members = fy_seq_empty;
			}
			continue;
		}

		if (fymm_line_keyword(line, len, "title", &rest)) {
			if (fy_is_invalid(title) || fy_is_null(title))
				title = fymm_trim_text(gb, rest, e);
			continue;
		}

		/*
		 * The evolution axis names the stages the map is drawn
		 * against, each optionally with a second label after a slash
		 * and its right edge after an `@`.
		 */
		if (fymm_line_keyword(line, len, "evolution", &rest)) {
			const char *s = rest;

			while (s < e) {
				const char *sep = memmem(s, (size_t)(e - s),
							 "->", 2);
				const char *ee = sep ? sep : e;
				const char *at = memchr(s, '@',
							(size_t)(ee - s));
				fy_generic edge = fy_null;

				if (at)
					edge = fy_float(strtod(at + 1, NULL));
				stages = fy_append(gb, stages,
					fy_mapping(gb,
						"name", wl_name(gb, s,
								at ? at : ee),
						"edge", edge));
				if (!sep)
					break;
				s = sep + 2;
			}
			continue;
		}

		if (fymm_line_keyword(line, len, "size", &rest)) {
			/* a pixel canvas size; the terminal has its own */
			continue;
		}

		if (fymm_line_keyword(line, len, "pipeline", &rest)) {
			const char *brace = memchr(rest, '{',
						   (size_t)(e - rest));

			pipe = fy_mapping(gb,
				"name", wl_name(gb, rest, brace ? brace : e));
			pipe_members = fy_seq_empty;
			continue;
		}

		if (fymm_line_keyword(line, len, "evolve", &rest)) {
			const char *ne = e;

			/* the target evolution trails the name */
			while (ne > rest && !isspace((unsigned char)ne[-1]))
				ne--;
			nodes = fy_append(gb, nodes,
				fy_mapping(gb,
					"kind", "evolve",
					"name", wl_name(gb, rest, ne),
					"visibility", fy_null,
					"evolution",
					fy_float(strtod(ne, NULL))));
			continue;
		}

		if (fymm_line_keyword(line, len, "annotations", &rest))
			continue;

		/*
		 * A note carries its text quoted; an annotation leads with its
		 * number and coordinate and quotes the text after them. Taking
		 * the quoted run when there is one covers both.
		 */
		if (fymm_line_keyword(line, len, "note", &rest) ||
		    fymm_line_keyword(line, len, "annotation", &rest)) {
			const char *qs = memchr(rest, '"', (size_t)(e - rest));
			const char *qe = qs ? memchr(qs + 1, '"',
						     (size_t)(e - qs - 1)) :
					      NULL;
			const char *open = memchr(rest, '[',
						  (size_t)(e - rest));

			vis = 0.5;
			evo = 0.5;
			(void)wl_coords(rest, e, &vis, &evo, NULL);
			notes = fy_append(gb, notes,
				fy_mapping(gb,
					"text", qs && qe ?
						fymm_trim_text(gb, qs + 1, qe) :
						fymm_trim_text(gb, rest,
							open ? open : e),
					"visibility", fy_float(vis),
					"evolution", fy_float(evo)));
			continue;
		}

		{
			static const char *const kinds[] = {
				"component", "anchor", "accelerator",
				"deaccelerator",
			};
			size_t k;

			for (k = 0; k < sizeof(kinds) / sizeof(kinds[0]); k++) {
				fy_generic node;

				if (!fymm_line_keyword(line, len, kinds[k],
						       &rest))
					continue;
				node = wl_placed(gb, kinds[k], rest, e);
				if (!fy_is_null(pipe))
					pipe_members = fy_append(gb,
								 pipe_members,
								 node);
				else
					nodes = fy_append(gb, nodes, node);
				break;
			}
			if (k < sizeof(kinds) / sizeof(kinds[0]))
				continue;
		}

		op = wl_find_op(line, e, &oplen);
		if (op && op > line) {
			semi = memchr(op, ';', (size_t)(e - op));
			q = op + oplen;
			links = fy_append(gb, links,
				fy_mapping(gb,
					"from", wl_name(gb, line, op),
					"to", wl_name(gb, q, semi ? semi : e),
					"op", fymm_trim_text(gb, op, op + oplen),
					"label", semi ?
						fymm_trim_text(gb, semi + 1,
							       e) :
						fy_null));
			continue;
		}

		fymm_diagf(p, true, p->lex.line, 1,
			   "unknown wardley statement '%.*s'", (int)len, line);
	}

	if (!fy_is_null(pipe) && !fy_is_invalid(pipe))
		pipelines = fy_append(gb, pipelines,
				      fy_assoc(gb, pipe, "members",
					       pipe_members));

	if (fy_is_invalid(title))
		title = acc.title;

	p->d->model = fy_mapping(gb,
		"type", "wardley",
		"title", title,
		"accTitle", acc.title,
		"accDescr", acc.descr,
		"config", config,
		"stages", stages,
		"nodes", nodes,
		"links", links,
		"notes", notes,
		"pipelines", pipelines);
	return 0;
}
