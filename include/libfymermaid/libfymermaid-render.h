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

/**
 * enum fymm_background - what the terminal is drawn on
 *
 * @FYMM_BG_AUTO: probe the environment and, if it can, ask the terminal
 * @FYMM_BG_DARK: light text on a dark ground
 * @FYMM_BG_LIGHT: dark text on a light ground
 */
enum fymm_background {
	FYMM_BG_AUTO = 0,
	FYMM_BG_DARK,
	FYMM_BG_LIGHT,
};

/**
 * enum fymm_fit - what to do with a diagram wider than the width
 *
 * @FYMM_FIT_SHRINK: close the gaps between the parts of the drawing until it
 *                   fits, and clip whatever is still over. This keeps the
 *                   whole diagram on the screen for as long as closing it up
 *                   can, and is the default.
 * @FYMM_FIT_LEGEND: close the gaps as FYMM_FIT_SHRINK does, and move a label
 *                   that still does not fit into a legend beneath the
 *                   drawing, leaving a number in its place. A narrow terminal
 *                   then loses the place of a label rather than the label
 *                   itself. Not every diagram type can do this; one that
 *                   cannot behaves as FYMM_FIT_SHRINK.
 * @FYMM_FIT_CLIP: draw at the natural size and clip to the width. The parts
 *                 that fit are drawn exactly as they would be with no limit.
 * @FYMM_FIT_NONE: ignore the width and emit the whole drawing, however wide.
 *                 For a pager that scrolls sideways, or a file.
 */
enum fymm_fit {
	FYMM_FIT_SHRINK = 0,
	FYMM_FIT_LEGEND,
	FYMM_FIT_CLIP,
	FYMM_FIT_NONE,
};

/* FYMM_WIDTH_AUTO - detect the terminal width */
#define FYMM_WIDTH_AUTO (-1)
/* FYMM_WIDTH_INF - never wrap or clip */
#define FYMM_WIDTH_INF (0)

/**
 * struct fymm_metrics - the spacing a diagram is drawn with
 *
 * @struct_size: sizeof(struct fymm_metrics), the forward compatibility guard
 * @margin: the cells left around the whole drawing
 * @col_gap: the cells left between two nodes of the same rank
 * @rank_gap: the cells left between two ranks
 * @plot_height: the rows a plotted chart gives its plot area
 * @max_width: the cells the drawing may occupy, or 0 to take it from
 *             fymm_render_cfg.width
 * @max_height: the rows it may occupy, or 0 for no limit
 *
 * A field left at zero takes the default for the diagram type, so setting one
 * thing needs nothing else:
 *
 *	struct fymm_metrics met = FYMM_METRICS_INIT;
 *
 *	met.margin = 2;
 *	rcfg.metrics = &met;
 *
 * Zero is the default of every field that has a useful zero -- no margin, no
 * width limit, no height limit -- so nothing is lost by spelling it that way.
 * Use fymm_metrics_default() to read the values a type is drawn with.
 *
 * A diagram type that has no use for a field ignores it: only a graph has
 * ranks, and only a plotted chart has a plot area.
 */
struct fymm_metrics {
	size_t struct_size;
	int margin;
	int col_gap;
	int rank_gap;
	int plot_height;
	int max_width;
	int max_height;
};

/* FYMM_METRICS_INIT - a metrics structure that asks for every default */
#define FYMM_METRICS_INIT \
	{ sizeof(struct fymm_metrics), 0, 0, 0, 0, 0, 0 }

/**
 * fymm_metrics_default() - fill @m with the spacing @type is drawn with
 * @m: the structure to fill
 * @type: the diagram type whose defaults are wanted, or FYMM_DT_UNKNOWN for
 *        the values a graph uses
 *
 * These are what a render uses when fymm_render_cfg.metrics is NULL.
 */
void
fymm_metrics_default(struct fymm_metrics *m, enum fymm_diagram_type type)
	FYMM_EXPORT;

/**
 * struct fymm_render_cfg - configuration for a render
 *
 * @struct_size: sizeof(struct fymm_render_cfg), the forward compatibility guard
 * @width: the output width in columns, or FYMM_WIDTH_AUTO / FYMM_WIDTH_INF
 * @fit: what to do when the diagram does not fit in @width
 * @metrics: the spacing to draw with, or NULL for the defaults of the
 *           diagram type
 * @color: how much colour to use
 * @charset: which glyphs to draw with
 * @options: a mapping of overrides applied over the diagram's own config,
 *           using the same keys mermaid's %%{init: ...}%% directive uses
 *           (`showBranches`, `showCommitLabel`, `mainBranchName`, ...)
 * @theme: the name of a built-in theme, or NULL for the default; see
 *         fymm_theme_iterate()
 * @theme_path: a theme file to read, applied over @theme; NULL for none
 * @background: what the terminal is drawn on. With FYMM_BG_AUTO it is
 *              probed, and a light terminal selects the `light` theme unless
 *              @theme already named one.
 *
 * A NULL cfg selects the defaults for every field.
 */
struct fymm_render_cfg {
	size_t struct_size;
	int width;
	enum fymm_fit fit;
	enum fymm_color_mode color;
	enum fymm_charset charset;
	fy_generic options;
	const char *theme;
	const char *theme_path;
	enum fymm_background background;
	const struct fymm_metrics *metrics;
};

/**
 * fymm_measure() - the cells a render of @d would occupy
 * @d: the diagram
 * @cfg: the configuration the render would use, or NULL for the defaults
 * @wp: where the width in columns is stored, or NULL
 * @hp: where the height in rows is stored, or NULL
 *
 * Answers what fymm_render() would produce, under the same configuration and
 * so under the same fit policy: with FYMM_FIT_NONE this is the natural size
 * of the diagram, and otherwise it is what the limits leave.
 *
 * The measure is taken by rendering, so it costs what a render costs. Call it
 * to size a pane or to decide whether a diagram is worth drawing, not in a
 * loop over a width.
 *
 * Returns: 0, or -1 when the diagram cannot be rendered.
 */
int
fymm_measure(const struct fymm_diagram *d, const struct fymm_render_cfg *cfg,
	     int *wp, int *hp)
	FYMM_EXPORT;

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

/**
 * fymm_detect_background() - what the terminal is drawn on
 *
 * Honours `$FYMM_BACKGROUND` (`light` or `dark`), then `$COLORFGBG`, and then
 * asks the terminal itself with an OSC 11 query when @fd is one. The query
 * goes to `/dev/tty`, not to @fd, so a redirected render never has escape
 * bytes in it, and it is polled with a short timeout so that a terminal that
 * does not answer cannot hang the caller.
 *
 * Returns FYMM_BG_DARK when nothing says otherwise, which is what a terminal
 * usually is.
 */
enum fymm_background
fymm_detect_background(int fd)
	FYMM_EXPORT;

/* fymm_detect_charset() - whether the locale can carry the box drawing glyphs */
enum fymm_charset
fymm_detect_charset(void)
	FYMM_EXPORT;

#ifdef __cplusplus
}
#endif

#endif /* LIBFYMERMAID_RENDER_H */
