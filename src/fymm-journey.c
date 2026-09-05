/*
 * fymm-journey.c - the user journey statement parser and model builder
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
 * A task is `name: score: actor, actor`. The score is how the actors felt,
 * from one to five. Both the score and the actors may be absent.
 */
static fy_generic journey_actors(struct fy_generic_builder *gb, const char *s,
				 const char *e)
{
	fy_generic actors = fy_seq_empty;
	const char *comma;

	for (;;) {
		comma = memchr(s, ',', (size_t)(e - s));
		if (comma > s || (!comma && e > s))
			actors = fy_append(gb, actors,
				fymm_trim_text(gb, s, comma ? comma : e));
		if (!comma)
			break;
		s = comma + 1;
	}
	return actors;
}

int fymm_parse_journey(struct fymm_parser *p, fy_generic config,
		       fy_generic title, struct fymm_token *htoks, int hn)
{
	struct fy_generic_builder *gb = p->d->gb;
	struct fymm_token toks[FYMM_MAX_TOKENS];
	struct fymm_acc acc = { fy_null, fy_null };
	fy_generic sections = fy_seq_empty;
	fy_generic tasks = fy_seq_empty;
	fy_generic section_name = fy_null;
	fy_generic score, actors;
	const char *s, *e, *rest, *colon;
	char buf[32], *end;
	size_t len;
	bool open_section = false;
	double v;
	int i, n;

	for (i = 1; i < hn; i++) {
		if (htoks[i].type == FYMM_TOK_COLON)
			continue;
		fymm_diagf(p, false, htoks[i].line, htoks[i].col,
			   "ignoring unknown journey option '%.*s'",
			   (int)htoks[i].len, htoks[i].text);
	}

#define J_CLOSE_SECTION() \
	do { \
		if (open_section || fy_len(tasks)) { \
			sections = fy_append(gb, sections, \
				fy_mapping(gb, "name", section_name, \
					   "tasks", tasks)); \
			tasks = fy_seq_empty; \
		} \
	} while (0)

	while (fymm_lex_next_line(&p->lex)) {
		s = fymm_lex_line(&p->lex, &len);
		if (!len)
			continue;
		e = s + len;

		n = fymm_line_tokens(&p->lex, toks, FYMM_MAX_TOKENS);
		if (fymm_stmt_acc(p, toks, n, &acc)) {
			fymm_tokens_reset(toks, n);
			continue;
		}
		fymm_tokens_reset(toks, n);

		if (fymm_line_keyword(s, len, "title", &rest)) {
			if (fy_is_invalid(title))
				title = fymm_trim_text(gb, rest, e);
			continue;
		}
		if (fymm_line_keyword(s, len, "section", &rest)) {
			J_CLOSE_SECTION();
			section_name = fymm_trim_text(gb, rest, e);
			open_section = true;
			continue;
		}

		/* everything else is a task */
		score = fy_null;
		actors = fy_seq_empty;
		colon = fymm_split_colon(s, e);
		if (colon) {
			const char *after = colon + 1;
			const char *colon2 = fymm_split_colon(after, e);
			const char *score_end = colon2 ? colon2 : e;

			snprintf(buf, sizeof(buf), "%.*s",
				 (int)(score_end - after), after);
			end = NULL;
			v = strtod(buf, &end);
			while (end && (*end == ' ' || *end == '\t'))
				end++;
			if (end && end != buf && !*end)
				score = fy_value(gb, v);
			else if (score_end > after)
				fymm_diagf(p, false, p->lex.line, 1,
					   "task score '%s' is not a number",
					   buf);
			if (colon2)
				actors = journey_actors(gb, colon2 + 1, e);
		}
		tasks = fy_append(gb, tasks,
			fy_mapping(gb,
				"name", fymm_trim_text(gb, s, colon ? colon : e),
				"score", score,
				"actors", actors));
	}

	J_CLOSE_SECTION();
#undef J_CLOSE_SECTION

	if (fy_is_invalid(title))
		title = acc.title;

	p->d->model = fy_mapping(gb,
		"type", "journey",
		"title", title,
		"accTitle", acc.title,
		"accDescr", acc.descr,
		"config", config,
		"sections", sections);
	return 0;
}
