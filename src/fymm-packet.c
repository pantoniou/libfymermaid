/*
 * fymm-packet.c - the packet diagram statement parser and model builder
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
 * A packet is a run of bit fields, each `<start>-<end>: "label"`, `<bit>:
 * "label"`, or `+<count>: "label"` to continue from wherever the last one
 * ended. The fields must be continuous and in order: a packet with a hole in
 * it describes nothing, and upstream refuses one.
 */
int fymm_parse_packet(struct fymm_parser *p, fy_generic config,
		      fy_generic title, struct fymm_token *htoks, int hn)
{
	struct fy_generic_builder *gb = p->d->gb;
	struct fymm_token toks[FYMM_MAX_TOKENS];
	struct fymm_acc acc = { fy_null, fy_null };
	fy_generic fields = fy_seq_empty;
	const char *line, *e, *rest, *colon, *dash;
	long long next = 0, start, end;
	char buf[64], *ep;
	size_t len;
	int i, n;

	for (i = 1; i < hn; i++) {
		if (htoks[i].type == FYMM_TOK_COLON)
			continue;
		fymm_diagf(p, false, htoks[i].line, htoks[i].col,
			   "ignoring unknown packet option '%.*s'",
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

		if (fymm_line_keyword(line, len, "title", &rest)) {
			if (fy_is_invalid(title))
				title = fymm_trim_text(gb, rest, e);
			continue;
		}

		colon = memchr(line, ':', len);
		if (!colon) {
			fymm_diagf(p, true, p->lex.line, 1,
				   "unknown packet statement '%.*s'",
				   (int)len, line);
			continue;
		}

		if (*line == '+') {
			/* `+n` takes the next n bits */
			snprintf(buf, sizeof(buf), "%.*s",
				 (int)(colon - line - 1), line + 1);
			ep = NULL;
			end = strtoll(buf, &ep, 10);
			if (!ep || ep == buf || *ep) {
				fymm_diagf(p, true, p->lex.line, 1,
					   "'%s' is not a bit count", buf);
				continue;
			}
			if (end <= 0) {
				fymm_diagf(p, true, p->lex.line, 1,
					   "a field covers at least one bit");
				continue;
			}
			start = next;
			end = start + end - 1;
		} else {
			dash = memchr(line, '-', (size_t)(colon - line));
			snprintf(buf, sizeof(buf), "%.*s",
				 (int)((dash ? dash : colon) - line), line);
			ep = NULL;
			start = strtoll(buf, &ep, 10);
			if (!ep || ep == buf || *ep) {
				fymm_diagf(p, true, p->lex.line, 1,
					   "'%s' is not a bit number", buf);
				continue;
			}
			if (dash) {
				snprintf(buf, sizeof(buf), "%.*s",
					 (int)(colon - dash - 1), dash + 1);
				ep = NULL;
				end = strtoll(buf, &ep, 10);
				if (!ep || ep == buf || *ep) {
					fymm_diagf(p, true, p->lex.line, 1,
						   "'%s' is not a bit number",
						   buf);
					continue;
				}
			} else {
				end = start;
			}
			if (end < start) {
				fymm_diagf(p, true, p->lex.line, 1,
					   "field %lld-%lld ends before it starts",
					   start, end);
				continue;
			}
			if (start != next) {
				fymm_diagf(p, true, p->lex.line, 1,
					   "field starts at bit %lld, but bit %lld is the next free one",
					   start, next);
				continue;
			}
		}

		{
			const char *ls = colon + 1, *le = e;

			while (ls < le && (*ls == ' ' || *ls == '\t'))
				ls++;
			while (le > ls && (le[-1] == ' ' || le[-1] == '\t'))
				le--;
			if (le - ls >= 2 && *ls == '"' && le[-1] == '"') {
				ls++;
				le--;
			}
			fields = fy_append(gb, fields,
				fy_mapping(gb,
					"start", start,
					"end", end,
					"label", fymm_trim_text(gb, ls, le)));
		}
		next = end + 1;
	}

	if (fy_is_invalid(title))
		title = acc.title;

	p->d->model = fy_mapping(gb,
		"type", "packet",
		"title", title,
		"accTitle", acc.title,
		"accDescr", acc.descr,
		"config", config,
		"fields", fields);
	return 0;
}
