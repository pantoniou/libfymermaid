/*
 * fymm-ishikawa.c - the cause and effect statement parser and model builder
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

#include <stdlib.h>
#include <string.h>

#include "fymm-internal.h"

/* The indent of a line, in columns. */
static int ik_indent(const char *s, const char *e)
{
	int col = 0;

	for (; s < e; s++) {
		if (*s == ' ')
			col++;
		else if (*s == '\t')
			col = (col + 8) & ~7;
		else
			break;
	}
	return col;
}

/*
 * The first line names the effect, whatever it is indented by; a diagram in
 * the wild indents it more than the causes that follow. Of the rest, the
 * shallowest indent is a category and anything deeper is one of its causes.
 */
int fymm_parse_ishikawa(struct fymm_parser *p, fy_generic config,
			fy_generic title, struct fymm_token *htoks, int hn)
{
	struct fy_generic_builder *gb = p->d->gb;
	struct fymm_token toks[FYMM_MAX_TOKENS];
	struct fymm_acc acc = { fy_null, fy_null };
	fy_generic categories = fy_seq_empty, causes = fy_seq_empty;
	fy_generic effect = fy_null, category = fy_null;
	const char *line, *raw;
	int indent, category_indent = -1;
	bool open_category = false;
	size_t len;
	int i, n;

	for (i = 1; i < hn; i++) {
		if (htoks[i].type == FYMM_TOK_COLON)
			continue;
		fymm_diagf(p, false, htoks[i].line, htoks[i].col,
			   "ignoring unknown ishikawa option '%.*s'",
			   (int)htoks[i].len, htoks[i].text);
	}

#define IK_CLOSE_CATEGORY() \
	do { \
		if (open_category) { \
			categories = fy_append(gb, categories, \
				fy_mapping(gb, "name", category, \
					   "causes", causes)); \
			causes = fy_seq_empty; \
		} \
	} while (0)

	while (fymm_lex_next_line(&p->lex)) {
		raw = p->lex.ls;
		line = fymm_lex_line(&p->lex, &len);
		if (!len)
			continue;
		indent = ik_indent(raw, line);

		n = fymm_line_tokens(&p->lex, toks, FYMM_MAX_TOKENS);
		if (fymm_stmt_acc(p, toks, n, &acc)) {
			fymm_tokens_reset(toks, n);
			continue;
		}
		fymm_tokens_reset(toks, n);

		if (fy_is_null(effect)) {
			effect = fymm_trim_text(gb, line, line + len);
			continue;
		}

		if (category_indent < 0 || indent <= category_indent) {
			IK_CLOSE_CATEGORY();
			category_indent = indent;
			category = fymm_trim_text(gb, line, line + len);
			open_category = true;
			continue;
		}

		causes = fy_append(gb, causes,
				   fymm_trim_text(gb, line, line + len));
	}

	IK_CLOSE_CATEGORY();
#undef IK_CLOSE_CATEGORY

	if (fy_is_invalid(title))
		title = acc.title;

	p->d->model = fy_mapping(gb,
		"type", "ishikawa",
		"title", title,
		"accTitle", acc.title,
		"accDescr", acc.descr,
		"config", config,
		"effect", effect,
		"categories", categories);
	return 0;
}
