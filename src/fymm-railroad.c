/*
 * fymm-railroad.c - grammar parsing for railroad diagrams
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

/*
 * A railroad diagram is drawn from a grammar, and mermaid accepts four
 * notations for one. They differ only in their surface: all four reduce to the
 * same tree of terminals, non-terminals, sequences, choices and repetitions,
 * so each dialect gets its own expression reader over a shared node model.
 */
enum rr_dialect {
	RR_IR,		/* the function call notation of railroad-beta */
	RR_EBNF,
	RR_PEG,
	RR_ABNF,
};

struct rr {
	struct fymm_parser *p;
	struct fy_generic_builder *gb;
	const char *s;		/* cursor */
	const char *e;
	enum rr_dialect dialect;
	int line;
	bool failed;
};

static void rr_error(struct rr *rr, const char *fmt, ...)
	__attribute__((format(printf, 2, 3)));

static void rr_error(struct rr *rr, const char *fmt, ...)
{
	va_list ap;

	if (rr->failed)
		return;
	rr->failed = true;
	va_start(ap, fmt);
	fymm_vdiagf(rr->p, true, rr->line, 1, fmt, ap);
	va_end(ap);
}

/* whitespace, block comments and `//` to end of line all separate tokens */
static void rr_skip(struct rr *rr)
{
	while (rr->s < rr->e) {
		if (*rr->s == '\n') {
			rr->line++;
			rr->s++;
		} else if (isspace((unsigned char)*rr->s)) {
			rr->s++;
		} else if (rr->e - rr->s >= 2 && rr->s[0] == '/' &&
			   rr->s[1] == '*') {
			rr->s += 2;
			while (rr->s < rr->e &&
			       !(rr->e - rr->s >= 2 && rr->s[0] == '*' &&
				 rr->s[1] == '/')) {
				if (*rr->s == '\n')
					rr->line++;
				rr->s++;
			}
			if (rr->s < rr->e)
				rr->s += 2;
		} else if (rr->e - rr->s >= 2 && rr->s[0] == '/' &&
			   rr->s[1] == '/') {
			while (rr->s < rr->e && *rr->s != '\n')
				rr->s++;
		} else {
			break;
		}
	}
}

static bool rr_eof(struct rr *rr)
{
	rr_skip(rr);
	return rr->s >= rr->e;
}

static bool rr_peek(struct rr *rr, const char *lit)
{
	size_t len = strlen(lit);

	rr_skip(rr);
	return (size_t)(rr->e - rr->s) >= len && !memcmp(rr->s, lit, len);
}

static bool rr_eat(struct rr *rr, const char *lit)
{
	if (!rr_peek(rr, lit))
		return false;
	rr->s += strlen(lit);
	return true;
}

/* a name is a run of identifier characters, with `-` and `_` allowed inside */
static bool rr_name(struct rr *rr, const char **sp, const char **ep)
{
	const char *b;

	rr_skip(rr);
	b = rr->s;
	while (rr->s < rr->e && (isalnum((unsigned char)*rr->s) ||
				 *rr->s == '_' || *rr->s == '-'))
		rr->s++;
	if (rr->s == b)
		return false;
	*sp = b;
	*ep = rr->s;
	return true;
}

/*
 * A string is double quoted and must close on the same line; an unterminated
 * one is an error rather than a run to end of input, because that is what it
 * always means.
 */
static bool rr_string(struct rr *rr, fy_generic *out)
{
	const char *b;
	char *buf;
	size_t n = 0;

	rr_skip(rr);
	if (rr->s >= rr->e || *rr->s != '"')
		return false;
	rr->s++;
	b = rr->s;
	buf = malloc((size_t)(rr->e - b) + 1);
	if (!buf) {
		rr_error(rr, "out of memory");
		return false;
	}
	while (rr->s < rr->e && *rr->s != '"' && *rr->s != '\n') {
		if (*rr->s == '\\' && rr->s + 1 < rr->e)
			rr->s++;
		buf[n++] = *rr->s++;
	}
	if (rr->s >= rr->e || *rr->s != '"') {
		free(buf);
		rr_error(rr, "unterminated string literal");
		return false;
	}
	rr->s++;
	*out = fy_value(rr->gb, fy_gb_intern_string_size(rr->gb, buf, n));
	free(buf);
	return true;
}

static fy_generic rr_leaf(struct rr *rr, const char *kind, fy_generic text)
{
	return fy_mapping(rr->gb, "kind", kind, "text", text,
			  "items", fy_seq_empty);
}

static fy_generic rr_leaf_range(struct rr *rr, const char *kind,
				const char *s, const char *e)
{
	return rr_leaf(rr, kind, fymm_trim_text(rr->gb, s, e));
}

