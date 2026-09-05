/*
 * fymm-canvas.c - the character cell canvas the renderers draw on
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

#include "fymm-canvas.h"

/*
 * The box drawing glyph for each combination of the four directions.  Line
 * cells are accumulated as a direction mask rather than as a glyph, so a
 * horizontal run crossing a vertical one resolves to a cross without either
 * of the two draw calls having to know about the other.
 */
static const uint32_t fymm_box_uni[16] = {
	[0] = ' ',
	[FYMM_LN_N] = 0x2502,					/* U+2502 vertical */
	[FYMM_LN_S] = 0x2502,
	[FYMM_LN_N | FYMM_LN_S] = 0x2502,
	[FYMM_LN_E] = 0x2500,					/* U+2500 horizontal */
	[FYMM_LN_N | FYMM_LN_E] = 0x2570,			/* U+2570 arc up right */
	[FYMM_LN_S | FYMM_LN_E] = 0x256d,			/* U+256D arc down right */
	[FYMM_LN_N | FYMM_LN_S | FYMM_LN_E] = 0x251c,		/* U+251C tee right */
	[FYMM_LN_W] = 0x2500,
	[FYMM_LN_N | FYMM_LN_W] = 0x256f,			/* U+256F arc up left */
	[FYMM_LN_S | FYMM_LN_W] = 0x256e,			/* U+256E arc down left */
	[FYMM_LN_N | FYMM_LN_S | FYMM_LN_W] = 0x2524,		/* U+2524 tee left */
	[FYMM_LN_E | FYMM_LN_W] = 0x2500,
	[FYMM_LN_N | FYMM_LN_E | FYMM_LN_W] = 0x2534,		/* U+2534 tee up */
	[FYMM_LN_S | FYMM_LN_E | FYMM_LN_W] = 0x252c,		/* U+252C tee down */
	[FYMM_LN_N | FYMM_LN_S | FYMM_LN_E | FYMM_LN_W] = 0x253c,/* U+253C cross */
};

static uint32_t fymm_box_glyph(enum fymm_charset cs, uint8_t mask, bool dashed)
{
	bool horiz = !(mask & (FYMM_LN_N | FYMM_LN_S));
	bool vert = !(mask & (FYMM_LN_E | FYMM_LN_W));

	if (!mask)
		return ' ';

	if (cs == FYMM_CHARSET_ASCII) {
		if (vert)
			return dashed ? ':' : '|';
		if (horiz)
			return dashed ? '.' : '-';
		return '+';
	}

	if (dashed && vert)
		return 0x254e;		/* U+254E dashed vertical */
	if (dashed && horiz)
		return 0x254c;		/* U+254C dashed horizontal */
	return fymm_box_uni[mask & 0x0f];
}

struct fymm_canvas *fymm_canvas_create(int w, int h, enum fymm_charset charset,
				       enum fymm_color_mode color)
{
	struct fymm_canvas *cv;
	int i;

	if (w <= 0 || h <= 0)
		return NULL;

	cv = malloc(sizeof(*cv));
	if (!cv)
		return NULL;
	memset(cv, 0, sizeof(*cv));
	cv->w = w;
	cv->h = h;
	cv->charset = charset;
	cv->color = color;
	cv->cells = malloc((size_t)w * (size_t)h * sizeof(*cv->cells));
	if (!cv->cells) {
		free(cv);
		return NULL;
	}
	memset(cv->cells, 0, (size_t)w * (size_t)h * sizeof(*cv->cells));
	for (i = 0; i < w * h; i++)
		cv->cells[i].color = FYMM_COLOR_DEFAULT;
	return cv;
}

void fymm_canvas_destroy(struct fymm_canvas *cv)
{
	if (!cv)
		return;
	free(cv->cells);
	free(cv);
}

static struct fymm_cell *fymm_canvas_at(struct fymm_canvas *cv, int x, int y)
{
	if (!cv || x < 0 || y < 0 || x >= cv->w || y >= cv->h)
		return NULL;
	return &cv->cells[(size_t)y * (size_t)cv->w + (size_t)x];
}

void fymm_canvas_put(struct fymm_canvas *cv, int x, int y, uint32_t cp,
		     int color, uint8_t attr)
{
	struct fymm_cell *c = fymm_canvas_at(cv, x, y);

	if (!c)
		return;
	c->cp = cp;
	c->lines = 0;
	c->dashed = false;
	c->color = (int8_t)color;
	c->attr = attr;
}

