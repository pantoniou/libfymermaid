/*
 * fymm-flowchart.c - the flowchart statement parser and model builder
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

#include "fymm-flowchart-shapes.inc"

/*
 * The node shapes, longest opening delimiter first so that `[[` is not read
 * as `[`. @open and @close bracket the label in the source.
 */
static const struct {
	const char *open;
	const char *close;
	const char *shape;
} fc_shapes[] = {
	{ "[[",	"]]",	"subroutine" },
	{ "[(",	")]",	"cylinder" },
	{ "[/",	"/]",	"parallelogram" },
	{ "[\\",	"\\]",	"parallelogram_alt" },
	{ "([",	"])",	"stadium" },
	{ "(((",	")))",	"doublecircle" },
	{ "((",	"))",	"circle" },
	{ "{{",	"}}",	"hexagon" },
	{ "[",	"]",	"rect" },
	{ "(",	")",	"round" },
	{ "{",	"}",	"rhombus" },
	{ ">",	"]",	"odd" },
};

#define FC_SHAPE_COUNT (sizeof(fc_shapes) / sizeof(fc_shapes[0]))

/* The characters an edge operator is built from. `x` and `o` close a stroke
 * run but are ordinary letters inside an identifier, so they do not end one:
 * `endpoint` is a node, not `endp` followed by a link. */
#define FC_STROKE "-=."
#define FC_EDGE_CHARS "-=.<>xo"
#define FC_ID_STOP "-=.<>"

struct fc {
	struct fymm_parser *p;
	struct fy_generic_builder *gb;
	fy_generic nodes;
	fy_generic edges;
	fy_generic subgraphs;
	fy_generic stack;	/* the open subgraph titles */
};

static bool fc_is(char c, const char *set)
{
	return c && strchr(set, c) != NULL;
}

/*
 * The close that matches the delimiter opened at @s, or NULL. Nesting of the
 * same delimiter is tracked, and a delimiter inside a quoted string is text:
 * `A["a] b"]` closes at the second `]`, and `node[hello ) world]` is not
 * troubled by the parenthesis at all.
 */
static const char *fc_match(const char *s, const char *e, const char *open,
			    const char *close)
{
	size_t ol = strlen(open), cl = strlen(close);
	bool in_quote = false;
	int depth = 0;
	const char *q;

	for (q = s; q < e; q++) {
		if (*q == '"' && (q == s || q[-1] != '\\')) {
			in_quote = !in_quote;
			continue;
		}
		if (in_quote)
			continue;
		if ((size_t)(e - q) >= ol && !memcmp(q, open, ol)) {
			depth++;
			q += ol - 1;
			continue;
		}
		if ((size_t)(e - q) >= cl && !memcmp(q, close, cl)) {
			if (!--depth)
				return q;
			q += cl - 1;
		}
	}
	return NULL;
}

/* Is @name a shape a source may ask for? */
static bool fc_shape_known(const char *name, size_t len)
{
	size_t i;

	for (i = 0; i < sizeof(fymm_flowchart_shapes) /
		    sizeof(fymm_flowchart_shapes[0]); i++) {
		if (strlen(fymm_flowchart_shapes[i]) == len &&
		    !memcmp(fymm_flowchart_shapes[i], name, len))
			return true;
	}
	return false;
}

/* Record a node the first time it is named; a later label replaces the id. */
static const char *fc_node(struct fc *f, const char *id, size_t idlen,
			   const char *text, const char *shape)
{
	const char *iid = fy_gb_intern_string_size(f->gb, id, idlen);
	fy_generic n;
	size_t i, count;

	count = fy_len(f->nodes);
	for (i = 0; i < count; i++) {
		n = fy_get_at(f->nodes, i);
		if (strcmp(fy_get(n, "id", ""), iid))
			continue;
		if (text)
			f->nodes = fy_replace(f->gb, f->nodes, i,
				fy_mapping(f->gb,
					"id", iid,
					"text", text,
					"shape", shape ? shape : "rect",
					"subgraph", fy_get(n, "subgraph")));
		return iid;
	}

	f->nodes = fy_append(f->gb, f->nodes,
		fy_mapping(f->gb,
			"id", iid,
			"text", text ? text : iid,
			"shape", shape ? shape : "rect",
			"subgraph", fy_len(f->stack) ?
				fy_get_at(f->stack, fy_len(f->stack) - 1) :
				fy_null));
	return iid;
}

