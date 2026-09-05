/*
 * fymm-markdown.c - label text broken into lines of attributed spans
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

#include <md4c.h>

#include "fymm-markdown.h"

/* struct fymm_span - a run of text that shares one set of attributes */
struct fymm_span {
	char *text;
	uint8_t attr;
};

struct fymm_rich_line {
	struct fymm_span *span;
	size_t nspans, aspans;
	int width;
};

struct fymm_rich {
	struct fymm_rich_line *line;
	size_t nlines, alines;
	int width;
};

/* struct md_ctx - what the md4c callbacks accumulate into */
struct md_ctx {
	struct fymm_rich_line *line;
	uint8_t attr;			/* the spans currently open */
	bool failed;
};

static struct fymm_rich_line *rich_add_line(struct fymm_rich *r)
{
	struct fymm_rich_line *nl;

	if (r->nlines == r->alines) {
		size_t na = r->alines ? r->alines * 2 : 4;

		nl = realloc(r->line, na * sizeof(*nl));
		if (!nl)
			return NULL;
		r->line = nl;
		r->alines = na;
	}
	nl = &r->line[r->nlines++];
	memset(nl, 0, sizeof(*nl));
	return nl;
}

/* Append text to the line, joining it to the last span when the attributes
 * match, so that a run is one span and not one per callback. */
static bool rich_add_text(struct fymm_rich_line *l, const char *text,
			  size_t len, uint8_t attr)
{
	struct fymm_span *sp;
	size_t have;
	char *nt;

	if (!len)
		return true;

	if (l->nspans && l->span[l->nspans - 1].attr == attr) {
		sp = &l->span[l->nspans - 1];
		have = strlen(sp->text);
		nt = realloc(sp->text, have + len + 1);
		if (!nt)
			return false;
		sp->text = nt;
		memcpy(sp->text + have, text, len);
		sp->text[have + len] = '\0';
		return true;
	}

	if (l->nspans == l->aspans) {
		size_t na = l->aspans ? l->aspans * 2 : 4;
		struct fymm_span *ns = realloc(l->span, na * sizeof(*ns));

		if (!ns)
			return false;
		l->span = ns;
		l->aspans = na;
	}
	sp = &l->span[l->nspans];
	sp->attr = attr;
	sp->text = malloc(len + 1);
	if (!sp->text)
		return false;
	memcpy(sp->text, text, len);
	sp->text[len] = '\0';
	l->nspans++;
	return true;
}

/* The attribute a markdown span carries in a terminal. */
static uint8_t md_span_attr(MD_SPANTYPE type)
{
	switch (type) {
	case MD_SPAN_STRONG:
		return FYMM_ATTR_BOLD;
	case MD_SPAN_EM:
		return FYMM_ATTR_ITALIC;
	case MD_SPAN_U:
	case MD_SPAN_A:
		return FYMM_ATTR_UNDERLINE;
	case MD_SPAN_DEL:
		return FYMM_ATTR_STRIKE;
	case MD_SPAN_CODE:
		return FYMM_ATTR_REVERSE;
	default:
		return 0;
	}
}

static int md_enter_span(MD_SPANTYPE type, void *detail, void *userdata)
{
	struct md_ctx *ctx = userdata;

	(void)detail;
	ctx->attr |= md_span_attr(type);
	return 0;
}

static int md_leave_span(MD_SPANTYPE type, void *detail, void *userdata)
{
	struct md_ctx *ctx = userdata;

	(void)detail;
	ctx->attr &= (uint8_t)~md_span_attr(type);
	return 0;
}

static int md_text(MD_TEXTTYPE type, const MD_CHAR *text, MD_SIZE size,
		   void *userdata)
{
	struct md_ctx *ctx = userdata;
	const char *s = text;

	switch (type) {
	case MD_TEXT_NULLCHAR:
		return 0;
	case MD_TEXT_BR:
	case MD_TEXT_SOFTBR:
		s = " ";
		size = 1;
		break;
	case MD_TEXT_ENTITY:
		/* the only entities a label is likely to carry */
		if (size == 6 && !memcmp(text, "&nbsp;", 6)) {
			s = " ";
			size = 1;
		} else if (size == 4 && !memcmp(text, "&lt;", 4)) {
			s = "<";
			size = 1;
		} else if (size == 4 && !memcmp(text, "&gt;", 4)) {
			s = ">";
			size = 1;
		} else if (size == 5 && !memcmp(text, "&amp;", 5)) {
			s = "&";
			size = 1;
		}
		break;
	default:
		break;
	}

	if (!rich_add_text(ctx->line, s, size, ctx->attr))
		ctx->failed = true;
	return 0;
}

static int md_block(MD_BLOCKTYPE type, void *detail, void *userdata)
{
	(void)type;
	(void)detail;
	(void)userdata;
	return 0;
}

/* Read one line as CommonMark inline content. */
static bool rich_parse_markdown(struct fymm_rich_line *l, const char *s,
				size_t len)
{
	static const MD_PARSER parser = {
		.abi_version = 0,
		/* strikethrough is not CommonMark, but `~~x~~` in a label
		 * plainly means it; underline is deliberately left off, so
		 * that `_x_` stays italic as a writer expects */
		.flags = MD_FLAG_NOHTML | MD_FLAG_STRIKETHROUGH,
		.enter_block = md_block,
		.leave_block = md_block,
		.enter_span = md_enter_span,
		.leave_span = md_leave_span,
		.text = md_text,
	};
	struct md_ctx ctx;

	memset(&ctx, 0, sizeof(ctx));
	ctx.line = l;
	if (md_parse(s, (MD_SIZE)len, &parser, &ctx))
		return false;
	return !ctx.failed;
}