/*
 * Add @mask to the line cell at (@x, @y). A cell that already holds a literal
 * glyph, a commit or a label, keeps it; a line stops at a node rather than
 * paints over it.
 *
 * A solid line wins over a dashed one in a shared cell. A merge edge and a
 * cherry-pick edge run along the same lane, and drawing the merge dashed
 * would report a relationship that is not there.
 */
void fymm_canvas_line(struct fymm_canvas *cv, int x, int y, uint8_t mask,
		      int color, bool dashed)
{
	struct fymm_cell *c = fymm_canvas_at(cv, x, y);

	if (!c || c->cp)
		return;
	if (!dashed)
		c->dashed = false;
	else if (!c->lines)
		c->dashed = true;
	c->lines |= mask;
	if (c->color == FYMM_COLOR_DEFAULT)
		c->color = (int8_t)color;
}

void fymm_canvas_hline(struct fymm_canvas *cv, int y, int x0, int x1,
		       int color, bool dashed)
{
	int x;

	for (x = x0; x <= x1; x++)
		fymm_canvas_line(cv, x, y,
				 (uint8_t)((x > x0 ? FYMM_LN_W : 0) |
					   (x < x1 ? FYMM_LN_E : 0)),
				 color, dashed);
}

void fymm_canvas_vline(struct fymm_canvas *cv, int x, int y0, int y1,
		       int color, bool dashed)
{
	int y;

	for (y = y0; y <= y1; y++)
		fymm_canvas_line(cv, x, y,
				 (uint8_t)((y > y0 ? FYMM_LN_N : 0) |
					   (y < y1 ? FYMM_LN_S : 0)),
				 color, dashed);
}

/* Decode one UTF-8 sequence, returning its length; invalid bytes decode as
 * themselves so that a mislabelled input still renders something. */
static size_t fymm_utf8_decode(const char *s, uint32_t *cpp)
{
	const unsigned char *u = (const unsigned char *)s;
	uint32_t cp;
	size_t i, n;

	if (u[0] < 0x80) {
		*cpp = u[0];
		return 1;
	}
	if ((u[0] & 0xe0) == 0xc0) {
		n = 2;
		cp = u[0] & 0x1f;
	} else if ((u[0] & 0xf0) == 0xe0) {
		n = 3;
		cp = u[0] & 0x0f;
	} else if ((u[0] & 0xf8) == 0xf0) {
		n = 4;
		cp = u[0] & 0x07;
	} else {
		*cpp = u[0];
		return 1;
	}
	for (i = 1; i < n; i++) {
		if ((u[i] & 0xc0) != 0x80) {
			*cpp = u[0];
			return 1;
		}
		cp = (cp << 6) | (u[i] & 0x3f);
	}
	*cpp = cp;
	return n;
}

static size_t fymm_utf8_encode(char *out, uint32_t cp)
{
	if (cp < 0x80) {
		out[0] = (char)cp;
		return 1;
	}
	if (cp < 0x800) {
		out[0] = (char)(0xc0 | (cp >> 6));
		out[1] = (char)(0x80 | (cp & 0x3f));
		return 2;
	}
	if (cp < 0x10000) {
		out[0] = (char)(0xe0 | (cp >> 12));
		out[1] = (char)(0x80 | ((cp >> 6) & 0x3f));
		out[2] = (char)(0x80 | (cp & 0x3f));
		return 3;
	}
	out[0] = (char)(0xf0 | (cp >> 18));
	out[1] = (char)(0x80 | ((cp >> 12) & 0x3f));
	out[2] = (char)(0x80 | ((cp >> 6) & 0x3f));
	out[3] = (char)(0x80 | (cp & 0x3f));
	return 4;
}

struct cp_range {
	uint32_t lo, hi;
};

/* Combining marks and other zero width codepoints, coarsely; labels in a
 * mermaid source are short identifiers, so the common cases are enough. */
static const struct cp_range fymm_zero_width[] = {
	{ 0x0300, 0x036f }, { 0x0483, 0x0489 }, { 0x0591, 0x05bd },
	{ 0x0610, 0x061a }, { 0x064b, 0x065f }, { 0x0670, 0x0670 },
	{ 0x06d6, 0x06dc }, { 0x0e31, 0x0e31 }, { 0x0e34, 0x0e3a },
	{ 0x1ab0, 0x1aff }, { 0x1dc0, 0x1dff }, { 0x200b, 0x200f },
	{ 0x20d0, 0x20f0 }, { 0xfe00, 0xfe0f }, { 0xfe20, 0xfe2f },
	{ 0x1f3fb, 0x1f3ff },
};