static fy_generic rr_node(struct rr *rr, const char *kind, fy_generic items)
{
	return fy_mapping(rr->gb, "kind", kind, "text", fy_null,
			  "items", items);
}

/*
 * A sequence or a choice of one element is that element; mermaid collapses
 * them, and keeping the wrapper would draw a rail around nothing.
 */
static fy_generic rr_group(struct rr *rr, const char *kind, fy_generic items)
{
	if (fy_len(items) == 1)
		return fy_get_at(items, 0);
	return rr_node(rr, kind, items);
}

static fy_generic rr_expr(struct rr *rr);

/* railroad-beta: `choice(terminal("a"), nonterminal("b"))` */
static fy_generic rr_expr_ir(struct rr *rr)
{
	static const struct {
		const char *word;
		const char *kind;
		bool leaf;
	} calls[] = {
		{ "terminal",	"terminal",	true },
		{ "nonterminal","nonterminal",	true },
		{ "special",	"special",	true },
		{ "sequence",	"sequence",	false },
		{ "choice",	"choice",	false },
		{ "optional",	"optional",	false },
		{ "zeroOrMore",	"zeroOrMore",	false },
		{ "oneOrMore",	"oneOrMore",	false },
	};
	const char *ns, *ne;
	fy_generic text, items;
	size_t i, len;

	if (rr_string(rr, &text))
		return rr_leaf(rr, "terminal", text);
	if (rr->failed)
		return fy_null;

	if (!rr_name(rr, &ns, &ne)) {
		rr_error(rr, "expected an expression");
		return fy_null;
	}
	len = (size_t)(ne - ns);

	if (!rr_eat(rr, "("))
		return rr_leaf_range(rr, "nonterminal", ns, ne);

	for (i = 0; i < sizeof(calls) / sizeof(calls[0]); i++) {
		if (strlen(calls[i].word) == len &&
		    !memcmp(calls[i].word, ns, len))
			break;
	}
	if (i == sizeof(calls) / sizeof(calls[0])) {
		rr_error(rr, "unknown railroad constructor '%.*s'",
			 (int)len, ns);
		return fy_null;
	}

	if (calls[i].leaf) {
		if (!rr_string(rr, &text)) {
			if (!rr->failed)
				rr_error(rr, "%s() takes a string",
					 calls[i].word);
			return fy_null;
		}
		if (!rr_eat(rr, ")")) {
			rr_error(rr, "expected ')'");
			return fy_null;
		}
		return rr_leaf(rr, calls[i].kind, text);
	}

	items = fy_seq_empty;
	if (!rr_peek(rr, ")")) {
		do {
			fy_generic sub = rr_expr(rr);

			if (rr->failed)
				return fy_null;
			items = fy_append(rr->gb, items, sub);
		} while (rr_eat(rr, ","));
	}
	if (!rr_eat(rr, ")")) {
		rr_error(rr, "expected ')'");
		return fy_null;
	}

	if (!strcmp(calls[i].kind, "sequence") ||
	    !strcmp(calls[i].kind, "choice"))
		return rr_group(rr, calls[i].kind, items);
	return rr_node(rr, calls[i].kind, items);
}

/* the suffixes `?`, `*` and `+` mean the same in EBNF, PEG and the IR */
static fy_generic rr_suffix(struct rr *rr, fy_generic node)
{
	for (;;) {
		const char *kind;

		if (rr_eat(rr, "?"))
			kind = "optional";
		else if (rr_eat(rr, "*"))
			kind = "zeroOrMore";
		else if (rr_eat(rr, "+"))
			kind = "oneOrMore";
		else
			return node;
		node = rr_node(rr, kind,
			       fy_append(rr->gb, fy_seq_empty, node));
	}
}

static bool rr_at_end(struct rr *rr)
{
	return rr_eof(rr) || rr_peek(rr, ";");
}

