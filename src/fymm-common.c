/*
 * fymm-common.c - statement handling shared by every diagram type
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

int fymm_line_tokens(struct fymm_lex *l, struct fymm_token *toks, int max)
{
	int n = 0;

	while (n < max) {
		memset(&toks[n], 0, sizeof(toks[n]));
		if (!fymm_lex_token(l, &toks[n]))
			break;
		n++;
	}
	return n;
}

void fymm_tokens_reset(struct fymm_token *toks, int n)
{
	int i;

	for (i = 0; i < n; i++)
		fymm_token_reset(&toks[i]);
}

const char *fymm_rest_text(struct fymm_parser *p, int from_col)
{
	const char *s;
	size_t len;

	s = fymm_lex_rest(&p->lex, from_col, &len);
	return fy_gb_intern_string_size(p->d->gb, s, len);
}

/*
 * `accTitle: text` and `accDescr: text` take the rest of the line.
 * `accDescr { ... }` takes each line up to a closing brace. The text is free
 * form, so it is read from the line and not through the tokenizer.
 */
bool fymm_stmt_acc(struct fymm_parser *p, struct fymm_token *toks, int n,
		   struct fymm_acc *acc)
{
	struct fy_generic_builder *gb = p->d->gb;
	fy_generic *slot;
	const char *s;
	char *text, *nt;
	size_t len, pos;
	bool title;

	if (!n || toks[0].type != FYMM_TOK_WORD)
		return false;
	if (fymm_token_ieq(&toks[0], "accTitle"))
		title = true;
	else if (fymm_token_ieq(&toks[0], "accDescr"))
		title = false;
	else
		return false;

	slot = title ? &acc->title : &acc->descr;

	if (n >= 2 && toks[1].type == FYMM_TOK_COLON) {
		*slot = fy_value(gb, fymm_rest_text(p, toks[1].col + 1));
		return true;
	}

	if (title || n < 2 || !fymm_token_is(&toks[1], "{")) {
		fymm_diagf(p, true, toks[0].line, toks[0].col,
			   "expected ':' or '{' after '%.*s'",
			   (int)toks[0].len, toks[0].text);
		return true;
	}

	text = NULL;
	pos = 0;
	while (fymm_lex_next_line(&p->lex)) {
		s = fymm_lex_line(&p->lex, &len);
		if (len == 1 && *s == '}')
			break;
		nt = realloc(text, pos + len + 2);
		if (!nt) {
			free(text);
			return true;
		}
		text = nt;
		if (pos)
			text[pos++] = '\n';
		memcpy(text + pos, s, len);
		pos += len;
		text[pos] = '\0';
	}
	if (text) {
		*slot = fy_value(gb, fy_gb_intern_string(gb, text));
		free(text);
	}
	return true;
}

/*
 * Find the colon that splits a free text line, or NULL. A colon inside
 * brackets or parentheses belongs to the text: a markdown link carries one in
 * its URL, and a timeline event may hold a link.
 */
const char *fymm_split_colon(const char *s, const char *e)
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

/* Intern [s, e) with the surrounding whitespace removed. */
fy_generic fymm_trim_text(struct fy_generic_builder *gb, const char *s,
			  const char *e)
{
	while (s < e && (*s == ' ' || *s == '\t'))
		s++;
	while (e > s && (e[-1] == ' ' || e[-1] == '\t'))
		e--;
	return fy_value(gb, fy_gb_intern_string_size(gb, s, (size_t)(e - s)));
}