/* East Asian wide and fullwidth, plus the emoji blocks that present wide. */
static const struct cp_range fymm_double_width[] = {
	{ 0x1100, 0x115f }, { 0x2e80, 0x303e }, { 0x3041, 0x33ff },
	{ 0x3400, 0x4dbf }, { 0x4e00, 0x9fff }, { 0xa000, 0xa4cf },
	{ 0xac00, 0xd7a3 }, { 0xf900, 0xfaff }, { 0xfe30, 0xfe6f },
	{ 0xff00, 0xff60 }, { 0xffe0, 0xffe6 }, { 0x1f300, 0x1f64f },
	{ 0x1f900, 0x1f9ff }, { 0x20000, 0x3fffd },
};

static bool cp_in(const struct cp_range *r, size_t n, uint32_t cp)
{
	size_t lo = 0, hi = n;

	while (lo < hi) {
		size_t mid = (lo + hi) / 2;

		if (cp < r[mid].lo)
			hi = mid;
		else if (cp > r[mid].hi)
			lo = mid + 1;
		else
			return true;
	}
	return false;
}

static int fymm_cp_width(uint32_t cp)
{
	if (cp == 0 || cp == FYMM_CP_CONT)
		return 1;
	if (cp < 0x20 || (cp >= 0x7f && cp < 0xa0))
		return 0;
	if (cp_in(fymm_zero_width,
		  sizeof(fymm_zero_width) / sizeof(fymm_zero_width[0]), cp))
		return 0;
	if (cp_in(fymm_double_width,
		  sizeof(fymm_double_width) / sizeof(fymm_double_width[0]), cp))
		return 2;
	return 1;
}

int fymm_text_width(const char *s)
{
	uint32_t cp;
	int w = 0;

	if (!s)
		return 0;
	while (*s) {
		s += fymm_utf8_decode(s, &cp);
		w += fymm_cp_width(cp);
	}
	return w;
}

int fymm_canvas_text(struct fymm_canvas *cv, int x, int y, const char *s,
		     int color, uint8_t attr)
{
	uint32_t cp;
	int w, x0 = x;

	if (!s)
		return 0;
	while (*s) {
		s += fymm_utf8_decode(s, &cp);
		w = fymm_cp_width(cp);
		if (!w)
			continue;
		fymm_canvas_put(cv, x, y, cp, color, attr);
		if (w == 2)
			fymm_canvas_put(cv, x + 1, y, FYMM_CP_CONT, color,
					attr);
		x += w;
	}
	return x - x0;
}

/*
 * The palette.  Each entry names the same colour three ways so that the
 * output degrades cleanly: a bright ANSI code for a sixteen colour terminal,
 * an xterm cube index for a 256 colour one, and the RGB truecolor terminals
 * take.  The eight branch colours lead, matching mermaid's git0..git7.
 */
static const struct {
	int ansi16;
	int xterm256;
	unsigned int rgb;
} fymm_palette[FYMM_PAL_COUNT] = {
	[FYMM_PAL_BRANCH0 + 0] = { 94, 39, 0x3b8eea },	/* blue */
	[FYMM_PAL_BRANCH0 + 1] = { 92, 78, 0x23d18b },	/* green */
	[FYMM_PAL_BRANCH0 + 2] = { 95, 170, 0xd670d6 },	/* magenta */
	[FYMM_PAL_BRANCH0 + 3] = { 93, 220, 0xe5c07b },	/* yellow */
	[FYMM_PAL_BRANCH0 + 4] = { 96, 80, 0x29b8db },	/* cyan */
	[FYMM_PAL_BRANCH0 + 5] = { 91, 203, 0xf14c4c },	/* red */
	[FYMM_PAL_BRANCH0 + 6] = { 33, 214, 0xe5a00d },	/* orange */
	[FYMM_PAL_BRANCH0 + 7] = { 37, 109, 0x83a598 },	/* teal */
	[FYMM_PAL_LABEL] = { 37, 250, 0xbdc3c7 },	/* label grey */
	[FYMM_PAL_TAG] = { 93, 222, 0xffd479 },		/* tag amber */
	[FYMM_PAL_TITLE] = { 97, 255, 0xffffff },	/* title white */
};

struct fymm_buf {
	char *data;
	size_t size, alloc;
	bool oom;
};

static void fymm_buf_put(struct fymm_buf *b, const char *s, size_t len)
{
	size_t need = b->size + len + 1;
	char *n;

	if (b->oom)
		return;
	if (need > b->alloc) {
		size_t na = b->alloc ? b->alloc : 256;

		while (na < need)
			na *= 2;
		n = realloc(b->data, na);
		if (!n) {
			b->oom = true;
			return;
		}
		b->data = n;
		b->alloc = na;
	}
	memcpy(b->data + b->size, s, len);
	b->size += len;
	b->data[b->size] = '\0';
}