/* EBNF: `a | b`, `[ x ]`, `{ x }`, `( x )`, `? special ?` */
static fy_generic rr_prim_ebnf(struct rr *rr)
{
	const char *ns, *ne, *b;
	fy_generic text, inner;

	if (rr_string(rr, &text))
		return rr_leaf(rr, "terminal", text);
	if (rr->failed)
		return fy_null;

	if (rr_eat(rr, "?")) {
		b = rr->s;
		while (rr->s < rr->e && *rr->s != '?') {
			if (*rr->s == '\n')
				rr->line++;
			rr->s++;
		}
		if (!rr_eat(rr, "?")) {
			rr_error(rr, "unterminated special sequence");
			return fy_null;
		}
		return rr_leaf_range(rr, "special", b, rr->s - 1);
	}

	if (rr_eat(rr, "[")) {
		inner = rr_expr(rr);
		if (!rr_eat(rr, "]") && !rr->failed)
			rr_error(rr, "expected ']'");
		return rr->failed ? fy_null :
			rr_node(rr, "optional",
				fy_append(rr->gb, fy_seq_empty, inner));
	}
	if (rr_eat(rr, "{")) {
		inner = rr_expr(rr);
		if (!rr_eat(rr, "}") && !rr->failed)
			rr_error(rr, "expected '}'");
		return rr->failed ? fy_null :
			rr_node(rr, "zeroOrMore",
				fy_append(rr->gb, fy_seq_empty, inner));
	}
	if (rr_eat(rr, "(")) {
		inner = rr_expr(rr);
		if (!rr_eat(rr, ")") && !rr->failed)
			rr_error(rr, "expected ')'");
		return rr->failed ? fy_null : inner;
	}
	if (rr_name(rr, &ns, &ne))
		return rr_leaf_range(rr, "nonterminal", ns, ne);

	rr_error(rr, "expected an expression");
	return fy_null;
}

/* PEG: `a / b`, `&a`, `!a`, `.` */
static fy_generic rr_prim_peg(struct rr *rr)
{
	const char *ns, *ne;
	fy_generic text, inner;

	if (rr_eat(rr, "&") || rr_eat(rr, "!")) {
		const char *kind = rr->s[-1] == '&' ? "and" : "not";

		inner = rr_prim_peg(rr);
		return rr->failed ? fy_null :
			rr_node(rr, kind,
				fy_append(rr->gb, fy_seq_empty, inner));
	}
	if (rr_string(rr, &text))
		return rr_leaf(rr, "terminal", text);
	if (rr->failed)
		return fy_null;
	if (rr_eat(rr, "."))
		return rr_leaf(rr, "any", fy_null);
	if (rr_eat(rr, "(")) {
		inner = rr_expr(rr);
		if (!rr_eat(rr, ")") && !rr->failed)
			rr_error(rr, "expected ')'");
		return rr->failed ? fy_null : inner;
	}
	if (rr_name(rr, &ns, &ne))
		return rr_leaf_range(rr, "nonterminal", ns, ne);

	rr_error(rr, "expected an expression");
	return fy_null;
}

/*
 * ABNF puts its repetition in front: `*x`, `1*x`, `2*4x` and a bare count.
 * Only the shape matters to the drawing, so the bounds collapse onto the two
 * repetitions a rail can show.
 */
static fy_generic rr_prim_abnf(struct rr *rr)
{
	const char *ns, *ne;
	fy_generic text, inner;
	bool star = false, atleast = false;

	rr_skip(rr);
	while (rr->s < rr->e && isdigit((unsigned char)*rr->s)) {
		atleast = true;
		rr->s++;
	}
	if (rr_eat(rr, "*")) {
		star = true;
		while (rr->s < rr->e && isdigit((unsigned char)*rr->s))
			rr->s++;
	}

	/* `%x41`, `%d65-90`, `%b0100.0001`: a terminal named by code point */
	if (rr_peek(rr, "%")) {
		const char *b = rr->s;

		rr->s++;
		while (rr->s < rr->e && (isalnum((unsigned char)*rr->s) ||
					 *rr->s == '-' || *rr->s == '.'))
			rr->s++;
		if (rr->s == b + 1) {
			rr_error(rr, "expected a numeric value after '%%'");
			return fy_null;
		}
		inner = rr_leaf_range(rr, "terminal", b, rr->s);
	} else if (rr_string(rr, &text)) {
		inner = rr_leaf(rr, "terminal", text);
	} else if (rr->failed) {
		return fy_null;
	} else if (rr_eat(rr, "(")) {
		inner = rr_expr(rr);
		if (!rr_eat(rr, ")") && !rr->failed)
			rr_error(rr, "expected ')'");
		if (rr->failed)
			return fy_null;
	} else if (rr_eat(rr, "[")) {
		inner = rr_expr(rr);
		if (!rr_eat(rr, "]") && !rr->failed)
			rr_error(rr, "expected ']'");
		if (rr->failed)
			return fy_null;
		inner = rr_node(rr, "optional",
				fy_append(rr->gb, fy_seq_empty, inner));
	} else if (rr_name(rr, &ns, &ne)) {
		inner = rr_leaf_range(rr, "nonterminal", ns, ne);
	} else {
		rr_error(rr, "expected an expression");
		return fy_null;
	}

	if (star)
		inner = rr_node(rr, atleast ? "oneOrMore" : "zeroOrMore",
				fy_append(rr->gb, fy_seq_empty, inner));
	return inner;
}

