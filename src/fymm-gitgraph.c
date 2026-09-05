/*
 * fymm-gitgraph.c - the gitGraph statement parser and model builder
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fymm-internal.h"

/* the most tokens one statement line can carry before we stop caring */
#define GG_MAX_TOKENS 32

/* struct gg_branch - the state of one branch during a parse
 *
 * @name: the branch name, interned in the diagram builder
 * @order: the `order:` attribute; valid only when @has_order is set
 * @tip: the index of the commit the branch points at, or -1 for none
 * @appear: the position of the `branch` statement among the branch statements
 * @lane: the display row, assigned after each branch is known
 */
struct gg_branch {
	const char *name;
	long long order;
	bool has_order;
	int tip;
	size_t appear;
	int lane;
};

/* struct gg_commit - the parse-time index of a commit, for id lookups
 *
 * @p0: the first parent, or -1 for a root commit
 * @p1: the second parent of a merge, or -1
 */
struct gg_commit {
	const char *id;
	int branch;
	int p0, p1;
	bool is_merge;
};

struct gg {
	struct fymm_parser *p;
	struct fy_generic_builder *gb;

	struct gg_branch *branches;
	size_t nbranches, abranches;

	struct gg_commit *commits;
	size_t ncommits, acommits;

	fy_generic commit_seq;
	fy_generic acc_title;
	fy_generic acc_descr;
	int cur;		/* the checked out branch */
};

static int gg_grow(void **arrp, size_t *acount, size_t need, size_t esize)
{
	size_t na = *acount ? *acount : 8;
	void *n;

	if (need <= *acount)
		return 0;
	while (na < need)
		na *= 2;
	n = realloc(*arrp, na * esize);
	if (!n)
		return -1;
	*arrp = n;
	*acount = na;
	return 0;
}

static int gg_branch_find(struct gg *g, const char *name, size_t len)
{
	size_t i;

	for (i = 0; i < g->nbranches; i++) {
		if (strlen(g->branches[i].name) == len &&
		    !memcmp(g->branches[i].name, name, len))
			return (int)i;
	}
	return -1;
}

static int gg_commit_find(struct gg *g, const char *id, size_t len)
{
	size_t i;

	for (i = 0; i < g->ncommits; i++) {
		if (strlen(g->commits[i].id) == len &&
		    !memcmp(g->commits[i].id, id, len))
			return (int)i;
	}
	return -1;
}

/* Create a branch with its tip at @from. @from is -1 for no fork point. */
static int gg_branch_add(struct gg *g, const char *name, size_t len, int from)
{
	struct gg_branch *b;

	if (gg_grow((void **)&g->branches, &g->abranches, g->nbranches + 1,
		    sizeof(*g->branches)) < 0)
		return -1;
	b = &g->branches[g->nbranches];
	memset(b, 0, sizeof(*b));
	b->name = fy_gb_intern_string_size(g->gb, name, len);
	b->tip = from;
	b->appear = g->nbranches;
	b->lane = -1;
	return (int)g->nbranches++;
}

static const char *gg_commit_type_name(int type)
{
	static const char *const names[] = {
		"NORMAL", "REVERSE", "HIGHLIGHT", "MERGE", "CHERRY_PICK",
	};

	return type >= 0 && type < (int)(sizeof(names) / sizeof(names[0])) ?
	       names[type] : "NORMAL";
}

enum {
	GG_NORMAL = 0,
	GG_REVERSE,
	GG_HIGHLIGHT,
	GG_MERGE,
	GG_CHERRY_PICK,
};

/*
 * Record one commit. A NULL @id gets the sequence number of the commit.
 * Mermaid generates a random id in this case. A deterministic id keeps the
 * render reproducible and stays usable as a cherry-pick target.
 */