/* Does the line open with @word and a space? Sets @restp past it. */
bool fymm_line_keyword(const char *s, size_t len, const char *word,
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

/* The diagram types this library knows how to read. */
static const struct fymm_diagram_ops fymm_ops[] = {
	{
		.keyword = "gitGraph",
		.config_key = "gitGraph",
		.type = FYMM_DT_GITGRAPH,
		.parse = fymm_parse_gitgraph,
		.render = fymm_render_gitgraph,
	}, {
		.keyword = "erDiagram",
		.config_key = "er",
		.type = FYMM_DT_ER,
		.parse = fymm_parse_er,
		.render = fymm_render_er,
	}, {
		.keyword = "stateDiagram",
		.config_key = "state",
		.type = FYMM_DT_STATE,
		.parse = fymm_parse_state,
		.render = fymm_render_state,
	}, {
		.keyword = "stateDiagram-v2",
		.config_key = "state",
		.type = FYMM_DT_STATE,
		.parse = fymm_parse_state,
		.render = fymm_render_state,
	}, {
		.keyword = "gantt",
		.config_key = "gantt",
		.type = FYMM_DT_GANTT,
		.parse = fymm_parse_gantt,
		.render = fymm_render_gantt,
	}, {
		.keyword = "radar-beta",
		.config_key = "radar",
		.type = FYMM_DT_RADAR,
		.parse = fymm_parse_radar,
		.render = fymm_render_radar,
	}, {
		.keyword = "xychart",
		.config_key = "xyChart",
		.type = FYMM_DT_XYCHART,
		.parse = fymm_parse_xychart,
		.render = fymm_render_xychart,
	}, {
		.keyword = "xychart-beta",
		.config_key = "xyChart",
		.type = FYMM_DT_XYCHART,
		.parse = fymm_parse_xychart,
		.render = fymm_render_xychart,
	}, {
		.keyword = "quadrantChart",
		.config_key = "quadrantChart",
		.type = FYMM_DT_QUADRANT,
		.parse = fymm_parse_quadrant,
		.render = fymm_render_quadrant,
	}, {
		.keyword = "classDiagram",
		.config_key = "class",
		.type = FYMM_DT_CLASS,
		.parse = fymm_parse_class,
		.render = fymm_render_class,
	}, {
		.keyword = "classDiagram-v2",
		.config_key = "class",
		.type = FYMM_DT_CLASS,
		.parse = fymm_parse_class,
		.render = fymm_render_class,
	}, {
		.keyword = "flowchart",
		.config_key = "flowchart",
		.semicolons = true,
		.type = FYMM_DT_FLOWCHART,
		.parse = fymm_parse_flowchart,
		.render = fymm_render_flowchart,
	}, {
		.keyword = "swimlane-beta",
		.config_key = "flowchart",
		.semicolons = true,
		.type = FYMM_DT_FLOWCHART,
		.parse = fymm_parse_flowchart,
		.render = fymm_render_flowchart,
	}, {
		.keyword = "graph",
		.config_key = "flowchart",
		.semicolons = true,
		.type = FYMM_DT_FLOWCHART,
		.parse = fymm_parse_flowchart,
		.render = fymm_render_flowchart,
	}, {
		.keyword = "sequenceDiagram",
		.config_key = "sequence",
		.semicolons = true,
		.hash_comment = true,
		.type = FYMM_DT_SEQUENCE,
		.parse = fymm_parse_sequence,
		.render = fymm_render_sequence,
	}, {
		.keyword = "mindmap",
		.config_key = "mindmap",
		.type = FYMM_DT_MINDMAP,
		.parse = fymm_parse_mindmap,
		.render = fymm_render_mindmap,
	}, {
		.keyword = "journey",
		.config_key = "journey",
		.type = FYMM_DT_JOURNEY,
		.parse = fymm_parse_journey,
		.render = fymm_render_journey,
	}, {
		.keyword = "timeline",
		.config_key = "timeline",
		.type = FYMM_DT_TIMELINE,
		.parse = fymm_parse_timeline,
		.render = fymm_render_timeline,
	}, {
		.keyword = "pie",
		.config_key = "pie",
		.type = FYMM_DT_PIE,
		.parse = fymm_parse_pie,
		.render = fymm_render_pie,
	},
};

#define FYMM_OPS_COUNT (sizeof(fymm_ops) / sizeof(fymm_ops[0]))

const struct fymm_diagram_ops *fymm_diagram_ops_by_keyword(const char *word,
							   size_t len)
{
	struct fymm_token t;
	size_t i;

	memset(&t, 0, sizeof(t));
	t.text = word;
	t.len = len;
	for (i = 0; i < FYMM_OPS_COUNT; i++) {
		if (fymm_token_ieq(&t, fymm_ops[i].keyword))
			return &fymm_ops[i];
	}
	return NULL;
}

const struct fymm_diagram_ops *fymm_diagram_ops_by_type(enum fymm_diagram_type t)
{
	size_t i;

	for (i = 0; i < FYMM_OPS_COUNT; i++) {
		if (fymm_ops[i].type == t)
			return &fymm_ops[i];
	}
	return NULL;
}
