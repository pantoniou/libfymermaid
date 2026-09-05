/*
 * fymm-timeline.c - the timeline statement parser and model builder
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
 * A timeline task line is free text split on colons: `task: event: event`.
 * The text may hold a markdown link, whose URL carries a colon of its own, so
 * the split ignores a colon inside brackets or parentheses.
 */
static const char *tl_split(const char *s, const char *e)
{
	int depth = 0;

	for (; s < e; s++) {
		if (*s == '[' || *s == '(')
			depth++;
		else if (*s == ']' || *s == ')')
			depth -= depth > 0;
		else if (*s == ':' && !depth)
			return s;
	}
	return NULL;
}

static fy_generic tl_text(struct fy_generic_builder *gb, const char *s,
			  const char *e)
{
	while (s < e && (*s == ' ' || *s == '\t'))
		s++;
	while (e > s && (e[-1] == ' ' || e[-1] == '\t'))
		e--;
	return fy_value(gb, fy_gb_intern_string_size(gb, s, (size_t)(e - s)));
}

/* Does @line start with @word followed by a space? */
static bool tl_keyword(const char *s, size_t len, const char *word,
		       const char **restp)
{
	size_t wl = strlen(word);

	if (len <= wl || strncasecmp(s, word, wl))
		return false;
	if (s[wl] != ' ' && s[wl] != '\t')
		return false;
	*restp = s + wl + 1;
	return true;
}

int fymm_parse_timeline(struct fymm_parser *p, fy_generic config,
			fy_generic title, struct fymm_token *htoks, int hn)
{
	struct fy_generic_builder *gb = p->d->gb;
	struct fymm_token toks[FYMM_MAX_TOKENS];
	struct fymm_acc acc = { fy_null, fy_null };
	fy_generic sections = fy_seq_empty;
	fy_generic tasks = fy_seq_empty;
	fy_generic events = fy_seq_empty;
	fy_generic section_name = fy_null;
	fy_generic task_name = fy_invalid;
	const char *orientation = "LR";
	const char *s, *e, *rest, *colon;
	size_t len;
	bool open_section = false;
	int i, n;

	/* the header carries the direction */
	for (i = 1; i < hn; i++) {
		if (htoks[i].type == FYMM_TOK_COLON)
			continue;
		if (fymm_token_ieq(&htoks[i], "LR")) {
			orientation = "LR";
		} else if (fymm_token_ieq(&htoks[i], "TD") ||
			   fymm_token_ieq(&htoks[i], "TB")) {
			orientation = "TD";
		} else {
			fymm_diagf(p, false, htoks[i].line, htoks[i].col,
				   "ignoring unknown timeline option '%.*s'",
				   (int)htoks[i].len, htoks[i].text);
		}
	}

/* Close the task being read, then the section, appending each to its parent. */
#define TL_CLOSE_TASK() \
	do { \
		if (fy_is_valid(task_name)) { \
			tasks = fy_append(gb, tasks, \
				fy_mapping(gb, "name", task_name, \
					   "events", events)); \
			task_name = fy_invalid; \
			events = fy_seq_empty; \
		} \
	} while (0)

#define TL_CLOSE_SECTION() \
	do { \
		TL_CLOSE_TASK(); \
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

		/* the statements that are read as tokens */
		n = fymm_line_tokens(&p->lex, toks, FYMM_MAX_TOKENS);
		if (fymm_stmt_acc(p, toks, n, &acc)) {
			fymm_tokens_reset(toks, n);
			continue;
		}
		fymm_tokens_reset(toks, n);

		if (tl_keyword(s, len, "title", &rest)) {
			TL_CLOSE_TASK();
			title = fy_is_valid(title) ? title : tl_text(gb, rest, e);
			continue;
		}
		if (tl_keyword(s, len, "section", &rest)) {
			TL_CLOSE_SECTION();
			section_name = tl_text(gb, rest, e);
			open_section = true;
			continue;
		}

		/* a line that opens with a colon continues the last task */
		if (*s == ':') {
			if (fy_is_invalid(task_name)) {
				fymm_diagf(p, true, p->lex.line, 1,
					   "an event line has no task to belong to");
				continue;
			}
			s++;
		} else {
			TL_CLOSE_TASK();
			colon = tl_split(s, e);
			task_name = tl_text(gb, s, colon ? colon : e);
			if (!colon)
				continue;
			s = colon + 1;
		}

		/* the rest of the line is one or more colon separated events */
		for (;;) {
			colon = tl_split(s, e);
			events = fy_append(gb, events,
					   tl_text(gb, s, colon ? colon : e));
			if (!colon)
				break;
			s = colon + 1;
		}
	}

	TL_CLOSE_SECTION();
#undef TL_CLOSE_TASK
#undef TL_CLOSE_SECTION

	if (fy_is_invalid(title))
		title = acc.title;

	p->d->model = fy_mapping(gb,
		"type", "timeline",
		"orientation", orientation,
		"title", title,
		"accTitle", acc.title,
		"accDescr", acc.descr,
		"config", config,
		"sections", sections);
	return 0;
}
