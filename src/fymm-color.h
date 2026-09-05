/*
 * fymm-color.h - colour parsing and reduction to the terminal palettes
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

#ifndef FYMM_COLOR_H
#define FYMM_COLOR_H

#include <stdbool.h>

/* FYMM_RGB_INVALID - the result of a colour that did not parse */
#define FYMM_RGB_INVALID 0xffffffffu

/*
 * Parse a colour into 0x00RRGGBB.
 *
 * Accepts `#rgb` and `#rrggbb`, an xterm palette index from 0 to 255, and the
 * sixteen ANSI colour names (`red`, `brightblue`). The `#` may be left off a
 * six digit hex colour; it may not be left off a three digit one, because
 * `196` is then a palette index. Returns FYMM_RGB_INVALID for anything else.
 */
unsigned int fymm_color_parse(const char *s);

/*
 * Reduce a colour to the nearest entry of a terminal palette.
 *
 * The distance is the redmean approximation, which stays close to a
 * perceptual metric without a colour space conversion.
 */
int fymm_rgb_to_xterm256(unsigned int rgb);

/* Reduce a colour to an SGR parameter for one of the sixteen ANSI colours. */
int fymm_rgb_to_ansi16(unsigned int rgb);

#endif /* FYMM_COLOR_H */
