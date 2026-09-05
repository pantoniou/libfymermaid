/*
 * fymm-sequence.c - the sequence diagram statement parser and model builder
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
 * The arrow forms, longest first: `-->>` must be recognised before `-->`,
 * and `<<-->>` before `<<->>`. @line is how the shaft is drawn, @head what
 * it ends in, and @both marks a bidirectional arrow.
 */
static const struct {
	const char *text;
	const char *line;
	const char *head;
	bool both;
} seq_arrows[] = {
	{ "<<-->>", "dotted", "arrow",	true },
	{ "<<->>",  "solid",  "arrow",	true },
	{ "-->>",   "dotted", "arrow",	false },
	{ "--x",    "dotted", "cross",	false },
	{ "--)",    "dotted", "open",	false },
	{ "->>",    "solid",  "arrow",	false },
	{ "-->",    "dotted", "none",	false },
	{ "-x",     "solid",  "cross",	false },
	{ "-)",     "solid",  "open",	false },
	{ "->",     "solid",  "none",	false },
	/* the branching forms; they carry a direction and an arrowhead, and
	 * the terminal cannot show the branch itself */
	{ "-|\\",   "solid",  "arrow",	false },
	{ "-|/",    "solid",  "arrow",	false },
	{ "-//",    "solid",  "arrow",	false },
	{ "-\\\\",   "solid",  "arrow",	false },
	{ "\\|-",   "solid",  "arrow",	false },
	{ "/|-",    "solid",  "arrow",	false },
	{ "//-",    "solid",  "arrow",	false },
	{ "\\\\-",   "solid",  "arrow",	false },
};

#define SEQ_ARROW_COUNT (sizeof(seq_arrows) / sizeof(seq_arrows[0]))

/* The statements that open a block, and the ones that continue one. */
static const char *const seq_blocks[] = {
	"loop", "alt", "opt", "par_over", "par", "critical", "break", "rect",
	"box",
};
static const char *const seq_block_alts[] = {
	"else", "and", "option",
};

struct seq {
	struct fymm_parser *p;
	struct fy_generic_builder *gb;
	fy_generic participants;
	fy_generic statements;
	int *active;		/* the activation depth of each participant */
	size_t nactive;
	int depth;
};

/* The position of @id in the participant list. */
static size_t seq_index_of(const struct seq *s, const char *id)
{
	size_t i, n = fy_len(s->participants);

	for (i = 0; i < n; i++) {
		if (!strcmp(fy_get(fy_get_at(s->participants, i), "id", ""), id))
			return i;
	}
	return (size_t)-1;
}

/* Record a participant the first time it is named, and return its id. */
static const char *seq_participant(struct seq *s, const char *name,
				   const char *label, bool actor, bool declared)
{
	fy_generic p;
	size_t i, n;

	n = fy_len(s->participants);
	for (i = 0; i < n; i++) {
		p = fy_get_at(s->participants, i);
		if (!strcmp(fy_get(p, "id", ""), name)) {
			/* a later explicit declaration supplies the label */
			if (declared)
				s->participants = fy_replace(s->gb,
					s->participants, i,
					fy_mapping(s->gb,
						"id", name,
						"label", label ? label : name,
						"actor", actor));
			return fy_get(fy_get_at(s->participants, i), "id", "");
		}
	}
	s->participants = fy_append(s->gb, s->participants,
		fy_mapping(s->gb,
			"id", name,
			"label", label ? label : name,
			"actor", actor));

	/* the activation depths run parallel to the participant list */
	{
		int *na = realloc(s->active, (n + 1) * sizeof(*s->active));

		if (na) {
			s->active = na;
			s->active[n] = 0;
			s->nactive = n + 1;
		}
	}
	return fy_get(fy_get_at(s->participants, fy_len(s->participants) - 1),
		      "id", "");
}

/*
 * Track how deep a participant's activation is. Deactivating one that is not
 * active is an error, as it is upstream: the bar has no start to close.
 */
static void seq_activation(struct seq *s, const char *id, bool on)
{
	size_t i = seq_index_of(s, id);

	if (i >= s->nactive)
		return;
	if (on) {
		s->active[i]++;
		return;
	}
	if (!s->active[i]) {
		fymm_diagf(s->p, true, s->p->lex.line, 1,
			   "'%s' is not active, so it cannot be deactivated",
			   id);
		return;
	}
	s->active[i]--;
}

static void seq_add(struct seq *s, fy_generic stmt)
{
	s->statements = fy_append(s->gb, s->statements, stmt);
}

/* The `}` that closes the flow mapping opened at @s, or NULL. */
static const char *seq_meta_end(const char *s, const char *e)
{
	bool in_quote = false;
	int depth = 0;

	for (; s < e; s++) {
		if (*s == '"' && (s[-1] != '\\'))
			in_quote = !in_quote;
		else if (in_quote)
			continue;
		else if (*s == '{')
			depth++;
		else if (*s == '}' && !--depth)
			return s;
	}
	return NULL;
}

