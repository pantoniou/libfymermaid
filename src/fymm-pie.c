/*
 * fymm-pie.c - the pie statement parser and model builder
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

/*
 * A slice is `"label" : value`. The label is quoted, so it may hold anything;
 * the value is a number. Mermaid rejects a negative value, and so does this.
 */
static void pie_stmt_slice(struct fymm_parser *p, struct fymm_token *toks,
			   int n, fy_generic *slices)
{
	struct fy_generic_builder *gb = p->d->gb;
	const char *label;
	char buf[64], *end;
	double value;

	if (toks[0].type != FYMM_TOK_STRING) {
		fymm_diagf(p, true, toks[0].line, toks[0].col,
			   "expected a quoted slice label, got '%.*s'",
			   (int)toks[0].len, toks[0].text);
		return;
	}
	if (n < 2 || toks[1].type != FYMM_TOK_COLON) {
		fymm_diagf(p, true, toks[0].line, toks[0].col,
			   "expected ':' after the slice label");
		return;
	}
	if (n < 3) {
		fymm_diagf(p, true, toks[0].line, toks[0].col,
			   "missing value for slice '%.*s'",
			   (int)toks[0].len, toks[0].text);
		return;
	}

	snprintf(buf, sizeof(buf), "%.*s", (int)toks[2].len, toks[2].text);
	end = NULL;
	value = strtod(buf, &end);
	if (!end || *end) {
		fymm_diagf(p, true, toks[2].line, toks[2].col,
			   "slice value '%s' is not a number", buf);
		return;
	}
	if (value < 0.0) {
		fymm_diagf(p, true, toks[2].line, toks[2].col,
			   "slice value '%s' is negative", buf);
		return;
	}

	label = fy_gb_intern_string_size(gb, toks[0].text, toks[0].len);
	*slices = fy_append(gb, *slices,
		fy_mapping(gb, "label", label, "value", value));
}

int fymm_parse_pie(struct fymm_parser *p, fy_generic config, fy_generic title,
		   struct fymm_token *htoks, int hn)
{
	struct fy_generic_builder *gb = p->d->gb;
	struct fymm_token toks[FYMM_MAX_TOKENS];
	struct fymm_acc acc = { fy_null, fy_null };
	fy_generic slices = fy_seq_empty;
	fy_generic header_title = fy_invalid;
	bool show_data = false;
	int i, n;

	/*
	 * The header is `pie`, then `showData` and `title <text>` in any
	 * order. A title runs to the end of the line and may hold a colon, so
	 * it is read from the line rather than from the tokens.
	 */
	for (i = 1; i < hn; i++) {
		if (htoks[i].type == FYMM_TOK_COLON)
			continue;
		if (fymm_token_ieq(&htoks[i], "showData")) {
			show_data = true;
			continue;
		}
		if (fymm_token_ieq(&htoks[i], "title")) {
			header_title = fy_value(gb,
				fymm_rest_text(p, htoks[i].col +
					       (int)htoks[i].len));
			break;
		}
		fymm_diagf(p, false, htoks[i].line, htoks[i].col,
			   "ignoring unknown pie option '%.*s'",
			   (int)htoks[i].len, htoks[i].text);
	}

	while (fymm_lex_next_line(&p->lex)) {
		n = fymm_line_tokens(&p->lex, toks, FYMM_MAX_TOKENS);
		if (!n)
			continue;

		if (fymm_stmt_acc(p, toks, n, &acc))
			;			/* handled */
		else if (fymm_token_ieq(&toks[0], "title"))
			header_title = fy_value(gb,
				fymm_rest_text(p, toks[0].col +
					       (int)toks[0].len));
		else if (fymm_token_ieq(&toks[0], "showData"))
			show_data = true;
		else
			pie_stmt_slice(p, toks, n, &slices);

		fymm_tokens_reset(toks, n);
	}

	if (fy_is_invalid(fy_get(config, "showData")))
		config = fy_assoc(gb, config, "showData", show_data);

	/* the frontmatter title wins over the one on the header line */
	if (fy_is_invalid(title))
		title = fy_is_valid(header_title) ? header_title : acc.title;

	p->d->model = fy_mapping(gb,
		"type", "pie",
		"title", title,
		"accTitle", acc.title,
		"accDescr", acc.descr,
		"config", config,
		"slices", slices);
	return 0;
}