static int gg_commit_add(struct gg *g, const char *id, const char *label,
			 int type, fy_generic tags, int p0, int p1,
			 int cherry_from)
{
	struct gg_commit *c;
	fy_generic parents;
	char idbuf[32];
	int idx;

	if (gg_grow((void **)&g->commits, &g->acommits, g->ncommits + 1,
		    sizeof(*g->commits)) < 0)
		return -1;

	idx = (int)g->ncommits;
	if (!id) {
		snprintf(idbuf, sizeof(idbuf), "%d", idx);
		id = idbuf;
	}

	c = &g->commits[g->ncommits];
	c->id = fy_gb_intern_string(g->gb, id);
	c->branch = g->cur;
	c->p0 = p0;
	c->p1 = p1;
	c->is_merge = type == GG_MERGE;

	parents = fy_seq_empty;
	if (p0 >= 0)
		parents = fy_append(g->gb, parents, (long long)p0);
	if (p1 >= 0)
		parents = fy_append(g->gb, parents, (long long)p1);

	g->commit_seq = fy_append(g->gb, g->commit_seq,
		fy_mapping(g->gb,
			"id", c->id,
			"seq", (long long)idx,
			"branch", (long long)g->cur,
			"type", gg_commit_type_name(type),
			"label", label ? label : c->id,
			"tags", tags,
			"parents", parents,
			"cherryFrom", (long long)cherry_from));

	g->ncommits++;
	g->branches[g->cur].tip = idx;
	return idx;
}

/* Read one statement line into @toks. Returns the token count. */
static int gg_line_tokens(struct fymm_lex *l, struct fymm_token *toks, int max)
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

static void gg_tokens_reset(struct fymm_token *toks, int n)
{
	int i;

	for (i = 0; i < n; i++)
		fymm_token_reset(&toks[i]);
}

/* struct gg_attrs - the `name: value` attributes of one statement */
struct gg_attrs {
	const struct fymm_token *id;
	const struct fymm_token *msg;
	const struct fymm_token *tag;
	const struct fymm_token *type;
	const struct fymm_token *order;
	const struct fymm_token *parent;
};

/*
 * Read the `name: value` attributes of a statement, from token @i. An unknown
 * name is reported and skipped, so one error does not cascade.
 */
static void gg_parse_attrs(struct gg *g, struct fymm_token *toks, int n, int i,
			   struct gg_attrs *a, const char *stmt)
{
	const struct fymm_token *key, *val;

	memset(a, 0, sizeof(*a));

	while (i < n) {
		key = &toks[i];
		if (key->type != FYMM_TOK_WORD) {
			fymm_diagf(g->p, true, key->line, key->col,
				   "expected an attribute name in a '%s' statement",
				   stmt);
			return;
		}
		if (i + 1 >= n || toks[i + 1].type != FYMM_TOK_COLON) {
			fymm_diagf(g->p, true, key->line, key->col,
				   "expected ':' after '%.*s'",
				   (int)key->len, key->text);
			return;
		}
		if (i + 2 >= n) {
			fymm_diagf(g->p, true, key->line, key->col,
				   "missing value for '%.*s'",
				   (int)key->len, key->text);
			return;
		}
		val = &toks[i + 2];

		if (fymm_token_ieq(key, "id"))
			a->id = val;
		else if (fymm_token_ieq(key, "msg"))
			a->msg = val;
		else if (fymm_token_ieq(key, "tag"))
			a->tag = val;
		else if (fymm_token_ieq(key, "type"))
			a->type = val;
		else if (fymm_token_ieq(key, "order"))
			a->order = val;
		else if (fymm_token_ieq(key, "parent"))
			a->parent = val;
		else
			fymm_diagf(g->p, false, key->line, key->col,
				   "ignoring unknown attribute '%.*s' on '%s'",
				   (int)key->len, key->text, stmt);
		i += 3;
	}
}

/* Convert a `type:` attribute to a commit type. */
static int gg_commit_type(struct gg *g, const struct fymm_token *t)
{
	if (!t)
		return GG_NORMAL;
	if (fymm_token_ieq(t, "NORMAL"))
		return GG_NORMAL;
	if (fymm_token_ieq(t, "REVERSE"))
		return GG_REVERSE;
	if (fymm_token_ieq(t, "HIGHLIGHT"))
		return GG_HIGHLIGHT;
	fymm_diagf(g->p, true, t->line, t->col,
		   "unknown commit type '%.*s', expected NORMAL, REVERSE or HIGHLIGHT",
		   (int)t->len, t->text);
	return GG_NORMAL;
}

