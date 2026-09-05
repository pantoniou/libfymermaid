/*
 * fymm-xychart.c - the xy chart statement parser and model builder
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

/* Strip one layer of double quotes and the surrounding space. */
static void xy_trim(const char **sp, const char **ep)
{
	const char *s = *sp, *e = *ep;

	while (s < e && (*s == ' ' || *s == '\t'))
		s++;
	while (e > s && (e[-1] == ' ' || e[-1] == '\t'))
		e--;
	if (e - s >= 2 && *s == '"' && e[-1] == '"') {
		s++;
		e--;
	}
	*sp = s;
	*ep = e;
}

/*
 * Read a number that fills [s, e) exactly. Upstream rejects a range or a data
 * point that is not wholly a number, so a trailing character is an error and
 * not a value with something after it.
 */
static bool xy_number(const char *s, const char *e, double *vp)
{
	char buf[64], *end;

	xy_trim(&s, &e);
	if (s >= e || (size_t)(e - s) >= sizeof(buf))
		return false;
	snprintf(buf, sizeof(buf), "%.*s", (int)(e - s), s);
	end = NULL;
	*vp = strtod(buf, &end);
	return end && end != buf && !*end;
}

/*
 * Find the `[ ... ]` list in [s, e). Returns false when a bracket is
 * unbalanced, which upstream treats as an error rather than as text.
 */
static bool xy_brackets(const char *s, const char *e, const char **openp,
			const char **closep)
{
	const char *open = memchr(s, '[', (size_t)(e - s));
	const char *close;

	*openp = NULL;
	*closep = NULL;
	if (!open)
		return true;
	close = memchr(open + 1, ']', (size_t)(e - open - 1));
	if (!close)
		return false;
	/* a second opening bracket inside the list is unbalanced */
	if (memchr(open + 1, '[', (size_t)(close - open - 1)))
		return false;
	*openp = open;
	*closep = close;
	return true;
}

/*
 * Read the comma separated body of a list. @numeric says whether each entry
 * must be a number, in which case an entry may carry a quoted label after it:
 * `[10 "A", 20 "B"]`.
 */
static bool xy_list(struct fymm_parser *p, struct fy_generic_builder *gb,
		    const char *s, const char *e, bool numeric,
		    fy_generic *outp, const char *what)
{
	fy_generic items = fy_seq_empty;
	const char *comma, *item_end, *quote;
	double v;

	{
		const char *bs = s, *be = e;

		xy_trim(&bs, &be);
		if (bs >= be) {
			fymm_diagf(p, true, p->lex.line, 1, "%s is empty",
				   what);
			return false;
		}
	}

	for (;;) {
		comma = memchr(s, ',', (size_t)(e - s));
		item_end = comma ? comma : e;

		if (!numeric) {
			const char *is = s, *ie = item_end;

			xy_trim(&is, &ie);
			if (is >= ie) {
				fymm_diagf(p, true, p->lex.line, 1,
					   "%s has an empty entry", what);
				return false;
			}
			items = fy_append(gb, items,
				fy_value(gb, fy_gb_intern_string_size(gb, is,
						(size_t)(ie - is))));
		} else {
			const char *is = s, *ie = item_end;
			fy_generic label = fy_null;

			xy_trim(&is, &ie);
			if (is >= ie) {
				fymm_diagf(p, true, p->lex.line, 1,
					   "%s has an empty entry", what);
				return false;
			}
			/* `10 "A"` is a value with a label of its own */
			quote = memchr(is, '"', (size_t)(ie - is));
			if (quote) {
				const char *ls = quote, *le = ie;

				xy_trim(&ls, &le);
				label = fy_value(gb,
					fy_gb_intern_string_size(gb, ls,
						(size_t)(le - ls)));
				ie = quote;
			}
			if (!xy_number(is, ie, &v)) {
				fymm_diagf(p, true, p->lex.line, 1,
					   "%s entry '%.*s' is not a number",
					   what, (int)(ie - is), is);
				return false;
			}
			items = fy_append(gb, items,
				fy_mapping(gb, "value", v, "label", label));
		}

		if (!comma)
			break;
		s = comma + 1;
	}
	*outp = items;
	return true;
}

/*
 * An axis is `x-axis [name] [range or categories]`. A range is
 * `<min> --> <max>`, both numbers. Categories are a list, which the y axis
 * does not accept.
 */
