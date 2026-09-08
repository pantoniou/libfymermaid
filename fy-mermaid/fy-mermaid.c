/*
 * fy-mermaid.c - render mermaid diagrams on the terminal
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

#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <libfymermaid.h>

#include "fymm-viewer.h"

static const char *progname = "fy-mermaid";

static const struct option lopts[] = {
	{ "output",	required_argument,	NULL,	'o' },
	{ "width",	required_argument,	NULL,	'w' },
	{ "fit",	required_argument,	NULL,	'F' },
	{ "color",	required_argument,	NULL,	'c' },
	{ "charset",	required_argument,	NULL,	'C' },
	{ "ascii",	no_argument,		NULL,	'a' },
	{ "theme",	required_argument,	NULL,	't' },
	{ "style",	required_argument,	NULL,	'S' },
	{ "list-themes",no_argument,		NULL,	'L' },
	{ "background",	required_argument,	NULL,	'b' },
	{ "interactive",no_argument,		NULL,	'i' },
	{ "dump-model",	no_argument,		NULL,	'm' },
	{ "flow",	no_argument,		NULL,	'f' },
	{ "strict",	no_argument,		NULL,	's' },
	{ "quiet",	no_argument,		NULL,	'q' },
	{ "version",	no_argument,		NULL,	'V' },
	{ "help",	no_argument,		NULL,	'h' },
	{ NULL,		0,			NULL,	0 },
};

static void usage(FILE *fp)
{
	fprintf(fp,
"usage: %s [options] [file ...]\n"
"\n"
"Render mermaid diagrams on the terminal.  With no file, or with `-',\n"
"the diagram is read from standard input.\n"
"\n"
"options:\n"
"  -o, --output FILE   write to FILE instead of standard output\n"
"  -w, --width N       render for a terminal N columns wide\n"
"  -F, --fit POLICY    shrink (default), legend, clip or none\n"
"  -c, --color MODE    auto (default), none, 16, 256 or true\n"
"  -C, --charset SET   auto (default), ascii, unicode or rich\n"
"  -a, --ascii         shorthand for --charset ascii\n"
"  -t, --theme NAME    colour theme; --list-themes names them\n"
"  -S, --style FILE    a theme file, applied over --theme\n"
"  -L, --list-themes   list the built-in themes and exit\n"
"  -b, --background M  auto (default), dark or light; a light terminal\n"
"                      takes the light theme unless --theme names one\n"
"  -i, --interactive   move a selection over the diagram with the arrow keys\n"
"                      and the mouse; enter reports the selected element\n"
"  -m, --dump-model    emit the parsed model as YAML instead of rendering\n"
"  -f, --flow          with --dump-model, emit flow style rather than block\n"
"  -s, --strict        treat warnings as errors\n"
"  -q, --quiet         do not report diagnostics\n"
"  -V, --version       print the library version and exit\n"
"  -h, --help          print this message and exit\n",
		progname);
}

static void list_themes(FILE *fp)
{
	const struct fymm_theme_info *ti;
	void *iter = NULL;

	while ((ti = fymm_theme_iterate(&iter)) != NULL)
		fprintf(fp, "  %-10s %s\n", ti->name, ti->description);
}

/* Check a theme name against the catalogue, so that a typo is reported once
 * and by name rather than as a failure to render every input. */
static int check_theme(const char *name)
{
	const struct fymm_theme_info *ti;
	void *iter = NULL;

	while ((ti = fymm_theme_iterate(&iter)) != NULL) {
		if (!strcmp(ti->name, name))
			return 0;
	}
	fprintf(stderr, "%s: unknown theme '%s'; the built-in themes are:\n",
		progname, name);
	list_themes(stderr);
	return -1;
}

static int parse_color(const char *s, enum fymm_color_mode *modep)
{
	if (!strcmp(s, "auto"))
		*modep = FYMM_COLOR_AUTO;
	else if (!strcmp(s, "none") || !strcmp(s, "off") || !strcmp(s, "no"))
		*modep = FYMM_COLOR_NONE;
	else if (!strcmp(s, "16"))
		*modep = FYMM_COLOR_16;
	else if (!strcmp(s, "256"))
		*modep = FYMM_COLOR_256;
	else if (!strcmp(s, "true") || !strcmp(s, "truecolor") ||
		 !strcmp(s, "24bit"))
		*modep = FYMM_COLOR_TRUECOLOR;
	else
		return -1;
	return 0;
}

static int parse_charset(const char *s, enum fymm_charset *csp)
{
	if (!strcmp(s, "auto"))
		*csp = FYMM_CHARSET_AUTO;
	else if (!strcmp(s, "ascii"))
		*csp = FYMM_CHARSET_ASCII;
	else if (!strcmp(s, "unicode") || !strcmp(s, "utf8") ||
		 !strcmp(s, "utf-8"))
		*csp = FYMM_CHARSET_UNICODE;
	else if (!strcmp(s, "rich"))
		*csp = FYMM_CHARSET_RICH;
	else
		return -1;
	return 0;
}

