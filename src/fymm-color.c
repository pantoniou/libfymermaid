/*
 * fymm-color.c - colour parsing and reduction to the terminal palettes
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

#include "fymm-color.h"

/* The canonical xterm values of the sixteen ANSI colours, with the SGR
 * parameter that selects each as a foreground. */
static const struct {
	unsigned int rgb;
	int sgr;
	const char *name;
} fymm_ansi16[16] = {
	{ 0x000000, 30, "black" },
	{ 0xcd0000, 31, "red" },
	{ 0x00cd00, 32, "green" },
	{ 0xcdcd00, 33, "yellow" },
	{ 0x0000ee, 34, "blue" },
	{ 0xcd00cd, 35, "magenta" },
	{ 0x00cdcd, 36, "cyan" },
	{ 0xe5e5e5, 37, "white" },
	{ 0x7f7f7f, 90, "brightblack" },
	{ 0xff0000, 91, "brightred" },
	{ 0x00ff00, 92, "brightgreen" },
	{ 0xffff00, 93, "brightyellow" },
	{ 0x5c5cff, 94, "brightblue" },
	{ 0xff00ff, 95, "brightmagenta" },
	{ 0x00ffff, 96, "brightcyan" },
	{ 0xffffff, 97, "brightwhite" },
};

/* The six levels of each axis of the xterm colour cube. */
static const int fymm_cube_level[6] = { 0, 95, 135, 175, 215, 255 };

static int hexval(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

/* Convert an xterm palette index to its colour. */
static unsigned int fymm_xterm_to_rgb(int idx)
{
	int r, g, b;

	if (idx < 16)
		return fymm_ansi16[idx].rgb;
	if (idx < 232) {
		idx -= 16;
		r = fymm_cube_level[(idx / 36) % 6];
		g = fymm_cube_level[(idx / 6) % 6];
		b = fymm_cube_level[idx % 6];
		return ((unsigned int)r << 16) | ((unsigned int)g << 8) |
		       (unsigned int)b;
	}
	r = 8 + (idx - 232) * 10;
	return ((unsigned int)r << 16) | ((unsigned int)r << 8) |
	       (unsigned int)r;
}

unsigned int fymm_color_parse(const char *s)
{
	unsigned int v = 0;
	bool hex_only = false;
	size_t i, len;
	char *end;
	long idx;
	int d;

	if (!s)
		return FYMM_RGB_INVALID;
	while (isspace((unsigned char)*s))
		s++;
	if (*s == '#') {
		s++;
		hex_only = true;
	}

	len = strlen(s);
	while (len && isspace((unsigned char)s[len - 1]))
		len--;

	/* an ANSI colour by name */
	for (i = 0; i < 16; i++) {
		if (strlen(fymm_ansi16[i].name) == len &&
		    !strncasecmp(s, fymm_ansi16[i].name, len))
			return fymm_ansi16[i].rgb;
	}

	/*
	 * A bare number is ambiguous: `196` is both a palette index and a
	 * three digit hex colour. A `#` marks hex, so without one a run of at
	 * most three decimal digits is an index, and anything else is hex.
	 */
	if (!hex_only && len && len <= 3) {
		for (i = 0; i < len; i++) {
			if (!isdigit((unsigned char)s[i]))
				break;
		}
		if (i == len) {
			idx = strtol(s, &end, 10);
			if (end == s + len && idx >= 0 && idx <= 255)
				return fymm_xterm_to_rgb((int)idx);
			return FYMM_RGB_INVALID;
		}
	}

	if (len == 3 || len == 6) {
		for (i = 0; i < len; i++) {
			d = hexval(s[i]);
			if (d < 0)
				break;
			v = (v << 4) | (unsigned int)d;
		}
		if (i == len) {
			if (len != 3)
				return v;
			/* #rgb expands each digit, so #f0a is #ff00aa */
			return ((v & 0xf00) << 12) | ((v & 0xf00) << 8) |
			       ((v & 0x0f0) << 8) | ((v & 0x0f0) << 4) |
			       ((v & 0x00f) << 4) | (v & 0x00f);
		}
	}

	return FYMM_RGB_INVALID;
}

/*
 * The redmean colour distance. It weights the red and blue axes by how red
 * the pair already is, which tracks perception closely enough for choosing a
 * palette entry and needs no colour space conversion.
 */
static long fymm_distance(unsigned int a, unsigned int b)
{
	long ar = (long)((a >> 16) & 0xff), ag = (long)((a >> 8) & 0xff);
	long ab = (long)(a & 0xff);
	long br = (long)((b >> 16) & 0xff), bg = (long)((b >> 8) & 0xff);
	long bb = (long)(b & 0xff);
	long rmean = (ar + br) / 2;
	long dr = ar - br, dg = ag - bg, db = ab - bb;

	return (((512 + rmean) * dr * dr) >> 8) + 4 * dg * dg +
	       (((767 - rmean) * db * db) >> 8);
}

static int fymm_nearest_level(int v)
{
	int i, best = 0;
	long bd = -1, d;

	for (i = 0; i < 6; i++) {
		d = (long)(v - fymm_cube_level[i]);
		d = d * d;
		if (bd < 0 || d < bd) {
			bd = d;
			best = i;
		}
	}
	return best;
}

int fymm_rgb_to_xterm256(unsigned int rgb)
{
	int r = (int)((rgb >> 16) & 0xff);
	int g = (int)((rgb >> 8) & 0xff);
	int b = (int)(rgb & 0xff);
	int cube, grey, gi;
	long dcube, dgrey;

	cube = 16 + 36 * fymm_nearest_level(r) + 6 * fymm_nearest_level(g) +
	       fymm_nearest_level(b);
	dcube = fymm_distance(rgb, fymm_xterm_to_rgb(cube));

	/* the grey ramp often beats the cube for a desaturated colour */
	gi = ((r + g + b) / 3 - 8 + 5) / 10;
	if (gi < 0)
		gi = 0;
	if (gi > 23)
		gi = 23;
	grey = 232 + gi;
	dgrey = fymm_distance(rgb, fymm_xterm_to_rgb(grey));

	return dgrey < dcube ? grey : cube;
}

int fymm_rgb_to_ansi16(unsigned int rgb)
{
	long best = -1, d;
	size_t i, bi = 0;

	for (i = 0; i < 16; i++) {
		d = fymm_distance(rgb, fymm_ansi16[i].rgb);
		if (best < 0 || d < best) {
			best = d;
			bi = i;
		}
	}
	return fymm_ansi16[bi].sgr;
}