/* one concatenated run, up to the next alternation bar or terminator */
static fy_generic rr_cat(struct rr *rr)
{
	fy_generic items = fy_seq_empty, node;

	for (;;) {
		if (rr_at_end(rr) || rr_peek(rr, "|") || rr_peek(rr, "/") ||
		    rr_peek(rr, ")") || rr_peek(rr, "]") ||
		    rr_peek(rr, "}") || rr_peek(rr, ","))
			break;

		switch (rr->dialect) {
		case RR_EBNF:
			node = rr_suffix(rr, rr_prim_ebnf(rr));
			break;
		case RR_PEG:
			node = rr_suffix(rr, rr_prim_peg(rr));
			break;
		case RR_ABNF:
			node = rr_prim_abnf(rr);
			break;
		default:
			node = rr_expr_ir(rr);
			break;
		}
		if (rr->failed)
			return fy_null;
		items = fy_append(rr->gb, items, node);
	}

	if (fy_empty(items)) {
		rr_error(rr, "expected an expression");
		return fy_null;
	}
	return rr_group(rr, "sequence", items);
}

/* the alternation bar is `|` in EBNF and `/` in PEG and ABNF */
static fy_generic rr_expr(struct rr *rr)
{
	fy_generic items = fy_seq_empty, node;

	if (rr->dialect == RR_IR)
		return rr_expr_ir(rr);

	for (;;) {
		node = rr_cat(rr);
		if (rr->failed)
			return fy_null;
		items = fy_append(rr->gb, items, node);
		if (rr->dialect == RR_EBNF) {
			if (!rr_eat(rr, "|"))
				break;
		} else if (!rr_eat(rr, "/")) {
			break;
		}
	}
	return rr_group(rr, "choice", items);
}

/*
 * The grammar is a list of `name = expression ;` rules, with an optional
 * quoted title before them. The terminator is not decoration: without it there
 * is no way to know a rule has ended, so a missing one is an error.
 */
int fymm_parse_railroad(struct fymm_parser *p, fy_generic config,
			fy_generic title, struct fymm_token *htoks, int hn)
{
	struct fy_generic_builder *gb = p->d->gb;
	struct rr rr = {
		.p = p,
		.gb = gb,
		.s = p->lex.p,
		.e = p->lex.end,
		.line = p->lex.line + 1,
		.failed = false,
	};
	fy_generic rules = fy_seq_empty, expr, text;
	const char *ns, *ne;
	int i;

	if (fymm_token_ieq(&htoks[0], "railroad-ebnf-beta"))
		rr.dialect = RR_EBNF;
	else if (fymm_token_ieq(&htoks[0], "railroad-peg-beta"))
		rr.dialect = RR_PEG;
	else if (fymm_token_ieq(&htoks[0], "railroad-abnf-beta"))
		rr.dialect = RR_ABNF;
	else
		rr.dialect = RR_IR;

	for (i = 1; i < hn; i++) {
		if (htoks[i].type == FYMM_TOK_COLON)
			continue;
		fymm_diagf(p, false, htoks[i].line, htoks[i].col,
			   "ignoring unknown railroad option '%.*s'",
			   (int)htoks[i].len, htoks[i].text);
	}

	/* consumed wholesale: a grammar rule is free to span lines */
	p->lex.p = p->lex.end;

	while (!rr_eof(&rr) && !rr.failed) {
		const char *save = rr.s;

		if (rr_eat(&rr, "title")) {
			if (rr_string(&rr, &text)) {
				if (fy_is_invalid(title) || fy_is_null(title))
					title = text;
				continue;
			}
			if (rr.failed)
				break;
			/* not a title after all, but a rule so named */
			rr.s = save;
		}

		if (!rr_name(&rr, &ns, &ne)) {
			rr_error(&rr, "expected a rule name");
			break;
		}
		if (!rr_eat(&rr, "<-") && !rr_eat(&rr, "::=") &&
		    !rr_eat(&rr, "=")) {
			rr_error(&rr, "expected '=' after rule '%.*s'",
				 (int)(ne - ns), ns);
			break;
		}

		expr = rr_expr(&rr);
		if (rr.failed)
			break;
		if (!rr_eat(&rr, ";")) {
			rr_error(&rr, "rule '%.*s' is missing its ';'",
				 (int)(ne - ns), ns);
			break;
		}

		rules = fy_append(gb, rules,
			fy_mapping(gb,
				"name", fymm_trim_text(gb, ns, ne),
				"expr", expr));
	}

	p->d->model = fy_mapping(gb,
		"type", "railroad",
		"title", title,
		"config", config,
		"dialect", rr.dialect == RR_EBNF ? "ebnf" :
			   rr.dialect == RR_PEG ? "peg" :
			   rr.dialect == RR_ABNF ? "abnf" : "ir",
		"rules", rules);
	return 0;
}