/* Skip a `:::className` decoration; a terminal has no use for the class. */
static const char *fc_skip_class(const char *q, const char *e)
{
	if (e - q < 3 || memcmp(q, ":::", 3))
		return q;
	for (q += 3; q < e && (isalnum((unsigned char)*q) || *q == '_' ||
			       *q == '-'); q++)
		;
	return q;
}

/*
 * Read one node from @s: an id, then an optional shape with its label, or
 * `@{ ... }` metadata. Returns the position after it.
 */
static const char *fc_read_node(struct fc *f, const char *s, const char *e,
				const char **idp)
{
	fy_generic_sized_string input;
	const char *id = s, *q, *close, *ts, *te;
	const char *text = NULL, *shape = NULL;
	fy_generic meta;
	size_t i, ol, cl;

	/* the id runs until a shape delimiter, an edge, or a separator */
	for (q = s; q < e; q++) {
		if (fc_is(*q, "[({&@|") || fc_is(*q, FC_ID_STOP) ||
		    *q == ' ' || *q == '\t')
			break;
	}
	if (q == s)
		return NULL;

	/* `odd->Vertex Text]` is the odd shape on a node whose id ends in a
	 * minus, not a link. The trailing `]` is what tells them apart. */
	if (q + 1 < e && *q == '-' && q[1] == '>' && e[-1] == ']')
		q++;

	/* `@{ shape: rounded }` carries the shape as a mapping */
	if (q < e && *q == '@' && q + 1 < e && q[1] == '{') {
		close = fc_match(q + 1, e, "{", "}");
		if (!close) {
			fymm_diagf(f->p, true, f->p->lex.line, 1,
				   "unterminated node metadata");
			return NULL;
		}
		/*
		 * The braces hold either a flow mapping written on one line,
		 * `@{ shape: rounded, label: "DD" }`, or a block mapping over
		 * several lines whose entries carry no commas. A newline in
		 * the content is what tells them apart; the braces are kept
		 * for the flow form and dropped for the block one. Choosing
		 * before parsing avoids a failed attempt, whose diagnostic
		 * would reach the user.
		 */
		if (memchr(q + 2, '\n', (size_t)(close - (q + 2)))) {
			input.data = q + 2;
			input.size = (size_t)(close - (q + 2));
		} else {
			input.data = q + 1;
			input.size = (size_t)(close + 1 - (q + 1));
		}
		meta = fy_parse(f->gb, input,
				FYMM_YAML_PARSE_FLAGS | FYOPPF_INPUT_TYPE_STRING,
				NULL);
		if (!fy_is_mapping(meta)) {
			fymm_diagf(f->p, true, f->p->lex.line, 1,
				   "malformed node metadata");
			return NULL;
		}
		shape = fy_get(meta, "shape", (const char *)NULL);
		text = fy_get(meta, "label", (const char *)NULL);
		/* a YAML literal block keeps its final newline; a label
		 * should not carry one into the render */
		if (text) {
			size_t tl = strlen(text);

			while (tl && (text[tl - 1] == '\n' ||
				      text[tl - 1] == ' ' ||
				      text[tl - 1] == '\t'))
				tl--;
			text = fy_gb_intern_string_size(f->gb, text, tl);
		}
		if (shape && !fc_shape_known(shape, strlen(shape))) {
			fymm_diagf(f->p, true, f->p->lex.line, 1,
				   "unknown node shape '%s'", shape);
			return NULL;
		}
		*idp = fc_node(f, id, (size_t)(q - id), text, shape);
		return close + 1;
	}

	/* otherwise a bracketed label, longest delimiter first */
	for (i = 0; i < FC_SHAPE_COUNT; i++) {
		ol = strlen(fc_shapes[i].open);
		cl = strlen(fc_shapes[i].close);
		if ((size_t)(e - q) < ol || memcmp(q, fc_shapes[i].open, ol))
			continue;

		close = fc_match(q, e, fc_shapes[i].open, fc_shapes[i].close);
		/* an unterminated label runs to the end of the statement,
		 * which is what upstream does rather than refuse the line */
		if (!close) {
			close = e;
			cl = 0;
		}

		ts = q + ol;
		te = close;
		/* a quoted label keeps what is inside it, markdown included */
		if (te - ts >= 2 && *ts == '"' && te[-1] == '"') {
			ts++;
			te--;
			if (te - ts >= 2 && *ts == '`' && te[-1] == '`') {
				ts++;
				te--;
			}
		}
		*idp = fc_node(f, id, (size_t)(q - id),
			       fy_gb_intern_string_size(f->gb, ts,
							(size_t)(te - ts)),
			       fc_shapes[i].shape);
		q = fc_skip_class(close + cl, e);
		/* `C[Hello]@{ shape: circle }` carries both */
		if (q + 1 < e && *q == '@' && q[1] == '{') {
			const char *meta_end = fc_match(q + 1, e, "{", "}");

			if (!meta_end) {
				fymm_diagf(f->p, true, f->p->lex.line, 1,
					   "unterminated node metadata");
				return NULL;
			}
			q = meta_end + 1;
		}
		return q;
	}

	*idp = fc_node(f, id, (size_t)(q - id), NULL, NULL);
	return fc_skip_class(q, e);
}

