/*
 * fymm-internal.h - internal declarations shared across libfymermaid
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

#ifndef FYMM_INTERNAL_H
#define FYMM_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <libfymermaid.h>

/* The parse flags every YAML fragment in a mermaid document is read with:
 * frontmatter and %%{init: ...}%% directives alike.  JSON is a subset of
 * YAML 1.2, so the single-quoted JSON-ish shape mermaid's own documentation
 * uses parses without a separate path. */
#define FYMM_YAML_PARSE_FLAGS \
	(FYOPPF_DISABLE_DIRECTORY | FYOPPF_MODE_YAML_1_2)

/* struct fymm_diagram - the parsed diagram
 *
 * @gb: the builder owning every generic below
 * @own_gb: whether @gb was created here and must be destroyed
 * @type: the diagram kind
 * @model: the model mapping handed out by fymm_diagram_model()
 * @diags: the diagnostics sequence
 * @filename: the name diagnostics are reported against
 * @nerrors: how many of @diags are errors
 */
struct fymm_diagram {
	struct fy_generic_builder *gb;
	bool own_gb;
	enum fymm_diagram_type type;
	fy_generic model;
	fy_generic diags;
	const char *filename;
	size_t nerrors;
};

/* struct fymm_lex - a line oriented scanner over mermaid source
 *
 * Mermaid statements are newline terminated, so the scanner hands out one
 * logical line at a time and tokenizes within it.  @line and @col track the
 * position of the token last returned, for diagnostics.
 */
struct fymm_lex {
	const char *start;	/* whole input */
	const char *end;
	const char *p;		/* cursor */
	const char *ls;		/* start of the current line */
	const char *le;		/* end of the current line, sans newline */
	int line;		/* 1 based */
	int col;		/* 1 based, of the last token */
	bool semi;		/* a `;` also ends a statement */
	bool hash;		/* a `#` also opens a comment */
	bool mid_line;		/* the last statement ended at a `;` */
};

/* enum fymm_tok - the token kinds within a statement line */
enum fymm_tok {
	FYMM_TOK_EOL = 0,	/* the line is exhausted */
	FYMM_TOK_WORD,		/* a bare run of characters */
	FYMM_TOK_STRING,	/* a quoted string, already unescaped */
	FYMM_TOK_COLON,		/* ':' */
};

/* struct fymm_token - one token of a statement line
 *
 * @text points into either the input (for a word) or @buf (for a string that
 * needed unescaping), and is always NUL terminated through @buf when @owned.
 */
struct fymm_token {
	enum fymm_tok type;
	const char *text;
	size_t len;
	int line;
	int col;
	char *buf;		/* unescape scratch, freed by the caller */
};

void fymm_lex_init(struct fymm_lex *l, const char *text, size_t len);
bool fymm_lex_next_line(struct fymm_lex *l);
void fymm_token_reset(struct fymm_token *t);
bool fymm_lex_token(struct fymm_lex *l, struct fymm_token *t);
bool fymm_token_is(const struct fymm_token *t, const char *word);
bool fymm_token_ieq(const struct fymm_token *t, const char *word);
const char *fymm_lex_rest(const struct fymm_lex *l, int from_col, size_t *lenp);
const char *fymm_lex_line(const struct fymm_lex *l, size_t *lenp);

/* struct fymm_parser - the state threaded through a parse */
struct fymm_parser {
	struct fymm_diagram *d;
	struct fymm_lex lex;
	unsigned int flags;
};

void fymm_diagf(struct fymm_parser *p, bool error, int line, int col,
		const char *fmt, ...)
	__attribute__((format(printf, 5, 6)));

/* the most tokens one statement line can carry before we stop reading */
#define FYMM_MAX_TOKENS 32

int fymm_line_tokens(struct fymm_lex *l, struct fymm_token *toks, int max);
void fymm_tokens_reset(struct fymm_token *toks, int n);

/* struct fymm_acc - the accessibility statements every diagram type takes */
struct fymm_acc {
	fy_generic title;
	fy_generic descr;
};

/* Handle `accTitle:` and `accDescr:`, in both the inline and the braced form.
 * Returns true when the statement was one of them. */
bool fymm_stmt_acc(struct fymm_parser *p, struct fymm_token *toks, int n,
		   struct fymm_acc *acc);

/* Read the rest of a statement line as free text, interned in the builder. */
const char *fymm_rest_text(struct fymm_parser *p, int from_col);

/* Free text helpers, for the types whose statements are not word tokens. */
const char *fymm_split_colon(const char *s, const char *e);
fy_generic fymm_trim_text(struct fy_generic_builder *gb, const char *s,
			  const char *e);
bool fymm_line_keyword(const char *s, size_t len, const char *word,
		       const char **restp);

