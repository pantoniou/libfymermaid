/*
 * fymm-quadrant.c - the quadrant chart statement parser and model builder
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

/* Strip one layer of double quotes, if the text carries them. */
static fy_generic qd_text(struct fy_generic_builder *gb, const char *s,
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
 * An axis is `x-axis <low> --> <high>`, and either end may be absent:
 * `x-axis Completeness --> Ability` names both, `x-axis Completeness` only
 * the low end.
 */
static fy_generic qd_axis(struct fy_generic_builder *gb, const char *s,
			  const char *e)
{
	const char *arrow = NULL, *q;

	for (q = s; q + 3 <= e; q++) {
		if (!memcmp(q, "-->", 3)) {
			arrow = q;
			break;
		}
	}
	return fy_mapping(gb,
		"low", qd_text(gb, s, arrow ? arrow : e),
		"high", arrow ? qd_text(gb, arrow + 3, e) : fy_null);
}

int fymm_parse_quadrant(struct fymm_parser *p, fy_generic config,
			fy_generic title, struct fymm_token *htoks, int hn)
{
	struct fy_generic_builder *gb = p->d->gb;
	struct fymm_token toks[FYMM_MAX_TOKENS];
	struct fymm_acc acc = { fy_null, fy_null };
	fy_generic points = fy_seq_empty;
	fy_generic xaxis = fy_null, yaxis = fy_null;
	fy_generic labels[4] = { fy_null, fy_null, fy_null, fy_null };
	const char *line, *e, *rest, *colon, *open, *close, *comma;
	size_t len;
	int i, n, q;

	for (i = 1; i < hn; i++) {
		if (htoks[i].type == FYMM_TOK_COLON)
			continue;
		fymm_diagf(p, false, htoks[i].line, htoks[i].col,
			   "ignoring unknown quadrantChart option '%.*s'",
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
				title = qd_text(gb, rest, e);
			continue;
		}
		if (fymm_line_keyword(line, len, "x-axis", &rest)) {
			xaxis = qd_axis(gb, rest, e);
			continue;
		}
		if (fymm_line_keyword(line, len, "y-axis", &rest)) {
			yaxis = qd_axis(gb, rest, e);
			continue;
		}
		for (q = 1; q <= 4; q++) {
			char kw[12];

			snprintf(kw, sizeof(kw), "quadrant-%d", q);
			if (fymm_line_keyword(line, len, kw, &rest)) {
				labels[q - 1] = qd_text(gb, rest, e);
				break;
			}
		}
		if (q <= 4)
			continue;

		/* the styling statements a terminal has no use for */
		if (fymm_line_keyword(line, len, "classDef", &rest) ||
		    fymm_line_keyword(line, len, "class", &rest))
			continue;

		/*
		 * `Name: [x, y]` places a point in the unit square. Styling
		 * may trail it, `[0.75, 0.75] radius: 10, color: #ff0000`,
		 * and the name may carry a class, `Name:::class1:`; neither
		 * reaches a terminal, so both are read and dropped.
		 */
		colon = fymm_split_colon(line, e);
		while (colon && colon + 2 < e && colon[1] == ':' &&
		       colon[2] == ':') {
			const char *next = fymm_split_colon(colon + 3, e);

			if (!next)
				break;
			colon = next;
		}
		open = colon ? memchr(colon, '[', (size_t)(e - colon)) : NULL;
		close = open ? memchr(open, ']', (size_t)(e - open)) : NULL;
		if (colon && open && close) {
			char buf[64];
			double xv, yv;
			char *end;

			comma = memchr(open, ',', (size_t)(close - open));
			if (!comma) {
				fymm_diagf(p, true, p->lex.line, 1,
					   "a point needs both an x and a y");
				continue;
			}
			snprintf(buf, sizeof(buf), "%.*s",
				 (int)(comma - open - 1), open + 1);
			end = NULL;
			xv = strtod(buf, &end);
			if (!end || *end) {
				fymm_diagf(p, true, p->lex.line, 1,
					   "point x '%s' is not a number", buf);
				continue;
			}
			snprintf(buf, sizeof(buf), "%.*s",
				 (int)(close - comma - 1), comma + 1);
			end = NULL;
			yv = strtod(buf, &end);
			if (!end || *end) {
				fymm_diagf(p, true, p->lex.line, 1,
					   "point y '%s' is not a number", buf);
				continue;
			}

			points = fy_append(gb, points,
				fy_mapping(gb,
					"name", qd_text(gb, line,
						memchr(line, ':',
						       (size_t)(e - line))),
					"x", xv, "y", yv));
			continue;
		}

		fymm_diagf(p, true, p->lex.line, 1,
			   "unknown quadrantChart statement '%.*s'",
			   (int)len, line);
	}

	if (fy_is_invalid(title))
		title = acc.title;

	p->d->model = fy_mapping(gb,
		"type", "quadrantChart",
		"title", title,
		"accTitle", acc.title,
		"accDescr", acc.descr,
		"config", config,
		"xAxis", xaxis,
		"yAxis", yaxis,
		"quadrants", fy_sequence(gb, labels[0], labels[1], labels[2],
					 labels[3]),
		"points", points);
	return 0;
}