/* struct fc_edge_op - one parsed edge operator */
struct fc_edge_op {
	const char *line;	/* solid, dotted or thick */
	const char *head;	/* arrow, none, circle or cross */
	const char *text;	/* the label, or NULL */
	bool bidirectional;
	const char *end;	/* where the operator finished */
};

static const char *fc_line_kind(char stroke)
{
	if (stroke == '=')
		return "thick";
	if (stroke == '.')
		return "dotted";
	return "solid";
}

static const char *fc_head_kind(char c)
{
	if (c == 'x')
		return "cross";
	if (c == 'o')
		return "circle";
	if (c == '>')
		return "arrow";
	return "none";
}

/*
 * Read an edge operator at @s. Two label styles exist: `-->|text|` puts the
 * label after the operator, and `-- text -->` puts it inside. Both end in a
 * stroke run that may carry the arrowhead.
 */
static bool fc_read_edge(struct fc *f, const char *s, const char *e,
			 struct fc_edge_op *op)
{
	const char *q = s, *run_end, *text_start, *run2, *run2_end;
	char stroke = 0;

	memset(op, 0, sizeof(*op));
	op->head = "none";

	/* an edge may be named: `A id1@--> B` */
	for (run_end = q; run_end < e; run_end++) {
		if (*run_end == '@' && run_end + 1 < e &&
		    fc_is(run_end[1], FC_STROKE "<")) {
			q = run_end + 1;
			break;
		}
		if (!isalnum((unsigned char)*run_end) && *run_end != '_')
			break;
	}

	if (q < e && *q == '<') {
		op->bidirectional = true;
		q++;
	}
	if (q >= e || !fc_is(*q, FC_STROKE))
		return false;

	for (run_end = q; run_end < e && fc_is(*run_end, FC_STROKE); run_end++) {
		if (*run_end != '-' || !stroke)
			stroke = *run_end;
	}
	op->line = fc_line_kind(stroke);

	/* an arrowhead ends the operator */
	if (run_end < e && fc_is(*run_end, ">xo")) {
		op->head = fc_head_kind(*run_end);
		op->end = run_end + 1;
		/* `-->|text|` */
		if (op->end < e && *op->end == '|') {
			const char *close = memchr(op->end + 1, '|',
						   (size_t)(e - op->end - 1));

			if (close) {
				op->text = fy_gb_intern_string_size(f->gb,
					op->end + 1,
					(size_t)(close - (op->end + 1)));
				op->end = close + 1;
			}
		}
		return true;
	}

	/*
	 * `-- text -->`: a later stroke run carries the head. The label may
	 * itself hold strokes, as in `-- test text with == -->`, so take the
	 * first run that is actually terminated by a head rather than the
	 * first run of any kind.
	 */
	text_start = run_end;
	for (run2 = run_end; run2 < e; run2++) {
		if (!fc_is(*run2, FC_STROKE))
			continue;
		for (run2_end = run2; run2_end < e &&
		     fc_is(*run2_end, FC_STROKE); run2_end++)
			;
		if (run2_end < e && fc_is(*run2_end, ">xo"))
			break;
		run2 = run2_end - 1;
	}
	if (run2 < e && run2 > text_start) {
		for (run2_end = run2; run2_end < e &&
		     fc_is(*run2_end, FC_STROKE); run2_end++)
			;
		if (run2_end < e && fc_is(*run2_end, ">xo")) {
			op->head = fc_head_kind(*run2_end);
			while (text_start < run2 && (*text_start == ' ' ||
						     *text_start == '\t'))
				text_start++;
			while (run2 > text_start && (run2[-1] == ' ' ||
						     run2[-1] == '\t'))
				run2--;
			op->text = run2 > text_start ?
				fy_gb_intern_string_size(f->gb, text_start,
					(size_t)(run2 - text_start)) : NULL;
			op->end = run2_end + 1;
			return true;
		}
	}