/*
 * struct fymm_diagram_ops - one diagram type
 *
 * @keyword: the word that opens the diagram, matched without case
 * @config_key: the key mermaid nests this type's settings under
 * @semicolons: whether a `;` separates statements as a newline does. It does
 *              for sequence and flowchart, and not for timeline, whose text
 *              carries semicolons of its own.
 * @hash_comment: whether a `#` opens a comment. It does for sequence; a class
 *                diagram uses `#` for visibility and a quadrant for a colour.
 * @type: the enum this type reports
 * @parse: reads the statements and builds the model; @toks holds the header
 *         line, which carries the type's own options
 * @render: draws a model of this type
 */
struct fymm_diagram_ops {
	const char *keyword;
	const char *config_key;
	bool semicolons;
	bool hash_comment;
	enum fymm_diagram_type type;
	int (*parse)(struct fymm_parser *p, fy_generic config,
		     fy_generic title, struct fymm_token *toks, int n);
	char *(*render)(const struct fymm_diagram *d, fy_generic model,
			const struct fymm_render_cfg *cfg);
};

const struct fymm_diagram_ops *fymm_diagram_ops_by_keyword(const char *word,
							   size_t len);
const struct fymm_diagram_ops *fymm_diagram_ops_by_type(enum fymm_diagram_type t);

/* Resolve the theme a render configuration asks for. */
struct fymm_theme;
int fymm_theme_resolve(struct fymm_theme *theme,
		       const struct fymm_render_cfg *cfg,
		       struct fy_generic_builder *gb);

/* one parser and one renderer per diagram type */
int fymm_parse_gitgraph(struct fymm_parser *p, fy_generic config,
			fy_generic title, struct fymm_token *toks, int n);
char *fymm_render_gitgraph(const struct fymm_diagram *d, fy_generic model,
			   const struct fymm_render_cfg *cfg);

/* how deep a mindmap may nest before the rails stop being tracked */
#define FYMM_MINDMAP_MAX_DEPTH 64

int fymm_parse_state(struct fymm_parser *p, fy_generic config,
		     fy_generic title, struct fymm_token *toks, int n);
char *fymm_render_state(const struct fymm_diagram *d, fy_generic model,
			const struct fymm_render_cfg *cfg);

int fymm_parse_gantt(struct fymm_parser *p, fy_generic config,
		     fy_generic title, struct fymm_token *toks, int n);
char *fymm_render_gantt(const struct fymm_diagram *d, fy_generic model,
			const struct fymm_render_cfg *cfg);

int fymm_parse_radar(struct fymm_parser *p, fy_generic config,
		     fy_generic title, struct fymm_token *toks, int n);
char *fymm_render_radar(const struct fymm_diagram *d, fy_generic model,
			const struct fymm_render_cfg *cfg);

int fymm_parse_xychart(struct fymm_parser *p, fy_generic config,
		       fy_generic title, struct fymm_token *toks, int n);
char *fymm_render_xychart(const struct fymm_diagram *d, fy_generic model,
			  const struct fymm_render_cfg *cfg);

int fymm_parse_quadrant(struct fymm_parser *p, fy_generic config,
			fy_generic title, struct fymm_token *toks, int n);
char *fymm_render_quadrant(const struct fymm_diagram *d, fy_generic model,
			   const struct fymm_render_cfg *cfg);

int fymm_parse_class(struct fymm_parser *p, fy_generic config,
		     fy_generic title, struct fymm_token *toks, int n);
char *fymm_render_class(const struct fymm_diagram *d, fy_generic model,
			const struct fymm_render_cfg *cfg);

int fymm_parse_flowchart(struct fymm_parser *p, fy_generic config,
			 fy_generic title, struct fymm_token *toks, int n);
char *fymm_render_flowchart(const struct fymm_diagram *d, fy_generic model,
			    const struct fymm_render_cfg *cfg);

int fymm_parse_sequence(struct fymm_parser *p, fy_generic config,
			fy_generic title, struct fymm_token *toks, int n);
char *fymm_render_sequence(const struct fymm_diagram *d, fy_generic model,
			   const struct fymm_render_cfg *cfg);

int fymm_parse_mindmap(struct fymm_parser *p, fy_generic config,
		       fy_generic title, struct fymm_token *toks, int n);
char *fymm_render_mindmap(const struct fymm_diagram *d, fy_generic model,
			  const struct fymm_render_cfg *cfg);

int fymm_parse_journey(struct fymm_parser *p, fy_generic config,
		       fy_generic title, struct fymm_token *toks, int n);
char *fymm_render_journey(const struct fymm_diagram *d, fy_generic model,
			  const struct fymm_render_cfg *cfg);

int fymm_parse_timeline(struct fymm_parser *p, fy_generic config,
			fy_generic title, struct fymm_token *toks, int n);
char *fymm_render_timeline(const struct fymm_diagram *d, fy_generic model,
			   const struct fymm_render_cfg *cfg);

int fymm_parse_pie(struct fymm_parser *p, fy_generic config,
		   fy_generic title, struct fymm_token *toks, int n);
char *fymm_render_pie(const struct fymm_diagram *d, fy_generic model,
		      const struct fymm_render_cfg *cfg);

#endif /* FYMM_INTERNAL_H */