/*
 * `participant A as Alice`, `actor Bob`, and either with `@{ ... }` metadata
 * between the name and the alias. The metadata is JSON, which is YAML, so the
 * generic parser validates it; malformed metadata is an error rather than
 * part of the name. An `alias` inside it names the participant, and an
 * external `as` wins over that.
 */
static void seq_stmt_participant(struct seq *s, const char *rest,
				 const char *e, bool actor)
{
	fy_generic_sized_string input;
	fy_generic meta = fy_invalid;
	const char *at, *close, *as, *name_end, *tail;
	char *name, *label = NULL;

	name_end = e;
	tail = rest;

	for (at = rest; at + 2 <= e; at++) {
		if (at[0] != '@' || at[1] != '{')
			continue;
		close = seq_meta_end(at + 1, e);
		if (!close) {
			fymm_diagf(s->p, true, s->p->lex.line, 1,
				   "unterminated metadata in a participant declaration");
			return;
		}
		input.data = at + 1;
		input.size = (size_t)(close + 1 - (at + 1));
		meta = fy_parse(s->gb, input,
				FYMM_YAML_PARSE_FLAGS | FYOPPF_INPUT_TYPE_STRING,
				NULL);
		if (!fy_is_mapping(meta)) {
			fymm_diagf(s->p, true, s->p->lex.line, 1,
				   "malformed metadata in a participant declaration");
			return;
		}
		name_end = at;
		tail = close + 1;
		break;
	}

	/* ` as ` splits the id from the label it is displayed with */
	for (as = tail; as + 4 <= e; as++) {
		if (!strncmp(as, " as ", 4))
			break;
	}
	if (as + 4 <= e) {
		const char *ls = as + 4;

		while (ls < e && *ls == ' ')
			ls++;
		label = strndup(ls, (size_t)(e - ls));
	} else if (fy_is_mapping(meta)) {
		const char *alias = fy_get(meta, "alias", (const char *)NULL);

		if (alias)
			label = strdup(alias);
	}
	if (name_end == e && as + 4 <= e)
		name_end = as;

	name = strndup(rest, (size_t)(name_end - rest));
	if (!name) {
		free(label);
		return;
	}
	while (*name && name[strlen(name) - 1] == ' ')
		name[strlen(name) - 1] = '\0';

	/* An id is an identifier, not markup. A declaration that runs a
	 * description into the name, `participant A B <br> C`, has no `as` to
	 * say where the name ends, and upstream refuses it too. */
	if (!label && strpbrk(name, "<>")) {
		fymm_diagf(s->p, true, s->p->lex.line, 1,
			   "'%s' is not a participant name; use 'as' to give a label",
			   name);
		free(name);
		return;
	}

	seq_participant(s, fy_gb_intern_string(s->gb, name),
			label ? fy_gb_intern_string(s->gb, label) : NULL,
			actor, true);
	free(name);
	free(label);
}

/* `Note right of Alice: text`, `Note over Alice,Bob: text` */
static void seq_stmt_note(struct seq *s, const char *rest, const char *e)
{
	const char *colon, *who, *comma;
	const char *placement = "over";
	fy_generic actors = fy_seq_empty;
	char *list;

	if (!strncasecmp(rest, "right of ", 9)) {
		placement = "right";
		rest += 9;
	} else if (!strncasecmp(rest, "left of ", 8)) {
		placement = "left";
		rest += 8;
	} else if (!strncasecmp(rest, "over ", 5)) {
		placement = "over";
		rest += 5;
	}

	colon = fymm_split_colon(rest, e);
	list = strndup(rest, (size_t)((colon ? colon : e) - rest));
	if (!list)
		return;

	for (who = list; who; who = comma ? comma + 1 : NULL) {
		char *w;

		comma = strchr(who, ',');
		w = strndup(who, comma ? (size_t)(comma - who) : strlen(who));
		if (!w)
			break;
		while (*w == ' ')
			memmove(w, w + 1, strlen(w));
		while (*w && w[strlen(w) - 1] == ' ')
			w[strlen(w) - 1] = '\0';
		if (*w)
			actors = fy_append(s->gb, actors,
				fy_value(s->gb,
					 seq_participant(s,
						fy_gb_intern_string(s->gb, w),
						NULL, false, false)));
		free(w);
	}
	free(list);

	seq_add(s, fy_mapping(s->gb,
		"kind", "note",
		"placement", placement,
		"actors", actors,
		"text", colon ? fymm_trim_text(s->gb, colon + 1, e) :
				fy_value(s->gb, ""),
		"depth", (long long)s->depth));
}

