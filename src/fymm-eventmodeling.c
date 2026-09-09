/*
 * fymm-eventmodeling.c - the event modelling statement parser
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

/* The frame kinds, with the spellings mermaid accepts for each. */
static const struct {
	const char *word;
	const char *kind;
} em_kinds[] = {
	{ "ui",		"ui" },
	{ "cmd",	"command" },
	{ "command",	"command" },
	{ "evt",	"event" },
	{ "event",	"event" },
	{ "view",	"view" },
	{ "pcr",	"processor" },
	{ "proc",	"processor" },
	{ "processor",	"processor" },
	{ "rmo",	"readmodel" },
	{ "readmodel",	"readmodel" },
	{ "aggregate",	"aggregate" },
	{ "job",	"job" },
	{ "trigger",	"trigger" },
};

#define EM_KIND_COUNT (sizeof(em_kinds) / sizeof(em_kinds[0]))

/*
 * The body of a data block is everything between its braces, which may open on
 * the `data` line or on any line after it, and may span as many lines as it
 * takes to balance. Gather it verbatim; nothing here interprets it.
 */
static fy_generic em_body(struct fymm_parser *p, const char *open,
			  const char *e)
{
	struct fy_generic_builder *gb = p->d->gb;
	char *buf = NULL, *nbuf;
	size_t size = 0, alloc = 0;
	const char *line;
	size_t len, i;
	int depth = 0;
	fy_generic body;

	for (;;) {
		if (!open) {
			if (!fymm_lex_next_line(&p->lex))
				break;
			line = fymm_lex_line(&p->lex, &len);
			open = line;
			e = line + len;
		}

		for (i = 0; open + i < e; i++) {
			if (open[i] == '{')
				depth++;
			else if (open[i] == '}')
				depth--;
		}

		if (size + (size_t)(e - open) + 2 > alloc) {
			alloc = (size + (size_t)(e - open) + 2) * 2;
			nbuf = realloc(buf, alloc);
			if (!nbuf) {
				free(buf);
				return fy_null;
			}
			buf = nbuf;
		}
		if (size)
			buf[size++] = '\n';
		memcpy(buf + size, open, (size_t)(e - open));
		size += (size_t)(e - open);

		if (depth <= 0)
			break;
		open = NULL;
	}

	if (!buf)
		return fy_null;
	body = fymm_trim_text(gb, buf, buf + size);
	free(buf);
	return body;
}

/*
 * A frame is `<lane> <number> <kind> <Name>`, where the lane is `tf` for the
 * timeline and `rf` for a read model. It may carry inline data in braces, a
 * reference to a data block in double brackets, and links to the frames it
 * follows after `->>`.
 */
int fymm_parse_eventmodeling(struct fymm_parser *p, fy_generic config,
			     fy_generic title, struct fymm_token *htoks, int hn)
{
	struct fy_generic_builder *gb = p->d->gb;
	struct fymm_token toks[FYMM_MAX_TOKENS];
	struct fymm_acc acc = { fy_null, fy_null };
	fy_generic_sized_string input;
	fy_generic frames = fy_seq_empty, blocks = fy_seq_empty;
	fy_generic data, refs, links;
	const char *line, *e, *rest, *q, *open, *close;
	size_t len, i;
	int n;

	for (i = 1; i < (size_t)hn; i++) {
		if (htoks[i].type == FYMM_TOK_COLON)
			continue;
		fymm_diagf(p, false, htoks[i].line, htoks[i].col,
			   "ignoring unknown eventmodeling option '%.*s'",
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

		/* `data Name { ... }` declares a block a frame may reference */
		if (fymm_line_keyword(line, len, "data", &rest)) {
			fy_generic body;

			open = memchr(rest, '{', (size_t)(e - rest));
			body = em_body(p, open, e);
			blocks = fy_append(gb, blocks,
				fy_mapping(gb,
					"name", fymm_trim_text(gb, rest,
							       open ? open : e),
					"body", body));
			fymm_tokens_reset(toks, n);
			continue;
		}

		if (n < 4 || (!fymm_token_ieq(&toks[0], "tf") &&
			      !fymm_token_ieq(&toks[0], "rf"))) {
			fymm_diagf(p, true, p->lex.line, 1,
				   "unknown eventmodeling statement '%.*s'",
				   (int)len, line);
			fymm_tokens_reset(toks, n);
			continue;
		}

		for (i = 0; i < EM_KIND_COUNT; i++) {
			if (fymm_token_ieq(&toks[2], em_kinds[i].word))
				break;
		}
		if (i == EM_KIND_COUNT) {
			fymm_diagf(p, true, toks[2].line, toks[2].col,
				   "'%.*s' is not a frame kind",
				   (int)toks[2].len, toks[2].text);
			fymm_tokens_reset(toks, n);
			continue;
		}

		/*
		 * The extras follow the name on the raw line, so measure from
		 * the token's column rather than from the trimmed line.
		 */
		rest = p->lex.ls + (toks[3].col - 1) + toks[3].len;
		e = p->lex.le;
		if (rest > e)
			rest = e;
		data = fy_null;
		refs = fy_null;
		links = fy_seq_empty;

		open = memchr(rest, '{', (size_t)(e - rest));
		close = open ? fymm_memrchr(open, '}', (size_t)(e - open)) : NULL;
		if (open && close) {
			input.data = open;
			input.size = (size_t)(close + 1 - open);
			data = fy_parse(gb, input,
					FYMM_YAML_PARSE_FLAGS |
					FYOPPF_INPUT_TYPE_STRING, NULL);
			if (!fy_is_mapping(data))
				data = fymm_trim_text(gb, open, close + 1);
		}

		open = fymm_memmem(rest, (size_t)(e - rest), "[[", 2);
		close = open ? fymm_memmem(open, (size_t)(e - open), "]]", 2) : NULL;
		if (open && close)
			refs = fymm_trim_text(gb, open + 2, close);

		/* `->> 02 ->> 03` names the frames this one follows */
		for (q = rest; q + 3 <= e; q++) {
			if (memcmp(q, "->>", 3))
				continue;
			{
				const char *ns = q + 3, *ne;

				while (ns < e && (*ns == ' ' || *ns == '\t'))
					ns++;
				for (ne = ns; ne < e &&
				     !isspace((unsigned char)*ne); ne++)
					;
				if (ne > ns)
					links = fy_append(gb, links,
						fymm_trim_text(gb, ns, ne));
				q = ne - 1;
			}
		}

		frames = fy_append(gb, frames,
			fy_mapping(gb,
				"lane", fymm_trim_text(gb, toks[0].text,
						       toks[0].text +
						       toks[0].len),
				"number", fymm_trim_text(gb, toks[1].text,
							 toks[1].text +
							 toks[1].len),
				"kind", em_kinds[i].kind,
				"name", fymm_trim_text(gb, toks[3].text,
						       toks[3].text +
						       toks[3].len),
				"data", data,
				"ref", refs,
				"follows", links));
		fymm_tokens_reset(toks, n);
	}

	if (fy_is_invalid(title))
		title = acc.title;

	p->d->model = fy_mapping(gb,
		"type", "eventmodeling",
		"title", title,
		"accTitle", acc.title,
		"accDescr", acc.descr,
		"config", config,
		"frames", frames,
		"data", blocks);
	return 0;
}
