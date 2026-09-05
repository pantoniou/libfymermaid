/*
 * fymm-mindmap.c - the mindmap statement parser and model builder
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
 * The node shapes, longest delimiter first: `))text((` must be recognised
 * before `)text(`, and `((text))` before `(text)`.
 */
static const struct {
	const char *open;
	const char *close;
	const char *shape;
} mm_shapes[] = {
	{ "))",	"((",	"bang" },
	{ "((",	"))",	"circle" },
	{ "{{",	"}}",	"hexagon" },
	{ "[",	"]",	"square" },
	{ "(",	")",	"rounded" },
	{ ")",	"(",	"cloud" },
};

#define MM_SHAPE_COUNT (sizeof(mm_shapes) / sizeof(mm_shapes[0]))

/* struct mm_node - one node while the tree is being read */
struct mm_node {
	const char *id;
	const char *text;
	const char *shape;
	const char *icon;
	const char *cls;
	int indent;
	int parent;
};

/* The indent of a line, in columns; a tab advances to the next multiple of
 * eight, as a terminal would render it. */
static int mm_indent(const char *s, const char *e)
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
 * Split a node line into its id, its text and its shape. The closing
 * delimiter is looked for at the end of the line, not at the first match, so
 * that a label may contain brackets of its own: `root["String containing []"]`.
 */
static void mm_parse_node(struct fy_generic_builder *gb, const char *s,
			  const char *e, struct mm_node *node)
{
	const char *open, *close, *ts, *te;
	size_t i, ol, cl;

	node->shape = "default";
	node->id = fy_gb_intern_string_size(gb, s, (size_t)(e - s));
	node->text = node->id;

	for (open = s; open < e; open++) {
		for (i = 0; i < MM_SHAPE_COUNT; i++) {
			ol = strlen(mm_shapes[i].open);
			cl = strlen(mm_shapes[i].close);
			if ((size_t)(e - open) < ol + cl ||
			    memcmp(open, mm_shapes[i].open, ol))
				continue;
			if (memcmp(e - cl, mm_shapes[i].close, cl))
				continue;

			close = e - cl;
			ts = open + ol;
			te = close;
			/* a quoted label keeps whatever is inside it */
			if (te - ts >= 2 && *ts == '"' && te[-1] == '"') {
				ts++;
				te--;
			}
			node->shape = mm_shapes[i].shape;
			node->id = fy_gb_intern_string_size(gb, s,
							    (size_t)(open - s));
			node->text = fy_gb_intern_string_size(gb, ts,
							      (size_t)(te - ts));
			if (!*node->id)
				node->id = node->text;
			return;
		}
	}
}

/* Build the generic subtree rooted at @idx. */
static fy_generic mm_build(struct fy_generic_builder *gb,
			   const struct mm_node *nodes, size_t count,
			   size_t idx)
{
	fy_generic children = fy_seq_empty;
	size_t i;

	for (i = idx + 1; i < count; i++) {
		if (nodes[i].parent == (int)idx)
			children = fy_append(gb, children,
					     mm_build(gb, nodes, count, i));
	}
	return fy_mapping(gb,
		"id", nodes[idx].id,
		"text", nodes[idx].text,
		"shape", nodes[idx].shape,
		"icon", nodes[idx].icon ? fy_value(gb, nodes[idx].icon) : fy_null,
		"class", nodes[idx].cls ? fy_value(gb, nodes[idx].cls) : fy_null,
		"children", children);
}

int fymm_parse_mindmap(struct fymm_parser *p, fy_generic config,
		       fy_generic title, struct fymm_token *htoks, int hn)
{
	struct fy_generic_builder *gb = p->d->gb;
	struct fymm_token toks[FYMM_MAX_TOKENS];
	struct fymm_acc acc = { fy_null, fy_null };
	struct mm_node *nodes = NULL, *node;
	size_t count = 0, alloc = 0, i;
	const char *s, *e, *raw;
	fy_generic root = fy_null;
	int indent, root_indent = -1;
	int n, top;

	for (i = 1; i < (size_t)hn; i++) {
		if (htoks[i].type == FYMM_TOK_COLON)
			continue;
		fymm_diagf(p, false, htoks[i].line, htoks[i].col,
			   "ignoring unknown mindmap option '%.*s'",
			   (int)htoks[i].len, htoks[i].text);
	}

	while (fymm_lex_next_line(&p->lex)) {
		size_t len;

		raw = p->lex.ls;
		s = fymm_lex_line(&p->lex, &len);
		if (!len)
			continue;
		e = s + len;
		indent = mm_indent(raw, s);

		n = fymm_line_tokens(&p->lex, toks, FYMM_MAX_TOKENS);
		if (fymm_stmt_acc(p, toks, n, &acc)) {
			fymm_tokens_reset(toks, n);
			continue;
		}
		fymm_tokens_reset(toks, n);

		/* the decorators attach to the node above them */
		if (len > 3 && !memcmp(s, ":::", 3)) {
			if (count)
				nodes[count - 1].cls =
					fy_gb_intern_string_size(gb, s + 3,
								 len - 3);
			continue;
		}
		if (len > 7 && !memcmp(s, "::icon(", 7) && e[-1] == ')') {
			if (count)
				nodes[count - 1].icon =
					fy_gb_intern_string_size(gb, s + 7,
								 len - 8);
			continue;
		}

		/* a mindmap has exactly one root; anything at or above its
		 * indent is a second one */
		if (root_indent >= 0 && indent <= root_indent) {
			fymm_diagf(p, true, p->lex.line, indent + 1,
				   "a mindmap has one root, and '%.*s' is a second",
				   (int)len, s);
			continue;
		}

		if (count == alloc) {
			struct mm_node *nn;

			alloc = alloc ? alloc * 2 : 16;
			nn = realloc(nodes, alloc * sizeof(*nodes));
			if (!nn)
				goto out;
			nodes = nn;
		}
		node = &nodes[count];
		memset(node, 0, sizeof(*node));
		node->indent = indent;
		mm_parse_node(gb, s, e, node);

		/* the closest node above with a smaller indent is the parent */
		node->parent = -1;
		for (top = (int)count - 1; top >= 0; top--) {
			if (nodes[top].indent < indent) {
				node->parent = top;
				break;
			}
		}
		if (node->parent < 0)
			root_indent = indent;
		count++;
	}

	if (count)
		root = mm_build(gb, nodes, count, 0);

out:
	free(nodes);
	if (fy_is_invalid(title))
		title = acc.title;

	p->d->model = fy_mapping(gb,
		"type", "mindmap",
		"title", title,
		"accTitle", acc.title,
		"accDescr", acc.descr,
		"config", config,
		"root", root);
	return 0;
}
