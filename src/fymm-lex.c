/*
 * fymm-lex.c - the line oriented scanner over mermaid source
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

void fymm_lex_init(struct fymm_lex *l, const char *text, size_t len)
{
	memset(l, 0, sizeof(*l));
	l->start = text;
	l->end = text + len;
	l->p = text;
	l->ls = text;
	l->le = text;
	l->line = 0;
}

/* Truncate the current line at a `%%` comment.  `%%{` opens a directive and
 * is not a comment, and a `%%` inside a quoted string is literal text. */
static const char *fymm_strip_comment(const char *s, const char *e)
{
	bool in_quote = false;
	const char *p;

	for (p = s; p < e; p++) {
		if (*p == '"' && (p == s || p[-1] != '\\')) {
			in_quote = !in_quote;
			continue;
		}
		if (in_quote)
			continue;
		if (p[0] == '%' && p + 1 < e && p[1] == '%' &&
		    !(p + 2 < e && p[2] == '{'))
			return p;
	}
	return e;
}

bool fymm_lex_next_line(struct fymm_lex *l)
{
	const char *nl;

	if (l->p >= l->end)
		return false;

	nl = memchr(l->p, '\n', (size_t)(l->end - l->p));
	l->ls = l->p;
	l->le = nl ? nl : l->end;
	l->p = nl ? nl + 1 : l->end;
	l->line++;

	/* drop a trailing carriage return, then any comment */
	if (l->le > l->ls && l->le[-1] == '\r')
		l->le--;
	l->le = fymm_strip_comment(l->ls, l->le);

	/* the token cursor restarts at the head of the line */
	l->col = 1;
	return true;
}

void fymm_token_reset(struct fymm_token *t)
{
	if (t->buf)
		free(t->buf);
	memset(t, 0, sizeof(*t));
}

/* Copy [s, e) into a fresh NUL terminated buffer, resolving backslash
 * escapes.  Only the escapes mermaid itself documents are special; anything
 * else keeps the backslash, which is what a Windows path in a label wants. */
static char *fymm_unescape(const char *s, const char *e, size_t *lenp)
{
	char *out, *o;
	const char *p;

	out = malloc((size_t)(e - s) + 1);
	if (!out)
		return NULL;

	for (p = s, o = out; p < e; p++) {
		if (*p != '\\' || p + 1 >= e) {
			*o++ = *p;
			continue;
		}
		switch (p[1]) {
		case '"':
			*o++ = '"';
			p++;
			break;
		case '\\':
			*o++ = '\\';
			p++;
			break;
		case 'n':
			*o++ = '\n';
			p++;
			break;
		case 't':
			*o++ = '\t';
			p++;
			break;
		default:
			*o++ = *p;
			break;
		}
	}
	*o = '\0';
	*lenp = (size_t)(o - out);
	return out;
}

/* A bare word runs until whitespace, a colon, or a quote. */
static bool fymm_is_word_char(char c)
{
	return !isspace((unsigned char)c) && c != ':' && c != '"';
}

bool fymm_lex_token(struct fymm_lex *l, struct fymm_token *t)
{
	const char *p = l->ls + (l->col - 1);
	const char *s;

	fymm_token_reset(t);
	t->line = l->line;

	while (p < l->le && isspace((unsigned char)*p))
		p++;

	t->col = (int)(p - l->ls) + 1;
	if (p >= l->le) {
		l->col = (int)(p - l->ls) + 1;
		t->type = FYMM_TOK_EOL;
		t->text = "";
		return false;
	}

	if (*p == ':') {
		t->type = FYMM_TOK_COLON;
		t->text = ":";
		t->len = 1;
		p++;
	} else if (*p == '"') {
		s = ++p;
		while (p < l->le && (*p != '"' || p[-1] == '\\'))
			p++;
		t->type = FYMM_TOK_STRING;
		t->buf = fymm_unescape(s, p, &t->len);
		t->text = t->buf ? t->buf : "";
		if (p < l->le)
			p++;	/* the closing quote */
	} else {
		s = p;
		while (p < l->le && fymm_is_word_char(*p))
			p++;
		t->type = FYMM_TOK_WORD;
		t->text = s;
		t->len = (size_t)(p - s);
	}

	l->col = (int)(p - l->ls) + 1;
	return true;
}

/* Trim leading and trailing whitespace from [*sp, *ep). */
static void fymm_trim(const char **sp, const char **ep)
{
	const char *s = *sp, *e = *ep;

	while (s < e && isspace((unsigned char)*s))
		s++;
	while (e > s && isspace((unsigned char)e[-1]))
		e--;
	*sp = s;
	*ep = e;
}

/*
 * The rest of the current line from column @from_col, trimmed. The statements
 * that take free text, such as accTitle, read their value this way instead of
 * through the tokenizer. The column is explicit because a caller reads the
 * whole line into tokens before it knows which statement it has.
 */
const char *fymm_lex_rest(const struct fymm_lex *l, int from_col, size_t *lenp)
{
	const char *s = l->ls + (from_col - 1);
	const char *e = l->le;

	if (s > e)
		s = e;
	fymm_trim(&s, &e);
	*lenp = (size_t)(e - s);
	return s;
}

/* The whole of the current line, trimmed. */
const char *fymm_lex_line(const struct fymm_lex *l, size_t *lenp)
{
	const char *s = l->ls;
	const char *e = l->le;

	fymm_trim(&s, &e);
	*lenp = (size_t)(e - s);
	return s;
}

bool fymm_token_is(const struct fymm_token *t, const char *word)
{
	size_t len = strlen(word);

	return t->len == len && !memcmp(t->text, word, len);
}

bool fymm_token_ieq(const struct fymm_token *t, const char *word)
{
	size_t i, len = strlen(word);

	if (t->len != len)
		return false;
	for (i = 0; i < len; i++) {
		if (tolower((unsigned char)t->text[i]) !=
		    tolower((unsigned char)word[i]))
			return false;
	}
	return true;
}
