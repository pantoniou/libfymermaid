/*
 * fymm-c4.c - the C4 statement parser and model builder
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

/* The element calls, and how many positional arguments each names. */
static const struct {
	const char *name;
	const char *kind;
} c4_elements[] = {
	{ "Person_Ext",		"person" },
	{ "Person",		"person" },
	{ "System_Ext",		"system" },
	{ "SystemDb_Ext",	"system" },
	{ "SystemQueue_Ext",	"system" },
	{ "SystemDb",		"system" },
	{ "SystemQueue",	"system" },
	{ "System",		"system" },
	{ "ContainerDb_Ext",	"container" },
	{ "ContainerQueue_Ext",	"container" },
	{ "Container_Ext",	"container" },
	{ "ContainerDb",	"container" },
	{ "ContainerQueue",	"container" },
	{ "Container",		"container" },
	{ "ComponentDb_Ext",	"component" },
	{ "ComponentQueue_Ext",	"component" },
	{ "Component_Ext",	"component" },
	{ "ComponentDb",	"component" },
	{ "ComponentQueue",	"component" },
	{ "Component",		"component" },
	{ "Node_L",		"node" },
	{ "Node_R",		"node" },
	{ "Node",		"node" },
	{ "Deployment_Node",	"node" },
};

#define C4_ELEMENT_COUNT (sizeof(c4_elements) / sizeof(c4_elements[0]))

/* The relation calls; the direction suffix only changes the drawing. */
static const char *const c4_relations[] = {
	"BiRel", "Rel_Back", "Rel_Neighbor", "Rel_Up", "Rel_Down",
	"Rel_Left", "Rel_Right", "Rel_U", "Rel_D", "Rel_L", "Rel_R", "Rel",
};

#define C4_RELATION_COUNT (sizeof(c4_relations) / sizeof(c4_relations[0]))

/* The boundary calls, which open a group that `}` closes. */
static const char *const c4_boundaries[] = {
	"Enterprise_Boundary", "System_Boundary", "Container_Boundary",
	"Node_Boundary", "Deployment_Boundary", "Boundary",
};

#define C4_BOUNDARY_COUNT (sizeof(c4_boundaries) / sizeof(c4_boundaries[0]))

/*
 * Split the argument list of a call on commas that are not inside quotes,
 * and separate the `$name="value"` ones from the positional ones.
 */
static int c4_args(struct fy_generic_builder *gb, const char *s, const char *e,
		   fy_generic *positional, fy_generic *named)
{
	const char *start = s, *q;
	bool in_quote = false;
	int n = 0;

	*positional = fy_seq_empty;
	*named = fy_map_empty;

	for (q = s; q <= e; q++) {
		if (q < e && *q == '"' && (q == s || q[-1] != '\\')) {
			in_quote = !in_quote;
			continue;
		}
		if (q < e && (in_quote || *q != ','))
			continue;

		{
			const char *as = start, *ae = q;
			const char *eq;

			while (as < ae && (*as == ' ' || *as == '\t'))
				as++;
			while (ae > as && (ae[-1] == ' ' || ae[-1] == '\t'))
				ae--;
			if (ae - as >= 2 && *as == '"' && ae[-1] == '"') {
				as++;
				ae--;
			}

			if (ae > as && *as == '$' &&
			    (eq = memchr(as, '=', (size_t)(ae - as))) != NULL) {
				const char *vs = eq + 1, *ve = ae;

				if (ve - vs >= 2 && *vs == '"' && ve[-1] == '"') {
					vs++;
					ve--;
				}
				*named = fy_assoc(gb, *named,
					fymm_trim_text(gb, as + 1, eq),
					fymm_trim_text(gb, vs, ve));
			} else if (ae > as) {
				*positional = fy_append(gb, *positional,
					fymm_trim_text(gb, as, ae));
				n++;
			}
		}
		start = q + 1;
	}
	return n;
}

int fymm_parse_c4(struct fymm_parser *p, fy_generic config, fy_generic title,
		  struct fymm_token *htoks, int hn)
{
	struct fy_generic_builder *gb = p->d->gb;
	struct fymm_token toks[FYMM_MAX_TOKENS];
	struct fymm_acc acc = { fy_null, fy_null };
	fy_generic elements = fy_seq_empty, relations = fy_seq_empty;
	fy_generic positional, named, boundary = fy_null;
	const char *line, *e, *rest, *open, *close;
	const char *kind = fy_gb_intern_string(gb, "C4Context");
	size_t len, i;
	int n, nargs;

