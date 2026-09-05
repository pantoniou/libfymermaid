/*
 * fymm-kanban.c - the kanban statement parser and model builder
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

/* The shapes a kanban node may be written with, longest delimiter first. */
static const struct {
	const char *open;
	const char *close;
} kb_shapes[] = {
	{ "[[", "]]" }, { "((", "))" }, { "{{", "}}" },
	{ "[", "]" }, { "(", ")" }, { "{", "}" },
};

#define KB_SHAPE_COUNT (sizeof(kb_shapes) / sizeof(kb_shapes[0]))

/* The indent of a line, in columns. */
static int kb_indent(const char *s, const char *e)
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
 * Split a node into its id and its label. As in a mindmap the closing
 * delimiter is looked for at the end of the line, so a label may carry
 * brackets of its own.
 */
static void kb_node(struct fy_generic_builder *gb, const char *s, const char *e,
		    fy_generic *idp, fy_generic *textp)
{
	const char *open, *ts, *te;
	size_t i, ol, cl;

	*idp = fymm_trim_text(gb, s, e);
	*textp = *idp;

	for (open = s; open < e; open++) {
		for (i = 0; i < KB_SHAPE_COUNT; i++) {
			ol = strlen(kb_shapes[i].open);
			cl = strlen(kb_shapes[i].close);
			if ((size_t)(e - open) < ol + cl ||
			    memcmp(open, kb_shapes[i].open, ol) ||
			    memcmp(e - cl, kb_shapes[i].close, cl))
				continue;
			ts = open + ol;
			te = e - cl;
			if (te - ts >= 2 && *ts == '"' && te[-1] == '"') {
				ts++;
				te--;
			}
			*idp = fymm_trim_text(gb, s, open);
			*textp = fymm_trim_text(gb, ts, te);
			if (!*fy_str(*idp))
				*idp = *textp;
			return;
		}
	}
}

int fymm_parse_kanban(struct fymm_parser *p, fy_generic config,
		      fy_generic title, struct fymm_token *htoks, int hn)
{
	struct fy_generic_builder *gb = p->d->gb;
	struct fymm_token toks[FYMM_MAX_TOKENS];
	struct fymm_acc acc = { fy_null, fy_null };
	fy_generic sections = fy_seq_empty, items = fy_seq_empty;
	fy_generic section_id = fy_null, section_text = fy_null;
	fy_generic id, text, meta = fy_null;
	fy_generic_sized_string input;
	const char *line, *e, *raw, *brace;
	int indent, section_indent = -1;
	bool open_section = false;
	size_t len;
	int i, n;

	for (i = 1; i < hn; i++) {
		if (htoks[i].type == FYMM_TOK_COLON)
			continue;
		fymm_diagf(p, false, htoks[i].line, htoks[i].col,
			   "ignoring unknown kanban option '%.*s'",
			   (int)htoks[i].len, htoks[i].text);
	}

#define KB_CLOSE_SECTION() \
	do { \
		if (open_section) { \
			sections = fy_append(gb, sections, \
				fy_mapping(gb, "id", section_id, \
					   "text", section_text, \
					   "items", items)); \
			items = fy_seq_empty; \
		} \
	} while (0)

	while (fymm_lex_next_line(&p->lex)) {
		raw = p->lex.ls;
		line = fymm_lex_line(&p->lex, &len);
		if (!len)
			continue;
		e = line + len;
		indent = kb_indent(raw, line);

		n = fymm_line_tokens(&p->lex, toks, FYMM_MAX_TOKENS);
		if (fymm_stmt_acc(p, toks, n, &acc)) {
			fymm_tokens_reset(toks, n);
			continue;
		}
		fymm_tokens_reset(toks, n);

		/* the decorators attach to whatever came before them */
		if (len > 3 && !memcmp(line, ":::", 3))
			continue;

		/* `@{ ... }` carries a card's metadata */
		meta = fy_null;
		brace = memmem(line, len, "@{", 2);
		if (brace && e[-1] == '}') {
			input.data = brace + 1;
			input.size = (size_t)(e - (brace + 1));
			meta = fy_parse(gb, input,
					FYMM_YAML_PARSE_FLAGS |
					FYOPPF_INPUT_TYPE_STRING, NULL);
			if (!fy_is_mapping(meta)) {
				input.data = brace + 2;
				input.size = (size_t)(e - 1 - (brace + 2));
				meta = fy_parse(gb, input,
						FYMM_YAML_PARSE_FLAGS |
						FYOPPF_INPUT_TYPE_STRING, NULL);
			}
			e = brace;
		}

		kb_node(gb, line, e, &id, &text);

		/*
		 * The first item sets the indent a column sits at. Anything
		 * shallower than that would be a card with no column above
		 * it, which upstream refuses.
		 */
		if (section_indent >= 0 && indent < section_indent) {
			fymm_diagf(p, true, p->lex.line, 1,
				   "'%s' sits above the first column, so the cards under it have none",
				   fy_str(id));
			continue;
		}

		/* the shallowest level is a column; everything under it is a
		 * card in that column */
		if (section_indent < 0 || indent <= section_indent) {
			KB_CLOSE_SECTION();
			section_indent = indent;
			section_id = id;
			section_text = text;
			open_section = true;
			continue;
		}

		items = fy_append(gb, items,
			fy_mapping(gb, "id", id, "text", text,
				   "metadata", meta));
	}

	KB_CLOSE_SECTION();
#undef KB_CLOSE_SECTION

	if (fy_is_invalid(title))
		title = acc.title;

	p->d->model = fy_mapping(gb,
		"type", "kanban",
		"title", title,
		"accTitle", acc.title,
		"accDescr", acc.descr,
		"config", config,
		"sections", sections);
	return 0;
}