static fy_generic gg_tag_seq(struct gg *g, const struct fymm_token *t)
{
	if (!t || !t->len)
		return fy_seq_empty;
	return fy_sequence(g->gb, fy_value(g->gb, fy_gb_intern_string_size(
					g->gb, t->text, t->len)));
}

/*
 * Read an explicit commit id. A duplicate id makes a cherry-pick ambiguous.
 * Mermaid reports a duplicate on `commit` as a warning and keeps the commit,
 * and reports a duplicate merge id as an error; @fatal selects which.
 */
static const char *gg_take_id(struct gg *g, const struct fymm_token *t,
			      bool fatal)
{
	if (!t || !t->len)
		return NULL;
	if (gg_commit_find(g, t->text, t->len) >= 0) {
		fymm_diagf(g->p, fatal, t->line, t->col,
			   "duplicate commit id '%.*s'", (int)t->len, t->text);
		if (fatal)
			return NULL;
	}
	return fy_gb_intern_string_size(g->gb, t->text, t->len);
}

static void gg_stmt_commit(struct gg *g, struct fymm_token *toks, int n)
{
	struct gg_attrs a;
	const char *id, *label, *msg;
	int first, type;

	/* `commit "text"` is shorthand for `commit msg: "text"` */
	msg = NULL;
	first = 1;
	if (n >= 2 && toks[1].type == FYMM_TOK_STRING &&
	    (n < 3 || toks[2].type != FYMM_TOK_COLON)) {
		msg = fy_gb_intern_string_size(g->gb, toks[1].text,
					       toks[1].len);
		first = 2;
	}

	gg_parse_attrs(g, toks, n, first, &a, "commit");
	id = gg_take_id(g, a.id, false);
	type = gg_commit_type(g, a.type);
	if (a.msg && a.msg->len)
		msg = fy_gb_intern_string_size(g->gb, a.msg->text, a.msg->len);
	label = msg ? msg : id;

	gg_commit_add(g, id, label, type, gg_tag_seq(g, a.tag),
		      g->branches[g->cur].tip, -1, -1);
}

static void gg_stmt_branch(struct gg *g, struct fymm_token *toks, int n)
{
	struct gg_attrs a;
	char buf[32];
	char *end;
	int idx;

	if (n < 2 || (toks[1].type != FYMM_TOK_WORD &&
		      toks[1].type != FYMM_TOK_STRING)) {
		fymm_diagf(g->p, true, toks[0].line, toks[0].col,
			   "'branch' needs a name");
		return;
	}
	if (gg_branch_find(g, toks[1].text, toks[1].len) >= 0) {
		fymm_diagf(g->p, true, toks[1].line, toks[1].col,
			   "branch '%.*s' already exists",
			   (int)toks[1].len, toks[1].text);
		return;
	}

	gg_parse_attrs(g, toks, n, 2, &a, "branch");

	/* The new branch forks from the tip of the current branch. */
	idx = gg_branch_add(g, toks[1].text, toks[1].len,
			    g->branches[g->cur].tip);
	if (idx < 0)
		return;
	if (a.order) {
		snprintf(buf, sizeof(buf), "%.*s", (int)a.order->len,
			 a.order->text);
		end = NULL;
		g->branches[idx].order = strtoll(buf, &end, 10);
		if (end && *end) {
			fymm_diagf(g->p, true, a.order->line, a.order->col,
				   "'order' must be a number, got '%s'", buf);
		} else {
			g->branches[idx].has_order = true;
		}
	}
	g->cur = idx;
}

static void gg_stmt_checkout(struct gg *g, struct fymm_token *toks, int n)
{
	int idx;

	if (n < 2 || (toks[1].type != FYMM_TOK_WORD &&
		      toks[1].type != FYMM_TOK_STRING)) {
		fymm_diagf(g->p, true, toks[0].line, toks[0].col,
			   "'%.*s' needs a branch name",
			   (int)toks[0].len, toks[0].text);
		return;
	}
	idx = gg_branch_find(g, toks[1].text, toks[1].len);
	if (idx < 0) {
		fymm_diagf(g->p, true, toks[1].line, toks[1].col,
			   "unknown branch '%.*s'",
			   (int)toks[1].len, toks[1].text);
		return;
	}
	g->cur = idx;
}