static fy_generic xy_axis(struct fymm_parser *p, struct fy_generic_builder *gb,
			  const char *s, const char *e, bool allow_categories,
			  bool *okp)
{
	const char *open, *close, *arrow, *q, *ns = s, *ne = e;
	fy_generic categories = fy_null, name = fy_null;
	double lo = 0.0, hi = 0.0;
	bool have_range = false;

	*okp = true;

	if (!xy_brackets(s, e, &open, &close)) {
		fymm_diagf(p, true, p->lex.line, 1,
			   "the axis has an unbalanced bracket");
		*okp = false;
		return fy_null;
	}

	if (open) {
		if (!allow_categories) {
			fymm_diagf(p, true, p->lex.line, 1,
				   "the y axis takes a range, not a list");
			*okp = false;
			return fy_null;
		}
		if (!xy_list(p, gb, open + 1, close, false, &categories,
			     "the category list")) {
			*okp = false;
			return fy_null;
		}
		ne = open;
	} else {
		arrow = NULL;
		for (q = s; q + 3 <= e; q++) {
			if (!memcmp(q, "-->", 3)) {
				arrow = q;
				break;
			}
		}
		if (arrow) {
			/* the range runs back from the arrow to the last
			 * space before it, leaving the name in front */
			const char *lo_start = arrow;

			while (lo_start > s && lo_start[-1] == ' ')
				lo_start--;
			while (lo_start > s && lo_start[-1] != ' ')
				lo_start--;
			if (!xy_number(lo_start, arrow, &lo) ||
			    !xy_number(arrow + 3, e, &hi)) {
				fymm_diagf(p, true, p->lex.line, 1,
					   "the axis range is not a pair of numbers");
				*okp = false;
				return fy_null;
			}
			have_range = true;
			ne = lo_start;
		}
	}

	xy_trim(&ns, &ne);
	if (ns < ne)
		name = fy_value(gb, fy_gb_intern_string_size(gb, ns,
					(size_t)(ne - ns)));

	return fy_mapping(gb,
		"name", name,
		"categories", categories,
		"min", have_range ? fy_value(gb, lo) : fy_null,
		"max", have_range ? fy_value(gb, hi) : fy_null);
}

/* `line [name] [data]` and `bar [name] [data]`; the data is required. */
static bool xy_series(struct fymm_parser *p, struct fy_generic_builder *gb,
		      const char *s, const char *e, const char *kind,
		      fy_generic *seriesp)
{
	const char *open, *close, *ns = s, *ne = e;
	fy_generic data, name = fy_null;

	if (!xy_brackets(s, e, &open, &close)) {
		fymm_diagf(p, true, p->lex.line, 1,
			   "the %s data has an unbalanced bracket", kind);
		return false;
	}
	if (!open) {
		fymm_diagf(p, true, p->lex.line, 1, "the %s has no data", kind);
		return false;
	}
	if (!xy_list(p, gb, open + 1, close, true, &data, "the data"))
		return false;

	ne = open;
	xy_trim(&ns, &ne);
	if (ns < ne)
		name = fy_value(gb, fy_gb_intern_string_size(gb, ns,
					(size_t)(ne - ns)));

	*seriesp = fy_append(gb, *seriesp,
		fy_mapping(gb, "kind", kind, "name", name, "data", data));
	return true;
}

int fymm_parse_xychart(struct fymm_parser *p, fy_generic config,
		       fy_generic title, struct fymm_token *htoks, int hn)
{
	struct fy_generic_builder *gb = p->d->gb;
	struct fymm_token toks[FYMM_MAX_TOKENS];
	struct fymm_acc acc = { fy_null, fy_null };
	fy_generic series = fy_seq_empty;
	fy_generic xaxis = fy_null, yaxis = fy_null;
	const char *line, *e, *rest;
	bool horizontal = false, ok;
	size_t len;
	int i, n;

	for (i = 1; i < hn; i++) {
		if (htoks[i].type == FYMM_TOK_COLON)
			continue;
		if (fymm_token_ieq(&htoks[i], "horizontal"))
			horizontal = true;
		else
			fymm_diagf(p, false, htoks[i].line, htoks[i].col,
				   "ignoring unknown xychart option '%.*s'",
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
			const char *ts = rest, *te = e;

			xy_trim(&ts, &te);
			if (fy_is_invalid(title))
				title = fy_value(gb,
					fy_gb_intern_string_size(gb, ts,
						(size_t)(te - ts)));
			continue;
		}
		if (fymm_line_keyword(line, len, "x-axis", &rest)) {
			xaxis = xy_axis(p, gb, rest, e, true, &ok);
			continue;
		}
		if (fymm_line_keyword(line, len, "y-axis", &rest)) {
			yaxis = xy_axis(p, gb, rest, e, false, &ok);
			continue;
		}
		if (fymm_line_keyword(line, len, "line", &rest)) {
			xy_series(p, gb, rest, e, "line", &series);
			continue;
		}
		if (fymm_line_keyword(line, len, "bar", &rest)) {
			xy_series(p, gb, rest, e, "bar", &series);
			continue;
		}

		fymm_diagf(p, true, p->lex.line, 1,
			   "unknown xychart statement '%.*s'", (int)len, line);
	}

	if (fy_is_invalid(title))
		title = acc.title;

	p->d->model = fy_mapping(gb,
		"type", "xychart",
		"orientation", horizontal ? "horizontal" : "vertical",
		"title", title,
		"accTitle", acc.title,
		"accDescr", acc.descr,
		"config", config,
		"xAxis", xaxis,
		"yAxis", yaxis,
		"series", series);
	return 0;
}
