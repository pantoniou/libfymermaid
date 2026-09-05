/*
 * libfymermaid-render.h - rendering a diagram model to the terminal
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

#ifndef LIBFYMERMAID_RENDER_H
#define LIBFYMERMAID_RENDER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include <libfyaml.h>
#include <libfyaml/libfyaml-generic.h>

#include <libfymermaid/libfymermaid-diagram.h>
#include <libfymermaid/libfymermaid-util.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * enum fymm_color_mode - how much colour the output may use
 *
 * @FYMM_COLOR_AUTO: probe the environment and the output descriptor
 * @FYMM_COLOR_NONE: emit no escape sequences at all
 * @FYMM_COLOR_16: the eight ANSI colours plus their bright variants
 * @FYMM_COLOR_256: the xterm 256 colour cube
 * @FYMM_COLOR_TRUECOLOR: 24 bit direct colour
 */
enum fymm_color_mode {
	FYMM_COLOR_AUTO = 0,
	FYMM_COLOR_NONE,
	FYMM_COLOR_16,
	FYMM_COLOR_256,
	FYMM_COLOR_TRUECOLOR,
};

/**
 * enum fymm_charset - which glyphs the renderer may draw with
 *
 * @FYMM_CHARSET_AUTO: probe the locale, falling back to ASCII
 * @FYMM_CHARSET_ASCII: seven bit ASCII only
 * @FYMM_CHARSET_UNICODE: box drawing and geometric shapes
 */
enum fymm_charset {
	FYMM_CHARSET_AUTO = 0,
	FYMM_CHARSET_ASCII,
	FYMM_CHARSET_UNICODE,
};

/* FYMM_WIDTH_AUTO - detect the terminal width */
#define FYMM_WIDTH_AUTO (-1)
/* FYMM_WIDTH_INF - never wrap or clip */
#define FYMM_WIDTH_INF (0)

/**
 * struct fymm_render_cfg - configuration for a render
 *
 * @struct_size: sizeof(struct fymm_render_cfg), the forward compatibility guard
 * @width: the output width in columns, or FYMM_WIDTH_AUTO / FYMM_WIDTH_INF
 * @color: how much colour to use
 * @charset: which glyphs to draw with
 * @options: a mapping of overrides applied over the diagram's own config,
 *           using the same keys mermaid's %%{init: ...}%% directive uses
 *           (`showBranches`, `showCommitLabel`, `mainBranchName`, ...)
 * @theme: the name of a built-in theme, or NULL for the default; see
 *         fymm_theme_iterate()
 * @theme_path: a theme file to read, applied over @theme; NULL for none
 *
 * A NULL cfg selects the defaults for every field.
 */
struct fymm_render_cfg {
	size_t struct_size;
	int width;
	enum fymm_color_mode color;
	enum fymm_charset charset;
	fy_generic options;
	const char *theme;
	const char *theme_path;
};

/**
 * struct fymm_theme_info - a built-in theme
 *
 * @name: the name to pass as fymm_render_cfg.theme
 * @description: a one line summary, for a usage message
 */
struct fymm_theme_info {
	const char *name;
	const char *description;
};

/**
 * fymm_theme_iterate() - walk the built-in themes
 *
 * Start with a NULL @prevp and call until it returns NULL::
 *
 *     void *iter = NULL;
 *     const struct fymm_theme_info *ti;
 *
 *     while ((ti = fymm_theme_iterate(&iter)) != NULL)
 *             printf("%s - %s\n", ti->name, ti->description);
 */
const struct fymm_theme_info *
fymm_theme_iterate(void **prevp)
	FYMM_EXPORT;

/* fymm_render_cfg_default() - fill @cfg in with the defaults */
void
fymm_render_cfg_default(struct fymm_render_cfg *cfg)
	FYMM_EXPORT;

/**
 * fymm_render() - render a diagram to a string
 *
 * @d: the diagram, which must not have errors
 * @cfg: the render configuration, or NULL for the defaults
 *
 * Returns:
 * The rendered text, to release with fymm_free(), or NULL on error.
 */
char *
fymm_render(const struct fymm_diagram *d, const struct fymm_render_cfg *cfg)
	FYMM_EXPORT;

/**
 * fymm_render_fp() - render a diagram straight to a stream
 *
 * With FYMM_COLOR_AUTO and FYMM_WIDTH_AUTO the probing is done against
 * @fp rather than against standard output.
 *
 * Returns:
 * 0 on success, -1 on error.
 */
int
fymm_render_fp(const struct fymm_diagram *d, const struct fymm_render_cfg *cfg,
	       FILE *fp)
	FYMM_EXPORT;

/**
 * fymm_detect_width() - the terminal width in columns
 *
 * Honours $COLUMNS, then TIOCGWINSZ on @fd, and settles on 80.
 */
int
fymm_detect_width(int fd)
	FYMM_EXPORT;

/**
 * fymm_detect_color_mode() - how much colour @fd can take
 *
 * Honours $NO_COLOR, $CLICOLOR_FORCE, $COLORTERM and $TERM, and returns
 * FYMM_COLOR_NONE when @fd is not a terminal.
 */
enum fymm_color_mode
fymm_detect_color_mode(int fd)
	FYMM_EXPORT;

/* fymm_detect_charset() - whether the locale can carry the box drawing glyphs */
enum fymm_charset
fymm_detect_charset(void)
	FYMM_EXPORT;

#ifdef __cplusplus
}
#endif

#endif /* LIBFYMERMAID_RENDER_H */