	/* a plain link with no head, such as `A --- B` */
	op->end = run_end;
	return true;
}

/*
 * A statement is a chain: `A & B --> C & D --> E`. Every node of one group
 * links to every node of the next, which is what the `&` form means.
 */
static void fc_stmt_chain(struct fc *f, const char *s, const char *e)
{
	const char *group[32], *next[32];
	struct fc_edge_op op;
	size_t ngroup = 0, nnext, i, j;
	const char *q = s, *id;

	while (q < e) {
		while (q < e && (*q == ' ' || *q == '\t'))
			q++;
		q = fc_read_node(f, q, e, &id);
		if (!q)
			return;
		if (ngroup < 32)
			group[ngroup++] = id;
		while (q < e && (*q == ' ' || *q == '\t'))
			q++;
		if (q < e && *q == '&') {
			q++;
			continue;
		}
		break;
	}
	if (!ngroup)
		return;

	while (q < e) {
		while (q < e && (*q == ' ' || *q == '\t'))
			q++;
		if (q >= e)
			break;
		if (!fc_read_edge(f, q, e, &op)) {
			fymm_diagf(f->p, true, f->p->lex.line, 1,
				   "expected a link between nodes");
			return;
		}
		q = op.end;

		nnext = 0;
		while (q < e) {
			while (q < e && (*q == ' ' || *q == '\t'))
				q++;
			q = fc_read_node(f, q, e, &id);
			if (!q)
				return;
			if (nnext < 32)
				next[nnext++] = id;
			while (q < e && (*q == ' ' || *q == '\t'))
				q++;
			if (q < e && *q == '&') {
				q++;
				continue;
			}
			break;
		}
		if (!nnext) {
			fymm_diagf(f->p, true, f->p->lex.line, 1,
				   "a link has no node on its right");
			return;
		}

		for (i = 0; i < ngroup; i++) {
			for (j = 0; j < nnext; j++)
				f->edges = fy_append(f->gb, f->edges,
					fy_mapping(f->gb,
						"from", group[i],
						"to", next[j],
						"line", op.line,
						"head", op.head,
						"bidirectional", op.bidirectional,
						"text", op.text ?
							fy_value(f->gb, op.text) :
							fy_null));
		}
		memcpy(group, next, nnext * sizeof(*group));
		ngroup = nnext;
	}
}

/*
 * Is the statement complete? A `@{ ... }` mapping and a quoted label may both
 * run over several lines, so a statement ends where its braces and its quotes
 * balance, not necessarily where the line does.
 */
static bool fc_balanced(const char *s, size_t len)
{
	bool in_quote = false;
	size_t i;
	int depth = 0;

	for (i = 0; i < len; i++) {
		if (s[i] == '"' && (!i || s[i - 1] != '\\'))
			in_quote = !in_quote;
		else if (in_quote)
			continue;
		else if (s[i] == '{')
			depth++;
		else if (s[i] == '}')
			depth -= depth > 0;
	}
	return !depth && !in_quote;
}

