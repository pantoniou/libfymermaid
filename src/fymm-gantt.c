/*
 * fymm-gantt.c - the gantt chart statement parser and model builder
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

/* the task states mermaid recognises before the identifier */
static const char *const gt_tags[] = {
	"active", "done", "crit", "milestone",
};

/*
 * Days since 1970-01-01 for a proleptic Gregorian date, by Howard Hinnant's
 * civil calendar algorithm. A gantt only ever needs whole days, and this
 * needs no timezone database and no libc call that depends on one.
 */
static long gt_days_from_civil(long y, unsigned m, unsigned d)
{
	long era, yoe, doy, doe;

	y -= m <= 2;
	era = (y >= 0 ? y : y - 399) / 400;
	yoe = y - era * 400;
	doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
	doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097 + doe - 719468;
}

/* Read an ISO `YYYY-MM-DD` date. Returns false for anything else. */
static bool gt_date(const char *s, const char *e, long *dayp)
{
	unsigned m, d;
	long y;
	int n = 0;

	while (s < e && (*s == ' ' || *s == '\t'))
		s++;
	while (e > s && (e[-1] == ' ' || e[-1] == '\t'))
		e--;
	if (e - s != 10)
		return false;
	if (sscanf(s, "%4ld-%2u-%2u%n", &y, &m, &d, &n) != 3 || n != 10)
		return false;
	if (m < 1 || m > 12 || d < 1 || d > 31)
		return false;
	*dayp = gt_days_from_civil(y, m, d);
	return true;
}

/* Read a duration such as `3d`, `2w` or `36h`, in days. */
static bool gt_duration(const char *s, const char *e, double *daysp)
{
	char buf[32], *end;
	double v;

	while (s < e && (*s == ' ' || *s == '\t'))
		s++;
	while (e > s && (e[-1] == ' ' || e[-1] == '\t'))
		e--;
	if (s >= e || (size_t)(e - s) >= sizeof(buf))
		return false;
	snprintf(buf, sizeof(buf), "%.*s", (int)(e - s), s);
	end = NULL;
	v = strtod(buf, &end);
	if (!end || end == buf)
		return false;

	if (!strcmp(end, "d"))
		*daysp = v;
	else if (!strcmp(end, "w"))
		*daysp = v * 7.0;
	else if (!strcmp(end, "h"))
		*daysp = v / 24.0;
	else if (!strcmp(end, "m"))
		*daysp = v / 1440.0;
	else if (!strcmp(end, "s"))
		*daysp = v / 86400.0;
	else if (!strcmp(end, "ms"))
		*daysp = v / 86400000.0;
	else
		return false;
	return true;
}

/* Split @s on commas into at most @max trimmed fields. */
static int gt_fields(const char *s, const char *e, const char **fs,
		     const char **fe, int max)
{
	const char *comma;
	int n = 0;

	while (n < max) {
		comma = memchr(s, ',', (size_t)(e - s));
		fs[n] = s;
		fe[n] = comma ? comma : e;
		while (fs[n] < fe[n] && (*fs[n] == ' ' || *fs[n] == '\t'))
			fs[n]++;
		while (fe[n] > fs[n] && (fe[n][-1] == ' ' || fe[n][-1] == '\t'))
			fe[n]--;
		n++;
		if (!comma)
			break;
		s = comma + 1;
	}
	return n;
}

static bool gt_is_tag(const char *s, const char *e)
{
	size_t i, len = (size_t)(e - s);

	for (i = 0; i < sizeof(gt_tags) / sizeof(gt_tags[0]); i++) {
		if (strlen(gt_tags[i]) == len && !strncasecmp(s, gt_tags[i], len))
			return true;
	}
	return false;
}

/*
 * Read one endpoint of a task: an ISO date, `after <id>`, `until <id>`, or a
 * duration. The kind is recorded so that the resolver can order the tasks
 * that depend on one another.
 */
static fy_generic gt_endpoint(struct fy_generic_builder *gb, const char *s,
			      const char *e)
{
	const char *rest;
	double days;
	long day;

	if (fymm_line_keyword(s, (size_t)(e - s), "after", &rest))
		return fy_mapping(gb, "kind", "after",
				  "ref", fymm_trim_text(gb, rest, e));
	if (fymm_line_keyword(s, (size_t)(e - s), "until", &rest))
		return fy_mapping(gb, "kind", "until",
				  "ref", fymm_trim_text(gb, rest, e));
	if (gt_date(s, e, &day))
		return fy_mapping(gb, "kind", "date", "day", (long long)day);
	if (gt_duration(s, e, &days))
		return fy_mapping(gb, "kind", "duration", "days", days);
	return fy_mapping(gb, "kind", "unknown",
			  "text", fymm_trim_text(gb, s, e));
}