static void gg_stmt_merge(struct gg *g, struct fymm_token *toks, int n)
{
	struct gg_attrs a;
	const char *id;
	int idx, type;

	if (n < 2 || (toks[1].type != FYMM_TOK_WORD &&
		      toks[1].type != FYMM_TOK_STRING)) {
		fymm_diagf(g->p, true, toks[0].line, toks[0].col,
			   "'merge' needs a branch name");
		return;
	}
	idx = gg_branch_find(g, toks[1].text, toks[1].len);
	if (idx < 0) {
		fymm_diagf(g->p, true, toks[1].line, toks[1].col,
			   "unknown branch '%.*s'",
			   (int)toks[1].len, toks[1].text);
		return;
	}
	if (idx == g->cur) {
		fymm_diagf(g->p, true, toks[1].line, toks[1].col,
			   "cannot merge branch '%s' with itself",
			   g->branches[idx].name);
		return;
	}
	if (g->branches[g->cur].tip < 0) {
		fymm_diagf(g->p, true, toks[0].line, toks[0].col,
			   "current branch '%s' has no commits to merge into",
			   g->branches[g->cur].name);
		return;
	}
	if (g->branches[idx].tip < 0) {
		fymm_diagf(g->p, true, toks[1].line, toks[1].col,
			   "branch '%s' has no commits to merge",
			   g->branches[idx].name);
		return;
	}
	if (g->branches[idx].tip == g->branches[g->cur].tip) {
		fymm_diagf(g->p, true, toks[1].line, toks[1].col,
			   "branch '%s' and '%s' have the same head",
			   g->branches[g->cur].name, g->branches[idx].name);
		return;
	}

	gg_parse_attrs(g, toks, n, 2, &a, "merge");
	id = gg_take_id(g, a.id, true);
	type = a.type ? gg_commit_type(g, a.type) : GG_MERGE;
	if (a.type)
		type = GG_MERGE;	/* the shape is fixed; only the styling varies */

	/* Label a merge commit only when the source names it. */
	gg_commit_add(g, id, id ? id : "", type, gg_tag_seq(g, a.tag),
		      g->branches[g->cur].tip, g->branches[idx].tip, -1);
}

static void gg_stmt_cherry_pick(struct gg *g, struct fymm_token *toks, int n)
{
	struct gg_attrs a;
	int src, parent;

	gg_parse_attrs(g, toks, n, 1, &a, "cherry-pick");
	if (!a.id || !a.id->len) {
		fymm_diagf(g->p, true, toks[0].line, toks[0].col,
			   "'cherry-pick' needs an 'id' attribute");
		return;
	}
	src = gg_commit_find(g, a.id->text, a.id->len);
	if (src < 0) {
		fymm_diagf(g->p, true, a.id->line, a.id->col,
			   "unknown commit id '%.*s'",
			   (int)a.id->len, a.id->text);
		return;
	}
	if (g->commits[src].branch == g->cur) {
		fymm_diagf(g->p, true, a.id->line, a.id->col,
			   "cannot cherry-pick commit '%.*s' onto its own branch",
			   (int)a.id->len, a.id->text);
		return;
	}
	if (g->branches[g->cur].tip < 0) {
		fymm_diagf(g->p, true, toks[0].line, toks[0].col,
			   "branch '%s' needs at least one commit before a cherry-pick",
			   g->branches[g->cur].name);
		return;
	}
	/* A merge commit has two parents. Cherry-picking one takes the side
	 * to keep, and that side must be an immediate parent. */
	if (a.parent && a.parent->len) {
		parent = gg_commit_find(g, a.parent->text, a.parent->len);
		if (parent < 0) {
			fymm_diagf(g->p, true, a.parent->line, a.parent->col,
				   "unknown parent commit id '%.*s'",
				   (int)a.parent->len, a.parent->text);
			return;
		}
		if (parent != g->commits[src].p0 &&
		    parent != g->commits[src].p1) {
			fymm_diagf(g->p, true, a.parent->line, a.parent->col,
				   "commit '%.*s' is not an immediate parent of '%.*s'",
				   (int)a.parent->len, a.parent->text,
				   (int)a.id->len, a.id->text);
			return;
		}
	} else if (g->commits[src].is_merge) {
		fymm_diagf(g->p, true, toks[0].line, toks[0].col,
			   "cherry-picking the merge commit '%.*s' needs a 'parent' attribute",
			   (int)a.id->len, a.id->text);
		return;
	}

	gg_commit_add(g, NULL, g->commits[src].id, GG_CHERRY_PICK,
		      gg_tag_seq(g, a.tag), g->branches[g->cur].tip, -1, src);
}