/* The `<br>` spellings a mermaid label may carry, longest first. */
static size_t rich_br_at(const char *s, const char *e)
{
	static const char *const forms[] = {
		"<br />", "<br/>", "</br >", "</br>", "<br>",
	};
	size_t i, l;

	/* a tab or a run of spaces may sit inside the tag */
	for (i = 0; i < sizeof(forms) / sizeof(forms[0]); i++) {
		l = strlen(forms[i]);
		if ((size_t)(e - s) >= l && !strncasecmp(s, forms[i], l))
			return l;
	}
	/* the general shape: `<`, an optional `/`, `br`, anything but `>`,
	 * then `>` */
	if (*s == '<') {
		const char *q = s + 1;

		if (q < e && *q == '/')
			q++;
		if (e - q >= 2 && !strncasecmp(q, "br", 2)) {
			q += 2;
			while (q < e && *q != '>' && *q != '<')
				q++;
			if (q < e && *q == '>')
				return (size_t)(q + 1 - s);
		}
	}
	return 0;
}

struct fymm_rich *fymm_rich_parse(const char *text, bool markdown)
{
	struct fymm_rich *r;
	struct fymm_rich_line *l;
	const char *s, *e, *q;
	size_t br, i, j;

	r = calloc(1, sizeof(*r));
	if (!r)
		return NULL;
	if (!text)
		text = "";

	s = text;
	e = text + strlen(text);
	for (;;) {
		const char *stop = e;

		for (q = s; q < e; q++) {
			if (*q == '\n') {
				stop = q;
				br = 1;
				goto split;
			}
			br = rich_br_at(q, e);
			if (br) {
				stop = q;
				goto split;
			}
		}
		br = 0;
split:
		l = rich_add_line(r);
		if (!l)
			goto err_out;
		if (markdown) {
			if (!rich_parse_markdown(l, s, (size_t)(stop - s)))
				goto err_out;
		} else if (!rich_add_text(l, s, (size_t)(stop - s), 0)) {
			goto err_out;
		}
		if (!br)
			break;
		s = stop + br;
	}

	/* measure once, so a renderer can size a box without redoing it */
	for (i = 0; i < r->nlines; i++) {
		l = &r->line[i];
		for (j = 0; j < l->nspans; j++)
			l->width += fymm_text_width(l->span[j].text);
		if (l->width > r->width)
			r->width = l->width;
	}
	return r;

err_out:
	fymm_rich_destroy(r);
	return NULL;
}

void fymm_rich_destroy(struct fymm_rich *r)
{
	size_t i, j;

	if (!r)
		return;
	for (i = 0; i < r->nlines; i++) {
		for (j = 0; j < r->line[i].nspans; j++)
			free(r->line[i].span[j].text);
		free(r->line[i].span);
	}
	free(r->line);
	free(r);
}

size_t fymm_rich_lines(const struct fymm_rich *r)
{
	return r ? r->nlines : 0;
}

int fymm_rich_width(const struct fymm_rich *r)
{
	return r ? r->width : 0;
}

int fymm_rich_line_width(const struct fymm_rich *r, size_t line)
{
	if (!r || line >= r->nlines)
		return 0;
	return r->line[line].width;
}

int fymm_rich_inline_width(const struct fymm_rich *r)
{
	size_t i;
	int w = 0;

	if (!r)
		return 0;
	for (i = 0; i < r->nlines; i++)
		w += r->line[i].width + (i ? 1 : 0);
	return w;
}

int fymm_rich_draw_inline(struct fymm_canvas *cv, int x, int y,
			  const struct fymm_rich *r, int color, uint8_t attr)
{
	size_t i;
	int drawn = 0;

	if (!r)
		return 0;
	for (i = 0; i < r->nlines; i++) {
		if (i)
			drawn += fymm_canvas_text(cv, x + drawn, y, " ", color,
						  attr);
		drawn += fymm_rich_draw_line(cv, x + drawn, y, r, i, color,
					     attr);
	}
	return drawn;
}

int fymm_rich_text(struct fymm_canvas *cv, int x, int y, const char *text,
		   int color, uint8_t attr)
{
	struct fymm_rich *r = fymm_rich_parse(text, false);
	int drawn;

	if (!r)
		return fymm_canvas_text(cv, x, y, text, color, attr);
	drawn = fymm_rich_draw_inline(cv, x, y, r, color, attr);
	fymm_rich_destroy(r);
	return drawn;
}

int fymm_rich_measure(const char *text)
{
	struct fymm_rich *r = fymm_rich_parse(text, false);
	int w;

	if (!r)
		return fymm_text_width(text);
	w = fymm_rich_inline_width(r);
	fymm_rich_destroy(r);
	return w;
}

int fymm_rich_draw_line(struct fymm_canvas *cv, int x, int y,
			const struct fymm_rich *r, size_t line, int color,
			uint8_t attr)
{
	const struct fymm_rich_line *l;
	size_t j;
	int drawn = 0;

	if (!r || line >= r->nlines)
		return 0;
	l = &r->line[line];
	for (j = 0; j < l->nspans; j++)
		drawn += fymm_canvas_text(cv, x + drawn, y, l->span[j].text,
					  color,
					  (uint8_t)(attr | l->span[j].attr));
	return drawn;
}