	if (hn > 0)
		kind = fy_gb_intern_string_size(gb, htoks[0].text,
						htoks[0].len);
	for (i = 1; i < (size_t)hn; i++) {
		if (htoks[i].type == FYMM_TOK_COLON)
			continue;
		fymm_diagf(p, false, htoks[i].line, htoks[i].col,
			   "ignoring unknown C4 option '%.*s'",
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

		if (len == 1 && *line == '}') {
			boundary = fy_null;
			continue;
		}
		if (fymm_line_keyword(line, len, "title", &rest)) {
			if (fy_is_invalid(title))
				title = fymm_trim_text(gb, rest, e);
			continue;
		}
		if (fymm_line_keyword(line, len, "UpdateElementStyle", &rest) ||
		    fymm_line_keyword(line, len, "UpdateRelStyle", &rest) ||
		    fymm_line_keyword(line, len, "UpdateLayoutConfig", &rest))
			continue;

		open = memchr(line, '(', len);
		close = open ? fymm_memrchr(open, ')', (size_t)(e - open)) : NULL;
		if (!open || !close) {
			fymm_diagf(p, true, p->lex.line, 1,
				   "unknown C4 statement '%.*s'", (int)len,
				   line);
			continue;
		}
		nargs = c4_args(gb, open + 1, close, &positional, &named);

		/* a boundary opens a group; `{` may trail the call */
		for (i = 0; i < C4_BOUNDARY_COUNT; i++) {
			if ((size_t)(open - line) != strlen(c4_boundaries[i]) ||
			    strncasecmp(line, c4_boundaries[i],
					(size_t)(open - line)))
				continue;
			boundary = fy_get_at(positional, 0);
			elements = fy_append(gb, elements,
				fy_mapping(gb,
					"id", boundary,
					"label", nargs > 1 ?
						fy_get_at(positional, 1) :
						boundary,
					"kind", "boundary",
					"args", positional,
					"named", named,
					"parent", fy_null));
			break;
		}
		if (i < C4_BOUNDARY_COUNT)
			continue;

		for (i = 0; i < C4_RELATION_COUNT; i++) {
			if ((size_t)(open - line) != strlen(c4_relations[i]) ||
			    strncasecmp(line, c4_relations[i],
					(size_t)(open - line)))
				continue;
			relations = fy_append(gb, relations,
				fy_mapping(gb,
					"call", c4_relations[i],
					"from", fy_get_at(positional, 0),
					"to", nargs > 1 ?
						fy_get_at(positional, 1) :
						fy_null,
					"label", nargs > 2 ?
						fy_get_at(positional, 2) :
						fy_null,
					"named", named));
			break;
		}
		if (i < C4_RELATION_COUNT)
			continue;

		for (i = 0; i < C4_ELEMENT_COUNT; i++) {
			if ((size_t)(open - line) != strlen(c4_elements[i].name) ||
			    strncasecmp(line, c4_elements[i].name,
					(size_t)(open - line)))
				continue;
			elements = fy_append(gb, elements,
				fy_mapping(gb,
					"id", fy_get_at(positional, 0),
					"label", nargs > 1 ?
						fy_get_at(positional, 1) :
						fy_get_at(positional, 0),
					"description", nargs > 2 ?
						fy_get_at(positional, 2) :
						fy_null,
					"kind", c4_elements[i].kind,
					"call", c4_elements[i].name,
					"named", named,
					"parent", boundary));
			break;
		}
		if (i < C4_ELEMENT_COUNT)
			continue;

		fymm_diagf(p, true, p->lex.line, 1,
			   "unknown C4 statement '%.*s'", (int)(open - line),
			   line);
	}

	if (fy_is_invalid(title))
		title = acc.title;

	p->d->model = fy_mapping(gb,
		"type", "c4",
		"c4Type", kind,
		"title", title,
		"accTitle", acc.title,
		"accDescr", acc.descr,
		"config", config,
		"elements", elements,
		"relations", relations);
	return 0;
}