int fymm_parse_flowchart(struct fymm_parser *p, fy_generic config,
			 fy_generic title, struct fymm_token *htoks, int hn)
{
	struct fy_generic_builder *gb = p->d->gb;
	struct fymm_token toks[FYMM_MAX_TOKENS];
	struct fymm_acc acc = { fy_null, fy_null };
	struct fc f;
	const char *direction = "TB";
	const char *line, *e, *rest;
	char *joined = NULL;
	size_t len;
	int i, n;

	memset(&f, 0, sizeof(f));
	f.p = p;
	f.gb = gb;
	f.nodes = fy_seq_empty;
	f.edges = fy_seq_empty;
	f.subgraphs = fy_seq_empty;
	f.stack = fy_seq_empty;

	for (i = 1; i < hn; i++) {
		if (htoks[i].type == FYMM_TOK_COLON)
			continue;
		if (fymm_token_ieq(&htoks[i], "TD"))
			direction = "TB";
		else if (fymm_token_ieq(&htoks[i], "TB") ||
			 fymm_token_ieq(&htoks[i], "BT") ||
			 fymm_token_ieq(&htoks[i], "LR") ||
			 fymm_token_ieq(&htoks[i], "RL"))
			direction = fy_gb_intern_string_size(gb, htoks[i].text,
							     htoks[i].len);
		else
			fymm_diagf(p, false, htoks[i].line, htoks[i].col,
				   "ignoring unknown flowchart option '%.*s'",
				   (int)htoks[i].len, htoks[i].text);
	}

	while (fymm_lex_next_line(&p->lex)) {
		line = fymm_lex_line(&p->lex, &len);
		if (!len)
			continue;

		n = fymm_line_tokens(&p->lex, toks, FYMM_MAX_TOKENS);
		if (fymm_stmt_acc(p, toks, n, &acc)) {
			fymm_tokens_reset(toks, n);
			continue;
		}
		fymm_tokens_reset(toks, n);

		/* join the following lines while the statement is open */
		free(joined);
		joined = NULL;
		if (!fc_balanced(line, len)) {
			size_t cap = len * 2 + 64, used = len;

			joined = malloc(cap);
			if (!joined)
				break;
			memcpy(joined, line, len);
			joined[used] = '\0';
			while (!fc_balanced(joined, used) &&
			       fymm_lex_next_line(&p->lex)) {
				const char *more;
				size_t mlen;

				/* keep the indentation of a continuation
				 * line: a YAML literal block inside the
				 * metadata is defined by it */
				more = p->lex.ls;
				mlen = (size_t)(p->lex.le - p->lex.ls);
				if (used + mlen + 2 > cap) {
					char *nj;

					cap = (used + mlen + 2) * 2;
					nj = realloc(joined, cap);
					if (!nj)
						break;
					joined = nj;
				}
				joined[used++] = '\n';
				memcpy(joined + used, more, mlen);
				used += mlen;
				joined[used] = '\0';
			}
			line = joined;
			len = used;
		}
		e = line + len;

		/* the statements that decorate rather than connect are read
		 * and recorded; a terminal has no use for their styling */
		if (fymm_line_keyword(line, len, "click", &rest) ||
		    fymm_line_keyword(line, len, "style", &rest) ||
		    fymm_line_keyword(line, len, "classDef", &rest) ||
		    fymm_line_keyword(line, len, "class", &rest) ||
		    fymm_line_keyword(line, len, "linkStyle", &rest))
			continue;

		if (fymm_line_keyword(line, len, "direction", &rest)) {
			direction = fy_gb_intern_string_size(gb, rest,
							     (size_t)(e - rest));
			continue;
		}

		if (len >= 8 && !strncasecmp(line, "subgraph", 8)) {
			const char *t = line + 8;
			fy_generic name;

			while (t < e && (*t == ' ' || *t == '\t'))
				t++;
			name = t < e ? fymm_trim_text(gb, t, e) :
				       fy_value(gb, "");
			f.stack = fy_append(gb, f.stack, name);
			f.subgraphs = fy_append(gb, f.subgraphs,
				fy_mapping(gb, "title", name,
					   "depth", (long long)(fy_len(f.stack) - 1)));
			continue;
		}
		if (len == 3 && !strncasecmp(line, "end", 3)) {
			if (!fy_len(f.stack))
				fymm_diagf(p, true, p->lex.line, 1,
					   "'end' with no subgraph open");
			else
				f.stack = fy_slice(gb, f.stack, 0,
						   fy_len(f.stack) - 1);
			continue;
		}

		fc_stmt_chain(&f, line, e);
	}

	free(joined);

	if (fy_len(f.stack))
		fymm_diagf(p, true, p->lex.line, 1,
			   "a subgraph was left open at the end of the diagram");

	if (fy_is_invalid(title))
		title = acc.title;

	/* the layered renderer draws downwards; say so rather than let the
	 * reader assume the direction was honoured */
	if (strcmp(direction, "TB"))
		fymm_diagf(p, false, 1, 1,
			   "the %s direction is not implemented yet; rendering top to bottom",
			   direction);

	p->d->model = fy_mapping(gb,
		"type", "flowchart",
		"direction", direction,
		"title", title,
		"accTitle", acc.title,
		"accDescr", acc.descr,
		"config", config,
		"nodes", f.nodes,
		"edges", f.edges,
		"subgraphs", f.subgraphs);
	return 0;
}
