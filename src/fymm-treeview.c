/*
 * fymm-treeview.c - the file tree statement parser and model builder
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

/* how deeply a tree may nest before the parents stop being tracked */
#define TV_MAX_DEPTH 64

static int tv_indent(const char *s, const char *e)
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
 * Indentation gives the tree, a trailing `/` marks a directory, and an entry
 * may carry a `:::class` and a `## comment` after its name.
 */
int fymm_parse_treeview(struct fymm_parser *p, fy_generic config,
			fy_generic title, struct fymm_token *htoks, int hn)
{
	struct fy_generic_builder *gb = p->d->gb;
	struct fymm_token toks[FYMM_MAX_TOKENS];
	struct fymm_acc acc = { fy_null, fy_null };
	fy_generic entries = fy_seq_empty;
	fy_generic comment, name;
	int indents[TV_MAX_DEPTH];
	const char *line, *raw, *e, *hash, *cls, *nend;
	int indent, depth = 0;
	size_t len;
	int i, n;

	for (i = 1; i < hn; i++) {
		if (htoks[i].type == FYMM_TOK_COLON)
			continue;
		fymm_diagf(p, false, htoks[i].line, htoks[i].col,
			   "ignoring unknown treeView option '%.*s'",
			   (int)htoks[i].len, htoks[i].text);
	}

	while (fymm_lex_next_line(&p->lex)) {
		raw = p->lex.ls;
		line = fymm_lex_line(&p->lex, &len);
		if (!len)
			continue;
		e = line + len;
		indent = tv_indent(raw, line);

		n = fymm_line_tokens(&p->lex, toks, FYMM_MAX_TOKENS);
		if (fymm_stmt_acc(p, toks, n, &acc)) {
			fymm_tokens_reset(toks, n);
			continue;
		}
		fymm_tokens_reset(toks, n);

		/* `## text` annotates the entry it sits on */
		comment = fy_null;
		hash = fymm_memmem(line, len, "##", 2);
		if (hash) {
			comment = fymm_trim_text(gb, hash + 2, e);
			e = hash;
		}
		/* `:::name` names a class, which a terminal has no use for */
		nend = e;
		for (cls = line; cls + 3 <= e; cls++) {
			if (!memcmp(cls, ":::", 3)) {
				nend = cls;
				break;
			}
		}
		name = fymm_trim_text(gb, line, nend);
		if (!*fy_str(name))
			continue;

		/* the depth is how many of the indents so far this one is
		 * past; a shallower line closes the ones it left */
		while (depth > 0 && indent <= indents[depth - 1])
			depth--;
		if (depth < TV_MAX_DEPTH)
			indents[depth] = indent;
		if (depth + 1 < TV_MAX_DEPTH)
			depth++;

		{
			const char *text = fy_str(name);
			size_t tl = strlen(text);

			entries = fy_append(gb, entries,
				fy_mapping(gb,
					"name", name,
					"depth", (long long)(depth - 1),
					"directory", tl && text[tl - 1] == '/',
					"comment", comment));
		}
	}

	if (fy_is_invalid(title))
		title = acc.title;

	p->d->model = fy_mapping(gb,
		"type", "treeView",
		"title", title,
		"accTitle", acc.title,
		"accDescr", acc.descr,
		"config", config,
		"entries", entries);
	return 0;
}