static int parse_fit(const char *s, enum fymm_fit *fp)
{
	if (!strcmp(s, "shrink"))
		*fp = FYMM_FIT_SHRINK;
	else if (!strcmp(s, "legend"))
		*fp = FYMM_FIT_LEGEND;
	else if (!strcmp(s, "clip"))
		*fp = FYMM_FIT_CLIP;
	else if (!strcmp(s, "none") || !strcmp(s, "off"))
		*fp = FYMM_FIT_NONE;
	else
		return -1;
	return 0;
}

/* Render one source, reporting whatever the parse had to say about it. */
static int do_one(const char *path, const struct fymm_render_cfg *rcfg,
		  unsigned int pflags, bool dump_model, bool flow, bool quiet,
		  bool interactive, FILE *out)
{
	struct fymm_parse_cfg pcfg;
	struct fymm_diagram *d;
	char *text;
	int rc = 0;

	memset(&pcfg, 0, sizeof(pcfg));
	pcfg.struct_size = sizeof(pcfg);
	pcfg.flags = pflags;

	d = fymm_parse_file(path, &pcfg);
	if (!d) {
		fprintf(stderr, "%s: cannot read %s\n", progname, path);
		return -1;
	}

	if (!quiet) {
		text = fymm_diagram_diagnostics_string(d);
		if (text) {
			fputs(text, stderr);
			fymm_free(text);
		}
	}

	if (fymm_diagram_has_errors(d)) {
		rc = -1;
		goto out;
	}

	if (interactive) {
		rc = fymm_viewer_run(d, rcfg, progname);
		goto out;
	}

	text = dump_model ? fymm_diagram_model_to_yaml(d, flow) :
			    fymm_render(d, rcfg);
	if (!text) {
		fprintf(stderr, "%s: cannot render %s\n", progname, path);
		rc = -1;
		goto out;
	}
	fputs(text, out);
	if (dump_model && (!*text || text[strlen(text) - 1] != '\n'))
		fputc('\n', out);
	fymm_free(text);

out:
	fymm_diagram_destroy(d);
	return rc;
}

int main(int argc, char *argv[])
{
	struct fymm_render_cfg rcfg;
	const char *output = NULL;
	unsigned int pflags = 0;
	bool dump_model = false, flow = false, quiet = false;
	bool interactive = false;
	FILE *out = stdout;
	int i, opt, rc = 0;

	if (argv[0] && *argv[0])
		progname = argv[0];

	fymm_render_cfg_default(&rcfg);

	while ((opt = getopt_long(argc, argv, "o:w:F:c:C:at:S:Lb:imfsqVh", lopts,
				  NULL)) != -1) {
		switch (opt) {
		case 'o':
			output = optarg;
			break;
		case 'w':
			rcfg.width = atoi(optarg);
			break;
		case 'F':
			if (parse_fit(optarg, &rcfg.fit)) {
				fprintf(stderr, "%s: bad fit policy '%s'\n",
					progname, optarg);
				return 1;
			}
			break;
		case 'c':
			if (parse_color(optarg, &rcfg.color)) {
				fprintf(stderr, "%s: bad colour mode '%s'\n",
					progname, optarg);
				return 1;
			}
			break;
		case 'C':
			if (parse_charset(optarg, &rcfg.charset)) {
				fprintf(stderr, "%s: bad charset '%s'\n",
					progname, optarg);
				return 1;
			}
			break;
		case 'a':
			rcfg.charset = FYMM_CHARSET_ASCII;
			break;
		case 't':
			rcfg.theme = optarg;
			break;
		case 'S':
			rcfg.theme_path = optarg;
			break;
		case 'L':
			list_themes(stdout);
			return 0;
		case 'b':
			if (!strcmp(optarg, "auto")) {
				rcfg.background = FYMM_BG_AUTO;
			} else if (!strcmp(optarg, "dark")) {
				rcfg.background = FYMM_BG_DARK;
			} else if (!strcmp(optarg, "light")) {
				rcfg.background = FYMM_BG_LIGHT;
			} else {
				fprintf(stderr, "%s: bad background '%s'\n",
					progname, optarg);
				return 1;
			}
			break;
		case 'i':
			interactive = true;
			break;
		case 'm':
			dump_model = true;
			break;
		case 'f':
			flow = true;
			break;
		case 's':
			pflags |= FYMM_PF_STRICT;
			break;
		case 'q':
			quiet = true;
			break;
		case 'V':
			printf("%s\n", fymm_library_version());
			return 0;
		case 'h':
			usage(stdout);
			return 0;
		default:
			usage(stderr);
			return 1;
		}
	}

	if (rcfg.theme && check_theme(rcfg.theme))
		return 1;

	/* the viewer takes the screen, so it takes one diagram */
	if (interactive && (dump_model || output || optind + 1 < argc)) {
		fprintf(stderr,
			"%s: --interactive takes one file and no output\n",
			progname);
		return 1;
	}

	if (output) {
		out = fopen(output, "wb");
		if (!out) {
			fprintf(stderr, "%s: cannot write %s\n", progname,
				output);
			return 1;
		}
	}

	if (optind >= argc) {
		if (do_one("-", &rcfg, pflags, dump_model, flow, quiet,
			   interactive, out))
			rc = 1;
	} else {
		for (i = optind; i < argc; i++) {
			if (do_one(argv[i], &rcfg, pflags, dump_model, flow,
				   quiet, interactive, out))
				rc = 1;
		}
	}

	if (out != stdout)
		fclose(out);
	return rc;
}