/*
 * Read an accessibility statement. `accTitle: text` and `accDescr: text` take
 * the rest of the line. `accDescr { ... }` takes each line up to a closing
 * brace. The text is free form, so it is read from the line and not through
 * the tokenizer.
 */
static void gg_stmt_acc(struct gg *g, struct fymm_token *toks, int n,
			bool title)
{
	fy_generic *slot = title ? &g->acc_title : &g->acc_descr;
	const char *s;
	char *text, *nt;
	size_t len, pos;

	if (n >= 2 && toks[1].type == FYMM_TOK_COLON) {
		s = fymm_lex_rest(&g->p->lex, toks[1].col + 1, &len);
		*slot = fy_value(g->gb, fy_gb_intern_string_size(g->gb, s, len));
		return;
	}

	if (title || n < 2 || !fymm_token_is(&toks[1], "{")) {
		fymm_diagf(g->p, true, toks[0].line, toks[0].col,
			   "expected ':' or '{' after '%.*s'",
			   (int)toks[0].len, toks[0].text);
		return;
	}

	/* the braced form:each line up to a line that holds only a closing brace */
	text = NULL;
	pos = 0;
	while (fymm_lex_next_line(&g->p->lex)) {
		s = fymm_lex_line(&g->p->lex, &len);
		if (len == 1 && *s == '}')
			break;
		nt = realloc(text, pos + len + 2);
		if (!nt) {
			free(text);
			return;
		}
		text = nt;
		if (pos)
			text[pos++] = '\n';
		memcpy(text + pos, s, len);
		pos += len;
		text[pos] = '\0';
	}
	if (text) {
		*slot = fy_value(g->gb, fy_gb_intern_string(g->gb, text));
		free(text);
	}
}

/*
 * Branch display order, as mermaid defines it. The main branch is first. The
 * branches without an `order:` attribute follow, in order of appearance. The
 * branches with an `order:` attribute follow those, in numeric order. An
 * explicit mainBranchOrder moves the main branch into the last group.
 */
struct gg_lane_key {
	int group;
	long long key;
	size_t appear;
	int idx;
};

static int gg_lane_cmp(const void *va, const void *vb)
{
	const struct gg_lane_key *a = va, *b = vb;

	if (a->group != b->group)
		return a->group < b->group ? -1 : 1;
	if (a->key != b->key)
		return a->key < b->key ? -1 : 1;
	if (a->appear != b->appear)
		return a->appear < b->appear ? -1 : 1;
	return 0;
}

static void gg_assign_lanes(struct gg *g, long long main_order)
{
	struct gg_lane_key *keys;
	size_t i;

	keys = malloc(g->nbranches * sizeof(*keys));
	if (!keys)
		return;

	for (i = 0; i < g->nbranches; i++) {
		keys[i].idx = (int)i;
		keys[i].appear = g->branches[i].appear;
		if (i == 0 && !main_order) {
			keys[i].group = 0;
			keys[i].key = 0;
		} else if (i == 0) {
			keys[i].group = 2;
			keys[i].key = main_order;
		} else if (!g->branches[i].has_order) {
			keys[i].group = 1;
			keys[i].key = 0;
		} else {
			keys[i].group = 2;
			keys[i].key = g->branches[i].order;
		}
	}
	qsort(keys, g->nbranches, sizeof(*keys), gg_lane_cmp);
	for (i = 0; i < g->nbranches; i++)
		g->branches[keys[i].idx].lane = (int)i;
	free(keys);
}