static void fymm_buf_puts(struct fymm_buf *b, const char *s)
{
	fymm_buf_put(b, s, strlen(s));
}

/* Emit the SGR sequence taking the terminal from @from to @to. */
static void fymm_emit_sgr(struct fymm_buf *b, enum fymm_color_mode mode,
			  int color, uint8_t attr)
{
	char seq[64];
	int n;

	if (mode == FYMM_COLOR_NONE)
		return;

	if (color == FYMM_COLOR_DEFAULT && !attr) {
		fymm_buf_puts(b, "\033[0m");
		return;
	}

	fymm_buf_puts(b, "\033[0");
	if (attr & FYMM_ATTR_BOLD)
		fymm_buf_puts(b, ";1");
	if (attr & FYMM_ATTR_DIM)
		fymm_buf_puts(b, ";2");
	if (color != FYMM_COLOR_DEFAULT && color < FYMM_PAL_COUNT) {
		unsigned int rgb = fymm_palette[color].rgb;

		switch (mode) {
		case FYMM_COLOR_TRUECOLOR:
			n = snprintf(seq, sizeof(seq), ";38;2;%u;%u;%u",
				     (rgb >> 16) & 0xff, (rgb >> 8) & 0xff,
				     rgb & 0xff);
			break;
		case FYMM_COLOR_256:
			n = snprintf(seq, sizeof(seq), ";38;5;%d",
				     fymm_palette[color].xterm256);
			break;
		default:
			n = snprintf(seq, sizeof(seq), ";%d",
				     fymm_palette[color].ansi16);
			break;
		}
		if (n > 0)
			fymm_buf_put(b, seq, (size_t)n);
	}
	fymm_buf_puts(b, "m");
}

char *fymm_canvas_emit(struct fymm_canvas *cv)
{
	struct fymm_buf b;
	struct fymm_cell *c;
	char utf[8];
	uint32_t cp;
	int x, y, last, first, stop, cur_color;
	uint8_t cur_attr;
	bool styled;

	if (!cv)
		return NULL;

	memset(&b, 0, sizeof(b));

	/* A lane band reserves a tag row and a label row whether or not the
	 * diagram uses them, so the grid routinely has blank rows top and
	 * bottom; they are noise in a pager and in a golden file. */
	first = cv->h;
	stop = 0;
	for (y = 0; y < cv->h; y++) {
		for (x = 0; x < cv->w; x++) {
			c = &cv->cells[(size_t)y * (size_t)cv->w + (size_t)x];
			if (!c->cp && !c->lines)
				continue;
			if (y < first)
				first = y;
			stop = y + 1;
			break;
		}
	}

	for (y = first; y < stop; y++) {
		/* trailing blanks are noise in a golden file and in a pager */
		last = -1;
		for (x = 0; x < cv->w; x++) {
			c = &cv->cells[(size_t)y * (size_t)cv->w + (size_t)x];
			if (c->cp || c->lines)
				last = x;
		}

		cur_color = FYMM_COLOR_DEFAULT;
		cur_attr = 0;
		styled = false;

		for (x = 0; x <= last; x++) {
			c = &cv->cells[(size_t)y * (size_t)cv->w + (size_t)x];
			if (c->cp == FYMM_CP_CONT)
				continue;

			cp = c->cp ? c->cp :
			     fymm_box_glyph(cv->charset, c->lines, c->dashed);

			if (cp == ' ') {
				/* no styling is worth spending on a blank */
				if (styled) {
					fymm_buf_puts(&b, "\033[0m");
					styled = false;
					cur_color = FYMM_COLOR_DEFAULT;
					cur_attr = 0;
				}
				fymm_buf_puts(&b, " ");
				continue;
			}

			if (c->color != cur_color || c->attr != cur_attr) {
				fymm_emit_sgr(&b, cv->color, c->color, c->attr);
				cur_color = c->color;
				cur_attr = c->attr;
				styled = cv->color != FYMM_COLOR_NONE &&
					 (cur_color != FYMM_COLOR_DEFAULT ||
					  cur_attr);
			}
			fymm_buf_put(&b, utf, fymm_utf8_encode(utf, cp));
		}
		if (styled)
			fymm_buf_puts(&b, "\033[0m");
		fymm_buf_puts(&b, "\n");
	}

	if (b.oom) {
		free(b.data);
		return NULL;
	}
	if (!b.data)
		b.data = strdup("");
	return b.data;
}
