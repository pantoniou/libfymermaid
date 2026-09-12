/*
 * fymm-palette-test.c - fymm_render_cfg.palette colours a diagram through the
 * mermaid.* roles of a libfypalette context
 *
 * Copyright (c) 2026 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libfymermaid.h>
#include <libfypalette.h>

/* Every role has a colour of its own, so the output says which role coloured
 * a cell. */
static const char theme[] =
	"colors:\n"
	"  main: '#0a0a0a'\n"
	"  side: '#0b0b0b'\n"
	"  head: '#0c0c0c'\n"
	"  tag: '#0d0d0d'\n"
	"roles:\n"
	"  mermaid:\n"
	"    title: {fg: head, attrs: [bold]}\n"
	"    tag: {fg: tag}\n"
	"    series:\n"
	"      0: {fg: main}\n"
	"      1: {fg: side}\n";

static const char source[] =
	"---\n"
	"title: Release\n"
	"---\n"
	"gitGraph\n"
	"   commit\n"
	"   branch feature\n"
	"   commit\n"
	"   checkout main\n"
	"   merge feature tag: \"v1.0\"\n";

static int failures;

/* Report a render that lacks want or holds forbid, escapes made visible. */
static void expect(int line, const char *out, const char *want, const char *forbid)
{
	const char *p;

	if (out && (!want || strstr(out, want)) && (!forbid || !strstr(out, forbid)))
		return;
	fprintf(stderr, "%s:%d: render lacks \"%s\" or holds \"%s\":\n", __FILE__,
		line, want ? want : "", forbid ? forbid : "");
	for (p = out ? out : ""; *p; p++) {
		if (*p == '\033')
			fputs("\\e", stderr);
		else
			fputc(*p, stderr);
	}
	fputc('\n', stderr);
	failures++;
}

static char *render(const struct fymm_diagram *d, struct fypal_ctx *palette,
		    size_t struct_size)
{
	struct fymm_render_cfg cfg;

	fymm_render_cfg_default(&cfg);
	cfg.struct_size = struct_size;
	cfg.width = 80;
	cfg.color = FYMM_COLOR_TRUECOLOR;
	cfg.charset = FYMM_CHARSET_UNICODE;
	cfg.background = FYMM_BG_DARK;
	cfg.palette = palette;
	return fymm_render(d, &cfg);
}

int main(void)
{
	struct fypal_caps caps = {
		.depth = FYPAL_DEPTH_TRUECOLOR,
		.attrs = FYPAL_ATTR_ALL,
		.underline_color = true,
	};
	struct fymm_diagram *d;
	struct fypal_ctx *palette;
	char *out;

	palette = fypal_ctx_create(&caps);
	if (!palette || fypal_ctx_load(palette, theme, "test")) {
		fprintf(stderr, "theme: %s\n",
			palette ? fypal_ctx_error(palette) : "no context");
		return 1;
	}
	d = fymm_parse(source, strlen(source), NULL);
	if (!d || fymm_diagram_has_errors(d)) {
		fprintf(stderr, "the diagram does not parse\n");
		return 1;
	}

	/* without a palette the built-in colours draw the diagram */
	out = render(d, NULL, sizeof(struct fymm_render_cfg));
	expect(__LINE__, out, "Release", "38;2;10;10;10");
	fymm_free(out);

	/* the palette colours the series, the title and the tag */
	out = render(d, palette, sizeof(struct fymm_render_cfg));
	expect(__LINE__, out, "38;2;10;10;10", NULL);
	expect(__LINE__, out, "38;2;11;11;11", NULL);
	expect(__LINE__, out, "1;38;2;12;12;12mRelease", NULL);
	expect(__LINE__, out, "38;2;13;13;13", NULL);
	fymm_free(out);

	/* a caller built before the field existed is not read past its end */
	out = render(d, palette, offsetof(struct fymm_render_cfg, palette));
	expect(__LINE__, out, "Release", "38;2;10;10;10");
	fymm_free(out);

	/* the palette follows its capabilities */
	caps.depth = FYPAL_DEPTH_256;
	fypal_ctx_set_caps(palette, &caps);
	out = render(d, palette, sizeof(struct fymm_render_cfg));
	expect(__LINE__, out, "38;2;10;10;10", NULL);
	fymm_free(out);

	fymm_diagram_destroy(d);
	fypal_ctx_destroy(palette);
	return failures ? 1 : 0;
}