/*
 * A message is `<actor><arrow>[+|-]<actor>: <text>`. The arrow is found
 * before the colon, so a colon in the message text is safe.
 */
static bool seq_stmt_message(struct seq *s, const char *line, const char *e)
{
	const char *colon = fymm_split_colon(line, e);
	const char *limit = colon ? colon : e;
	const char *at = NULL, *from_end, *to;
	const char *from_id, *to_id;
	const char *activation = "none";
	size_t i, found = SEQ_ARROW_COUNT, alen = 0;
	char *from, *dst;

	/* the leftmost arrow wins; at one position the longest form does */
	for (const char *q = line; q < limit && !at; q++) {
		for (i = 0; i < SEQ_ARROW_COUNT; i++) {
			alen = strlen(seq_arrows[i].text);
			if ((size_t)(limit - q) >= alen &&
			    !memcmp(q, seq_arrows[i].text, alen)) {
				at = q;
				found = i;
				break;
			}
		}
	}
	if (!at)
		return false;

	from_end = at;
	to = at + strlen(seq_arrows[found].text);
	if (to < limit && (*to == '+' || *to == '-')) {
		activation = *to == '+' ? "activate" : "deactivate";
		to++;
	}

	from = strndup(line, (size_t)(from_end - line));
	dst = strndup(to, (size_t)(limit - to));
	if (!from || !dst) {
		free(from);
		free(dst);
		return true;
	}
	while (*from && from[strlen(from) - 1] == ' ')
		from[strlen(from) - 1] = '\0';
	while (*dst == ' ')
		memmove(dst, dst + 1, strlen(dst));
	while (*dst && dst[strlen(dst) - 1] == ' ')
		dst[strlen(dst) - 1] = '\0';

	from_id = seq_participant(s, fy_gb_intern_string(s->gb, from), NULL,
				  false, false);
	to_id = seq_participant(s, fy_gb_intern_string(s->gb, dst), NULL,
				false, false);

	seq_add(s, fy_mapping(s->gb,
		"kind", "message",
		"from", from_id,
		"to", to_id,
		"line", seq_arrows[found].line,
		"head", seq_arrows[found].head,
		"bidirectional", seq_arrows[found].both,
		"activation", activation,
		"text", colon ? fymm_trim_text(s->gb, colon + 1, e) :
				fy_value(s->gb, ""),
		"depth", (long long)s->depth));
	/* `->>+` activates the participant the message reaches, and `->>-`
	 * closes the activation of the one it came from */
	if (!strcmp(activation, "activate"))
		seq_activation(s, to_id, true);
	else if (!strcmp(activation, "deactivate"))
		seq_activation(s, from_id, false);

	free(from);
	free(dst);
	return true;
}