/* Set each config key the renderer reads that the source did not set. */
static fy_generic gg_config_defaults(struct fy_generic_builder *gb,
				     fy_generic config)
{
	static const struct {
		const char *key;
		bool value;
	} bools[] = {
		{ "showBranches", true },
		{ "showCommitLabel", true },
		{ "rotateCommitLabel", true },
		{ "parallelCommits", false },
	};
	size_t i;

	for (i = 0; i < sizeof(bools) / sizeof(bools[0]); i++) {
		if (fy_is_invalid(fy_get(config, bools[i].key)))
			config = fy_assoc(gb, config, bools[i].key,
					  bools[i].value);
	}
	if (fy_is_invalid(fy_get(config, "mainBranchName")))
		config = fy_assoc(gb, config, "mainBranchName", "main");
	if (fy_is_invalid(fy_get(config, "mainBranchOrder")))
		config = fy_assoc(gb, config, "mainBranchOrder", 0LL);
	return config;
}

int fymm_parse_gitgraph(struct fymm_parser *p, fy_generic config,
			const char *orientation, fy_generic title)
{
	struct fymm_token toks[GG_MAX_TOKENS];
	struct gg g;
	fy_generic branch_seq;
	const char *main_name;
	size_t i;
	int n;

	memset(&g, 0, sizeof(g));
	g.p = p;
	g.gb = p->d->gb;
	g.commit_seq = fy_seq_empty;
	g.acc_title = fy_null;
	g.acc_descr = fy_null;
	g.cur = 0;

	config = gg_config_defaults(g.gb, config);
	main_name = fy_get(config, "mainBranchName", "main");

	/* The main branch always exists, named or not. */
	if (gg_branch_add(&g, main_name, strlen(main_name), -1) < 0)
		return -1;

	while (fymm_lex_next_line(&p->lex)) {
		n = gg_line_tokens(&p->lex, toks, GG_MAX_TOKENS);
		if (!n)
			continue;

		if (toks[0].type != FYMM_TOK_WORD) {
			fymm_diagf(p, true, toks[0].line, toks[0].col,
				   "expected a gitGraph statement");
		} else if (fymm_token_ieq(&toks[0], "commit")) {
			gg_stmt_commit(&g, toks, n);
		} else if (fymm_token_ieq(&toks[0], "branch")) {
			gg_stmt_branch(&g, toks, n);
		} else if (fymm_token_ieq(&toks[0], "checkout") ||
			   fymm_token_ieq(&toks[0], "switch")) {
			gg_stmt_checkout(&g, toks, n);
		} else if (fymm_token_ieq(&toks[0], "merge")) {
			gg_stmt_merge(&g, toks, n);
		} else if (fymm_token_ieq(&toks[0], "cherry-pick") ||
			   fymm_token_ieq(&toks[0], "cherrypick")) {
			gg_stmt_cherry_pick(&g, toks, n);
		} else if (fymm_token_ieq(&toks[0], "accTitle")) {
			gg_stmt_acc(&g, toks, n, true);
		} else if (fymm_token_ieq(&toks[0], "accDescr")) {
			gg_stmt_acc(&g, toks, n, false);
		} else {
			fymm_diagf(p, true, toks[0].line, toks[0].col,
				   "unknown gitGraph statement '%.*s'",
				   (int)toks[0].len, toks[0].text);
		}
		gg_tokens_reset(toks, n);
	}

	gg_assign_lanes(&g, (long long)fy_get(config, "mainBranchOrder", 0LL));

	branch_seq = fy_seq_empty;
	for (i = 0; i < g.nbranches; i++)
		branch_seq = fy_append(g.gb, branch_seq,
			fy_mapping(g.gb,
				"name", g.branches[i].name,
				"index", (long long)i,
				"lane", (long long)g.branches[i].lane,
				"order", g.branches[i].has_order ?
					fy_value(g.gb, g.branches[i].order) :
					fy_null,
				"tip", (long long)g.branches[i].tip));

	p->d->model = fy_mapping(g.gb,
		"type", "gitGraph",
		"orientation", orientation,
		"title", fy_is_valid(title) ? title : g.acc_title,
		"accTitle", g.acc_title,
		"accDescr", g.acc_descr,
		"config", config,
		"branches", branch_seq,
		"commits", g.commit_seq);

	free(g.branches);
	free(g.commits);
	return 0;
}
