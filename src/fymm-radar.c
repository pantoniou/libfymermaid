/*
 * fymm-radar.c - the radar chart statement parser and model builder
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
 * `A["Axis A"]` gives an axis both a name and a label; a bare `A` uses the
 * name for both. Returns the position after the entry.
 */
static const char *rd_entry(struct fy_generic_builder *gb, const char *s,
			    const char *e, fy_generic *namep, fy_generic *labelp)
{
	const char *q, *open, *close;

	while (s < e && (*s == ' ' || *s == '\t'))
		s++;
	for (q = s; q < e && *q != ',' && *q != '[' && *q != '{' &&
		    *q != ' ' && *q != '\t'; q++)
		;
	*namep = fymm_trim_text(gb, s, q);
	*labelp = fy_null;

	while (q < e && (*q == ' ' || *q == '\t'))
		q++;
	if (q < e && *q == '[') {
		open = q;
		close = memchr(open, ']', (size_t)(e - open));
		if (close) {
			const char *ls = open + 1, *le = close;

			if (le - ls >= 2 && *ls == '"' && le[-1] == '"') {
				ls++;
				le--;
			}
			*labelp = fymm_trim_text(gb, ls, le);
			q = close + 1;
		}
	}
	return q;
}

/*
 * A curve carries its values in braces, either in axis order, `{1,2,3}`, or
 * keyed by axis name in any order, `{ C: 3, A: 1, B: 2 }`.
 */
static fy_generic rd_values(struct fymm_parser *p, struct fy_generic_builder *gb,
			    const char *s, const char *e, bool *keyedp)
{
	fy_generic values = fy_seq_empty;
	const char *comma, *colon, *item_end;
	char buf[64], *end;
	double v;

	*keyedp = false;
	for (;;) {
		comma = memchr(s, ',', (size_t)(e - s));
		item_end = comma ? comma : e;

		colon = memchr(s, ':', (size_t)(item_end - s));
		if (colon) {
			*keyedp = true;
			snprintf(buf, sizeof(buf), "%.*s",
				 (int)(item_end - colon - 1), colon + 1);
		} else {
			snprintf(buf, sizeof(buf), "%.*s",
				 (int)(item_end - s), s);
		}
		end = NULL;
		v = strtod(buf, &end);
		while (end && (*end == ' ' || *end == '\t'))
			end++;
		if (!end || end == buf || *end) {
			fymm_diagf(p, true, p->lex.line, 1,
				   "curve value '%s' is not a number", buf);
			return fy_seq_empty;
		}
		values = fy_append(gb, values,
			fy_mapping(gb,
				"axis", colon ? fymm_trim_text(gb, s, colon) :
						fy_null,
				"value", v));
		if (!comma)
			break;
		s = comma + 1;
	}
	return values;
}

int fymm_parse_radar(struct fymm_parser *p, fy_generic config, fy_generic title,
		     struct fymm_token *htoks, int hn)
{
	struct fy_generic_builder *gb = p->d->gb;
	struct fymm_token toks[FYMM_MAX_TOKENS];
	struct fymm_acc acc = { fy_null, fy_null };
	fy_generic axes = fy_seq_empty, curves = fy_seq_empty;
	fy_generic name, label;
	const char *line, *e, *rest, *q, *open, *close;
	bool keyed;
	size_t len;
	int i, n;

	for (i = 1; i < hn; i++) {
		if (htoks[i].type == FYMM_TOK_COLON)
			continue;
		fymm_diagf(p, false, htoks[i].line, htoks[i].col,
			   "ignoring unknown radar option '%.*s'",
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
		/* the display options are recorded in the config */
		if (fymm_line_keyword(line, len, "ticks", &rest) ||
		    fymm_line_keyword(line, len, "min", &rest) ||
		    fymm_line_keyword(line, len, "max", &rest)) {
			char kw[8];
			double v;
			char *end2;
			char buf[64];

			snprintf(kw, sizeof(kw), "%.*s",
				 (int)(strchr(line, ' ') - line), line);
			snprintf(buf, sizeof(buf), "%.*s", (int)(e - rest),
				 rest);
			end2 = NULL;
			v = strtod(buf, &end2);
			if (end2 && end2 != buf && !*end2)
				config = fy_assoc(gb, config, kw, v);
			else
				fymm_diagf(p, true, p->lex.line, 1,
					   "'%s' takes a number, got '%s'", kw,
					   buf);
			continue;
		}
		if (fymm_line_keyword(line, len, "showLegend", &rest)) {
			config = fy_assoc(gb, config, "showLegend",
					  !strncasecmp(rest, "true", 4));
			continue;
		}
		if (fymm_line_keyword(line, len, "graticule", &rest)) {
			config = fy_assoc(gb, config, "graticule",
					  fymm_trim_text(gb, rest, e));
			continue;
		}

		if (fymm_line_keyword(line, len, "axis", &rest)) {
			for (q = rest; q < e; ) {
				q = rd_entry(gb, q, e, &name, &label);
				if (fy_is_valid(name) &&
				    *fy_str(name))
					axes = fy_append(gb, axes,
						fy_mapping(gb, "name", name,
							   "label", label));
				while (q < e && *q != ',')
					q++;
				if (q < e)
					q++;
			}
			continue;
		}

		if (fymm_line_keyword(line, len, "curve", &rest)) {
			open = memchr(rest, '{', (size_t)(e - rest));
			close = open ? memchr(open, '}', (size_t)(e - open)) :
				       NULL;
			if (!open || !close) {
				fymm_diagf(p, true, p->lex.line, 1,
					   "a curve needs its values in braces");
				continue;
			}
			rd_entry(gb, rest, open, &name, &label);
			curves = fy_append(gb, curves,
				fy_mapping(gb,
					"name", name,
					"label", label,
					"values", rd_values(p, gb, open + 1,
							    close, &keyed),
					"keyed", keyed));
			continue;
		}

		fymm_diagf(p, true, p->lex.line, 1,
			   "unknown radar statement '%.*s'", (int)len, line);
	}

	if (fy_is_invalid(title))
		title = acc.title;

	p->d->model = fy_mapping(gb,
		"type", "radar",
		"title", title,
		"accTitle", acc.title,
		"accDescr", acc.descr,
		"config", config,
		"axes", axes,
		"curves", curves);
	return 0;
}