int fymm_parse_gantt(struct fymm_parser *p, fy_generic config, fy_generic title,
		     struct fymm_token *htoks, int hn)
{
	struct fy_generic_builder *gb = p->d->gb;
	struct fymm_token toks[FYMM_MAX_TOKENS];
	struct fymm_acc acc = { fy_null, fy_null };
	const char *fs[8], *fe[8];
	fy_generic sections = fy_seq_empty, tasks = fy_seq_empty;
	fy_generic section_name = fy_null, tags;
	const char *line, *e, *rest, *colon;
	bool open_section = false;
	size_t len;
	int i, n, nf, first;

	for (i = 1; i < hn; i++) {
		if (htoks[i].type == FYMM_TOK_COLON)
			continue;
		fymm_diagf(p, false, htoks[i].line, htoks[i].col,
			   "ignoring unknown gantt option '%.*s'",
			   (int)htoks[i].len, htoks[i].text);
	}

#define GT_CLOSE_SECTION() \
	do { \
		if (open_section || fy_len(tasks)) { \
			sections = fy_append(gb, sections, \
				fy_mapping(gb, "name", section_name, \
					   "tasks", tasks)); \
			tasks = fy_seq_empty; \
		} \
	} while (0)

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
		if (fymm_line_keyword(line, len, "section", &rest)) {
			GT_CLOSE_SECTION();
			section_name = fymm_trim_text(gb, rest, e);
			open_section = true;
			continue;
		}
		/* the settings, kept in the config for a consumer to read */
		if (fymm_line_keyword(line, len, "dateFormat", &rest)) {
			config = fy_assoc(gb, config, "dateFormat",
					  fymm_trim_text(gb, rest, e));
			continue;
		}
		if (fymm_line_keyword(line, len, "axisFormat", &rest) ||
		    fymm_line_keyword(line, len, "tickInterval", &rest) ||
		    fymm_line_keyword(line, len, "excludes", &rest) ||
		    fymm_line_keyword(line, len, "includes", &rest) ||
		    fymm_line_keyword(line, len, "todayMarker", &rest) ||
		    fymm_line_keyword(line, len, "weekday", &rest) ||
		    fymm_line_keyword(line, len, "click", &rest))
			continue;
		if (len == strlen("inclusiveEndDates") &&
		    !strncasecmp(line, "inclusiveEndDates", len)) {
			config = fy_assoc(gb, config, "inclusiveEndDates", true);
			continue;
		}

		/* everything else is a task: `Name : [tags,] [id,] a, b` */
		colon = fymm_split_colon(line, e);
		if (!colon) {
			fymm_diagf(p, true, p->lex.line, 1,
				   "unknown gantt statement '%.*s'",
				   (int)len, line);
			continue;
		}

		nf = gt_fields(colon + 1, e, fs, fe, 8);
		tags = fy_seq_empty;
		for (first = 0; first < nf && gt_is_tag(fs[first], fe[first]);
		     first++)
			tags = fy_append(gb, tags,
					 fymm_trim_text(gb, fs[first],
							fe[first]));

		tasks = fy_append(gb, tasks,
			fy_mapping(gb,
				"name", fymm_trim_text(gb, line, colon),
				"tags", tags,
				/* with three fields left the first is the id;
				 * with two the task is unnamed */
				"id", nf - first >= 3 ?
					fymm_trim_text(gb, fs[first],
						       fe[first]) : fy_null,
				"start", nf - first >= 2 ?
					gt_endpoint(gb, fs[nf - 2], fe[nf - 2]) :
					fy_null,
				"end", nf - first >= 1 ?
					gt_endpoint(gb, fs[nf - 1], fe[nf - 1]) :
					fy_null,
				"section", section_name));
	}

	GT_CLOSE_SECTION();
#undef GT_CLOSE_SECTION

	if (fy_is_invalid(title))
		title = acc.title;

	p->d->model = fy_mapping(gb,
		"type", "gantt",
		"title", title,
		"accTitle", acc.title,
		"accDescr", acc.descr,
		"config", config,
		"sections", sections);
	return 0;
}