int fymm_parse_sequence(struct fymm_parser *p, fy_generic config,
			fy_generic title, struct fymm_token *htoks, int hn)
{
	struct fy_generic_builder *gb = p->d->gb;
	struct fymm_token toks[FYMM_MAX_TOKENS];
	struct fymm_acc acc = { fy_null, fy_null };
	struct seq s;
	const char *line, *e, *rest;
	size_t len, i;
	bool autonumber = false;
	int n;

	memset(&s, 0, sizeof(s));
	s.p = p;
	s.gb = gb;
	s.participants = fy_seq_empty;
	s.statements = fy_seq_empty;

	for (i = 1; i < (size_t)hn; i++) {
		if (htoks[i].type == FYMM_TOK_COLON)
			continue;
		fymm_diagf(p, false, htoks[i].line, htoks[i].col,
			   "ignoring unknown sequenceDiagram option '%.*s'",
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

		/* `title Text` and `title: Text` are both accepted */
		if (fymm_line_keyword(line, len, "title", &rest) ||
		    (len > 6 && !strncasecmp(line, "title:", 6) &&
		     (rest = line + 6) != NULL)) {
			if (fy_is_invalid(title))
				title = fymm_trim_text(gb, rest, e);
			continue;
		}
		/* `autonumber` may carry a start and a step, which the
		 * renderer does not use but which must not be an error */
		if (!strncasecmp(line, "autonumber", 10) &&
		    (len == 10 || line[10] == ' ' || line[10] == '\t')) {
			const char *q, *dot;

			/* the start and the step carry at most hundredths */
			for (q = line + 10; q < e; ) {
				while (q < e && (*q == ' ' || *q == '\t'))
					q++;
				for (dot = NULL; q < e && *q != ' ' &&
						 *q != '\t'; q++) {
					if (*q == '.')
						dot = q;
				}
				if (dot && q - dot > 3)
					fymm_diagf(p, true, p->lex.line, 1,
						   "an autonumber carries at most two decimal places");
			}
			autonumber = true;
			continue;
		}

		/* the declarations */
		if (fymm_line_keyword(line, len, "participant", &rest)) {
			seq_stmt_participant(&s, rest, e, false);
			continue;
		}
		if (fymm_line_keyword(line, len, "actor", &rest)) {
			seq_stmt_participant(&s, rest, e, true);
			continue;
		}
		if (fymm_line_keyword(line, len, "create", &rest) ||
		    fymm_line_keyword(line, len, "destroy", &rest)) {
			const char *sub;

			if (fymm_line_keyword(rest, (size_t)(e - rest),
					      "participant", &sub))
				seq_stmt_participant(&s, sub, e, false);
			else if (fymm_line_keyword(rest, (size_t)(e - rest),
						   "actor", &sub))
				seq_stmt_participant(&s, sub, e, true);
			else
				seq_participant(&s,
					fy_gb_intern_string_size(gb, rest,
						(size_t)(e - rest)),
					NULL, false, false);
			continue;
		}

		/* the metadata statements are recorded and not drawn */
		if (fymm_line_keyword(line, len, "links", &rest) ||
		    fymm_line_keyword(line, len, "link", &rest) ||
		    fymm_line_keyword(line, len, "properties", &rest))
			continue;

		if (fymm_line_keyword(line, len, "activate", &rest) ||
		    fymm_line_keyword(line, len, "deactivate", &rest)) {
			bool on = !strncasecmp(line, "activate", 8);
			const char *who;

			who = seq_participant(&s,
					fy_gb_intern_string_size(gb, rest,
						(size_t)(e - rest)),
					NULL, false, false);
			seq_activation(&s, who, on);
			seq_add(&s, fy_mapping(gb,
				"kind", on ? "activate" : "deactivate",
				"actor", who,
				"depth", (long long)s.depth));
			continue;
		}

		if (!strncasecmp(line, "note", 4) &&
		    (len == 4 || line[4] == ' ')) {
			rest = line + 4;
			while (rest < e && *rest == ' ')
				rest++;
			seq_stmt_note(&s, rest, e);
			continue;
		}

		/* the block statements */
		if (len == 3 && !strncasecmp(line, "end", 3)) {
			if (!s.depth) {
				fymm_diagf(p, true, p->lex.line, 1,
					   "'end' with no block open");
				continue;
			}
			s.depth--;
			seq_add(&s, fy_mapping(gb, "kind", "block-end",
					       "depth", (long long)s.depth));
			continue;
		}
		for (i = 0; i < sizeof(seq_blocks) / sizeof(seq_blocks[0]); i++) {
			if (!fymm_line_keyword(line, len, seq_blocks[i], &rest) &&
			    strncasecmp(line, seq_blocks[i], len))
				continue;
			if (!fymm_line_keyword(line, len, seq_blocks[i], &rest))
				rest = e;
			seq_add(&s, fy_mapping(gb,
				"kind", "block-start",
				"block", seq_blocks[i],
				"text", fymm_trim_text(gb, rest, e),
				"depth", (long long)s.depth));
			s.depth++;
			break;
		}
		if (i < sizeof(seq_blocks) / sizeof(seq_blocks[0]))
			continue;

		for (i = 0; i < sizeof(seq_block_alts) / sizeof(seq_block_alts[0]); i++) {
			if (!fymm_line_keyword(line, len, seq_block_alts[i], &rest) &&
			    strncasecmp(line, seq_block_alts[i], len))
				continue;
			if (!fymm_line_keyword(line, len, seq_block_alts[i], &rest))
				rest = e;
			seq_add(&s, fy_mapping(gb,
				"kind", "block-alt",
				"block", seq_block_alts[i],
				"text", fymm_trim_text(gb, rest, e),
				"depth", (long long)(s.depth ? s.depth - 1 : 0)));
			break;
		}
		if (i < sizeof(seq_block_alts) / sizeof(seq_block_alts[0]))
			continue;

		if (seq_stmt_message(&s, line, e))
			continue;

		fymm_diagf(p, true, p->lex.line, 1,
			   "unknown sequenceDiagram statement '%.*s'",
			   (int)len, line);
	}

	if (s.depth)
		fymm_diagf(p, true, p->lex.line, 1,
			   "%d block%s left open at the end of the diagram",
			   s.depth, s.depth == 1 ? "" : "s");

	free(s.active);

	if (fy_is_invalid(title))
		title = acc.title;
	if (fy_is_invalid(fy_get(config, "autonumber")))
		config = fy_assoc(gb, config, "autonumber", autonumber);

	p->d->model = fy_mapping(gb,
		"type", "sequenceDiagram",
		"title", title,
		"accTitle", acc.title,
		"accDescr", acc.descr,
		"config", config,
		"participants", s.participants,
		"statements", s.statements);
	return 0;
}
