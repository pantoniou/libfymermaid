/*
 * fymm-markdown.h - label text broken into lines of attributed spans
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

#ifndef FYMM_MARKDOWN_H
#define FYMM_MARKDOWN_H

#include <stdbool.h>
#include <stddef.h>

#include "fymm-canvas.h"

/* the opaque result of reading a label */
struct fymm_rich;

/*
 * Read a label into lines of attributed spans.
 *
 * A `<br>` in any of its spellings ends a line; mermaid accepts `<br>`,
 * `<br/>`, `<br />` and `</br>`, and a diagram in the wild carries all four.
 *
 * When @markdown is set the text of each line is read as CommonMark inline
 * content, so `**bold**` becomes a bold span rather than four literal
 * asterisks. Mermaid calls that a markdown string and writes it inside
 * backticks; a plain label is never formatted, because a label is allowed to
 * contain an asterisk.
 *
 * Returns NULL only when it cannot allocate.
 */
struct fymm_rich *fymm_rich_parse(const char *text, bool markdown);

void fymm_rich_destroy(struct fymm_rich *r);

/* how many lines the label occupies */
size_t fymm_rich_lines(const struct fymm_rich *r);

/* the display width of the widest line, in cells */
int fymm_rich_width(const struct fymm_rich *r);

/* the display width of one line, in cells */
int fymm_rich_line_width(const struct fymm_rich *r, size_t line);

/*
 * Draw one line at (@x, @y). @color and @attr are what the caller wants for
 * the text; the attributes a span carries are added to @attr, so a bold run
 * inside a dim label is both.
 *
 * Returns the cells drawn.
 */
int fymm_rich_draw_line(struct fymm_canvas *cv, int x, int y,
			const struct fymm_rich *r, size_t line, int color,
			uint8_t attr);

/*
 * Draw the whole label on one row, joining its lines with a space.
 *
 * Some diagrams give a label a box of its own, and a `<br>` there is a line
 * break. Others make the label one row of a list -- a timeline event, a gantt
 * task, an axis name -- where the row is the unit and there is nowhere for a
 * second line to go. Those use this.
 *
 * Returns the cells drawn.
 */
int fymm_rich_draw_inline(struct fymm_canvas *cv, int x, int y,
			  const struct fymm_rich *r, int color, uint8_t attr);

/* The width the label needs when it is drawn on one row. */
int fymm_rich_inline_width(const struct fymm_rich *r);

/*
 * Read @text and draw it on one row at (@x, @y), the common case for a label
 * that has no box. Returns the cells drawn.
 */
int fymm_rich_text(struct fymm_canvas *cv, int x, int y, const char *text,
		   int color, uint8_t attr);

/* The width @text needs when drawn on one row, with its markup resolved. */
int fymm_rich_measure(const char *text);

#endif /* FYMM_MARKDOWN_H */
