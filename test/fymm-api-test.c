/*
 * fymm-api-test.c - assertions over the public libfymermaid API
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

#include <libfymermaid.h>

#include "fymm-color.h"
#include "fymm-markdown.h"

static int failures;

#define CHECK(_cond, _fmt, ...) \
	do { \
		if (!(_cond)) { \
			fprintf(stderr, "%s:%d: " _fmt "\n", __func__, \
				__LINE__, ##__VA_ARGS__); \
			failures++; \
		} \
	} while (0)

static struct fymm_diagram *parse(const char *text)
{
	struct fymm_parse_cfg cfg;

	memset(&cfg, 0, sizeof(cfg));
	cfg.struct_size = sizeof(cfg);
	cfg.filename = "test.mmd";
	return fymm_parse(text, FYMM_NT, &cfg);
}

/* How many diagnostics of @level the parse produced. */
static size_t count_level(const struct fymm_diagram *d, const char *level)
{
	fy_generic diags = fymm_diagram_diagnostics(d);
	size_t i, count, n = 0;

	count = fy_is_sequence(diags) ?
		fy_generic_sequence_get_item_count(diags) : 0;
	for (i = 0; i < count; i++) {
		if (!strcmp(fy_get(fy_get_at(diags, i), "level", ""), level))
			n++;
	}
	return n;
}

static void test_version(void)
{
	const char *v = fymm_library_version();

	CHECK(v && *v, "the library version is empty");
}

static void test_bad_input(void)
{
	struct fymm_diagram *d;

	CHECK(!fymm_parse(NULL, 0, NULL), "a NULL source should not parse");

	d = parse("");
	CHECK(d != NULL, "an empty source should still yield a diagram");
	CHECK(fymm_diagram_has_errors(d), "an empty source should be an error");
	CHECK(fymm_diagram_type(d) == FYMM_DT_UNKNOWN, "type should be unknown");
	CHECK(!fymm_render(d, NULL), "a diagram with errors must not render");
	fymm_diagram_destroy(d);

	/*
	 * A header this library does not know. Naming a real mermaid type
	 * here would rot as soon as that type is implemented, so the case
	 * uses a keyword that cannot become one.
	 */
	d = parse("notADiagramType\n  a --> b\n");
	CHECK(d && fymm_diagram_has_errors(d),
	      "an unsupported diagram type should be an error");
	CHECK(d && fymm_diagram_type(d) == FYMM_DT_UNKNOWN,
	      "an unsupported diagram type should report as unknown");
	fymm_diagram_destroy(d);
}

static void test_model_shape(void)
{
	static const char src[] =
		"gitGraph\n"
		"   commit id: \"a\"\n"
		"   branch dev\n"
		"   commit id: \"b\"\n"
		"   checkout main\n"
		"   merge dev id: \"m\" tag: \"v1\"\n";
	struct fymm_diagram *d;
	fy_generic model, commits, branches, merge;

	d = parse(src);
	CHECK(d != NULL, "parse returned NULL");
	if (!d)
		return;
	CHECK(!fymm_diagram_has_errors(d), "unexpected errors");
	CHECK(fymm_diagram_type(d) == FYMM_DT_GITGRAPH, "wrong diagram type");
	CHECK(!strcmp(fymm_diagram_type_name(fymm_diagram_type(d)), "gitGraph"),
	      "wrong type name");

	model = fymm_diagram_model(d);
	CHECK(fy_is_mapping(model), "the model should be a mapping");
	CHECK(!strcmp(fy_get(model, "type", ""), "gitGraph"), "wrong type key");
	CHECK(!strcmp(fy_get(model, "orientation", ""), "LR"),
	      "the default orientation should be LR");

	branches = fy_get(model, "branches");
	CHECK(fy_generic_sequence_get_item_count(branches) == 2,
	      "expected two branches");
	CHECK(!strcmp(fy_get(fy_get_at(branches, 0), "name", ""), "main"),
	      "the first branch should be main");

	commits = fy_get(model, "commits");
	CHECK(fy_generic_sequence_get_item_count(commits) == 3,
	      "expected three commits");

	/* the merge commit joins the two lines: two parents, in that order */
	merge = fy_get_at(commits, 2);
	CHECK(!strcmp(fy_get(merge, "type", ""), "MERGE"),
	      "the third commit should be a merge");
	CHECK(fy_generic_sequence_get_item_count(fy_get(merge, "parents")) == 2,
	      "a merge commit should have two parents");
	CHECK((long long)fy_get_at(fy_get(merge, "parents"), 0, -1LL) == 0,
	      "the first parent should be the tip of the current branch");
	CHECK((long long)fy_get_at(fy_get(merge, "parents"), 1, -1LL) == 1,
	      "the second parent should be the tip of the merged branch");

	fymm_diagram_destroy(d);
}

/*
 * Branch display order: main leads, then the branches with no `order:` in
 * the order they appeared, then the ordered ones sorted numerically.
 */
static void test_branch_order(void)
{
	static const char src[] =
		"gitGraph\n"
		"   commit\n"
		"   branch late order: 9\n"
		"   branch early order: 1\n"
		"   branch plain\n";
	static const char *const want[] = { "main", "plain", "early", "late" };
	struct fymm_diagram *d;
	fy_generic branches;
	size_t i, count;

	d = parse(src);
	if (!d)
		return;
	CHECK(!fymm_diagram_has_errors(d), "unexpected errors");

	branches = fy_get(fymm_diagram_model(d), "branches");
	count = fy_generic_sequence_get_item_count(branches);
	CHECK(count == 4, "expected four branches, got %zu", count);

	for (i = 0; i < count && i < 4; i++) {
		fy_generic b;
		size_t j;

		/* find the branch sitting in lane i */
		for (j = 0; j < count; j++) {
			b = fy_get_at(branches, j);
			if ((long long)fy_get(b, "lane", -1LL) == (long long)i)
				break;
		}
		CHECK(j < count, "no branch in lane %zu", i);
		if (j >= count)
			continue;
		CHECK(!strcmp(fy_get(b, "name", ""), want[i]),
		      "lane %zu should be '%s', is '%s'", i, want[i],
		      fy_get(b, "name", ""));
	}
	fymm_diagram_destroy(d);
}

/* Each of these is one specific mistake, and should report exactly one. */
static void test_errors(void)
{
	static const struct {
		const char *name;
		const char *src;
	} cases[] = {
		{ "unknown checkout",
		  "gitGraph\n commit\n checkout nope\n" },
		{ "self merge",
		  "gitGraph\n commit\n merge main\n" },
		{ "unknown merge",
		  "gitGraph\n commit\n merge nope\n" },
		{ "duplicate merge id",
		  "gitGraph\n commit id: \"a\"\n branch dev\n commit\n"
		  " checkout main\n merge dev id: \"a\"\n" },
		{ "merge with no commit on the current branch",
		  "gitGraph\n branch dev\n checkout dev\n commit\n"
		  " checkout main\n merge dev\n" },
		{ "merge of branches with the same head",
		  "gitGraph\n commit\n branch dev\n checkout main\n"
		  " merge dev\n" },
		{ "cherry-pick of a merge without a parent",
		  "gitGraph\n commit id: \"z\"\n branch dev\n"
		  " branch rel\n checkout dev\n commit id: \"a\"\n"
		  " checkout main\n merge dev id: \"m\"\n"
		  " checkout rel\n commit id: \"c\"\n"
		  " cherry-pick id: \"m\"\n" },
		{ "cherry-pick of a merge with a parent that is not immediate",
		  "gitGraph\n commit id: \"z\"\n branch dev\n"
		  " branch rel\n checkout dev\n commit id: \"a\"\n"
		  " commit id: \"b\"\n"
		  " checkout main\n merge dev id: \"m\"\n"
		  " checkout rel\n commit id: \"c\"\n"
		  " cherry-pick id: \"m\" parent: \"a\"\n" },
		{ "duplicate branch",
		  "gitGraph\n commit\n branch dev\n branch dev\n" },
		{ "cherry-pick of an unknown commit",
		  "gitGraph\n commit\n branch dev\n commit\n"
		  " cherry-pick id: \"nope\"\n" },
		{ "cherry-pick onto the same branch",
		  "gitGraph\n commit id: \"a\"\n commit\n"
		  " cherry-pick id: \"a\"\n" },
		{ "cherry-pick without an id",
		  "gitGraph\n commit\n branch dev\n commit\n"
		  " cherry-pick\n" },
		{ "unknown statement",
		  "gitGraph\n commit\n rebase main\n" },
		{ "bad commit type",
		  "gitGraph\n commit type: SPARKLY\n" },
		{ "attribute without a value",
		  "gitGraph\n commit id:\n" },
	};
	struct fymm_diagram *d;
	size_t i, n;

	for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		d = parse(cases[i].src);
		if (!d)
			continue;
		n = count_level(d, "error");
		CHECK(n == 1, "%s: expected one error, got %zu",
		      cases[i].name, n);
		CHECK(fymm_diagram_has_errors(d), "%s: has_errors is false",
		      cases[i].name);
		fymm_diagram_destroy(d);
	}
}

/*
 * These are the cases mermaid reports without refusing the diagram. A repeated
 * commit id makes a later cherry-pick ambiguous but does not stop the render.
 */
static void test_warnings(void)
{
	static const struct {
		const char *name;
		const char *src;
	} cases[] = {
		{ "duplicate commit id",
		  "gitGraph\n commit id: \"a\"\n commit id: \"a\"\n" },
		{ "unknown attribute",
		  "gitGraph\n commit wibble: 3\n" },
		{ "unimplemented orientation",
		  "gitGraph BT:\n commit\n" },
	};
	struct fymm_diagram *d;
	size_t i, n;
	char *text;

	for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		d = parse(cases[i].src);
		if (!d)
			continue;
		n = count_level(d, "warning");
		CHECK(n == 1, "%s: expected one warning, got %zu",
		      cases[i].name, n);
		CHECK(!fymm_diagram_has_errors(d), "%s: should still render",
		      cases[i].name);
		text = fymm_render(d, NULL);
		CHECK(text != NULL, "%s: render failed", cases[i].name);
		fymm_free(text);
		fymm_diagram_destroy(d);
	}
}

/* An accessibility statement reaches the model, and stands in for a title. */
static void test_accessibility(void)
{
	static const char src[] =
		"gitGraph\n"
		"  accTitle: A release graph\n"
		"  accDescr {\n"
		"    One branch,\n"
		"    merged back.\n"
		"  }\n"
		"  commit\n";
	struct fymm_diagram *d;
	fy_generic model;

	d = parse(src);
	if (!d)
		return;
	CHECK(!fymm_diagram_has_errors(d), "unexpected errors");

	model = fymm_diagram_model(d);
	CHECK(!strcmp(fy_get(model, "accTitle", ""), "A release graph"),
	      "accTitle should reach the model");
	CHECK(!strcmp(fy_get(model, "accDescr", ""),
		      "One branch,\nmerged back."),
	      "the braced accDescr should join its lines");
	CHECK(!strcmp(fy_get(model, "title", ""), "A release graph"),
	      "accTitle should stand in for an absent title");

	fymm_diagram_destroy(d);
}

/* An unknown attribute is a warning, and STRICT turns it into an error. */
static void test_strict(void)
{
	static const char src[] = "gitGraph\n commit wibble: 3\n";
	struct fymm_parse_cfg cfg;
	struct fymm_diagram *d;

	d = parse(src);
	if (d) {
		CHECK(!fymm_diagram_has_errors(d), "a warning is not an error");
		CHECK(count_level(d, "warning") == 1, "expected one warning");
		fymm_diagram_destroy(d);
	}

	memset(&cfg, 0, sizeof(cfg));
	cfg.struct_size = sizeof(cfg);
	cfg.flags = FYMM_PF_STRICT;
	d = fymm_parse(src, FYMM_NT, &cfg);
	if (d) {
		CHECK(fymm_diagram_has_errors(d),
		      "STRICT should promote the warning");
		CHECK(count_level(d, "warning") == 0,
		      "STRICT should leave no warnings behind");
		fymm_diagram_destroy(d);
	}
}

/* The frontmatter and the %%{init}%% directive both feed the same config. */
static void test_config_sources(void)
{
	static const char src[] =
		"---\n"
		"title: Titled\n"
		"config:\n"
		"  gitGraph:\n"
		"    mainBranchName: trunk\n"
		"    showCommitLabel: false\n"
		"---\n"
		"%%{init: {'gitGraph': {'showCommitLabel': true}}}%%\n"
		"gitGraph\n"
		"   commit id: \"a\"\n";
	struct fymm_diagram *d;
	fy_generic model, config;

	d = parse(src);
	if (!d)
		return;
	CHECK(!fymm_diagram_has_errors(d), "unexpected errors");

	model = fymm_diagram_model(d);
	config = fy_get(model, "config");
	CHECK(!strcmp(fy_get(model, "title", ""), "Titled"),
	      "the frontmatter title should reach the model");
	CHECK(!strcmp(fy_get(config, "mainBranchName", ""), "trunk"),
	      "mainBranchName should come from the frontmatter");
	CHECK(!strcmp(fy_get(fy_get_at(fy_get(model, "branches"), 0), "name", ""),
		      "trunk"),
	      "the main branch should be named trunk");
	/* the directive is applied after the frontmatter, so it wins */
	CHECK(fy_get(config, "showCommitLabel", false) == true,
	      "the init directive should override the frontmatter");
	/* the submap is hoisted, not left nested */
	CHECK(fy_is_invalid(fy_get(config, "gitGraph")),
	      "the gitGraph submap should have been hoisted");

	fymm_diagram_destroy(d);
}

/* ASCII output must be seven bit, whatever the diagram contains. */
static void test_render_modes(void)
{
	static const char src[] =
		"gitGraph\n"
		"   commit id: \"a\" type: HIGHLIGHT tag: \"v1\"\n"
		"   branch dev\n"
		"   commit id: \"b\" type: REVERSE\n"
		"   checkout main\n"
		"   merge dev\n";
	struct fymm_render_cfg rcfg;
	struct fymm_diagram *d;
	char *plain, *labelled;
	const unsigned char *p;

	d = parse(src);
	if (!d)
		return;
	CHECK(!fymm_diagram_has_errors(d), "unexpected errors");

	fymm_render_cfg_default(&rcfg);
	rcfg.color = FYMM_COLOR_NONE;
	rcfg.charset = FYMM_CHARSET_ASCII;
	plain = fymm_render(d, &rcfg);
	CHECK(plain != NULL, "the ASCII render produced nothing");
	if (plain) {
		for (p = (const unsigned char *)plain; *p; p++) {
			if (*p >= 0x80) {
				CHECK(false, "ASCII output has a byte >= 0x80");
				break;
			}
		}
		CHECK(!strchr(plain, '\033'),
		      "FYMM_COLOR_NONE output should carry no escapes");
		CHECK(strstr(plain, "[v1]") != NULL, "the tag is missing");
		CHECK(strstr(plain, "b") != NULL, "a commit label is missing");
	}

	/* turning the labels off through the render config must shrink it */
	rcfg.options = fy_mapping(fymm_diagram_builder(d),
				  "showCommitLabel", false);
	labelled = fymm_render(d, &rcfg);
	CHECK(labelled != NULL, "the unlabelled render produced nothing");
	if (plain && labelled)
		CHECK(strlen(labelled) < strlen(plain),
		      "dropping the labels should shrink the output");

	fymm_free(plain);
	fymm_free(labelled);
	fymm_diagram_destroy(d);
}

/*
 * A themed render must reach a 256 and a 16 colour terminal, so a colour is
 * reduced to the nearest entry of the palette the terminal has. These pin the
 * reduction at the ends and at a few interior points.
 */
static void test_color_reduction(void)
{
	static const struct {
		const char *text;
		unsigned int rgb;
		int xterm256;
		int ansi16;
	} cases[] = {
		{ "#000000", 0x000000, 16, 30 },	/* black */
		{ "#ffffff", 0xffffff, 231, 97 },	/* white */
		{ "#ff0000", 0xff0000, 196, 91 },	/* red */
		{ "#00ff00", 0x00ff00, 46, 92 },
		{ "#0000ff", 0x0000ff, 21, 34 },	/* nearer plain blue */
		{ "#f00", 0xff0000, 196, 91 },		/* the short form */
		{ "ff0000", 0xff0000, 196, 91 },	/* without the hash */
		{ "red", 0xcd0000, 160, 31 },		/* by ANSI name */
		{ "brightblue", 0x5c5cff, 63, 94 },
		{ "196", 0xff0000, 196, 91 },		/* an xterm index */
		{ "#808080", 0x808080, 244, 90 },	/* a grey ramp entry */
	};
	size_t i;
	unsigned int rgb;

	for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		rgb = fymm_color_parse(cases[i].text);
		CHECK(rgb == cases[i].rgb,
		      "'%s' parsed as %06x, expected %06x",
		      cases[i].text, rgb, cases[i].rgb);
		if (rgb == FYMM_RGB_INVALID)
			continue;
		CHECK(fymm_rgb_to_xterm256(rgb) == cases[i].xterm256,
		      "'%s' reduced to xterm %d, expected %d", cases[i].text,
		      fymm_rgb_to_xterm256(rgb), cases[i].xterm256);
		CHECK(fymm_rgb_to_ansi16(rgb) == cases[i].ansi16,
		      "'%s' reduced to SGR %d, expected %d", cases[i].text,
		      fymm_rgb_to_ansi16(rgb), cases[i].ansi16);
	}

	CHECK(fymm_color_parse("nonsense") == FYMM_RGB_INVALID,
	      "a word that is not a colour should not parse");
	CHECK(fymm_color_parse("#12345") == FYMM_RGB_INVALID,
	      "a five digit hex should not parse");
	CHECK(fymm_color_parse("300") == FYMM_RGB_INVALID,
	      "an index past 255 should not parse");
	CHECK(fymm_color_parse(NULL) == FYMM_RGB_INVALID,
	      "NULL should not parse");
}

/* Every shipped theme must load and change what a render emits. */
static void test_theme_catalogue(void)
{
	static const char src[] =
		"gitGraph\n commit id: \"a\"\n branch dev\n commit id: \"b\"\n";
	const struct fymm_theme_info *ti;
	struct fymm_render_cfg rcfg;
	struct fymm_diagram *d;
	char *plain, *themed;
	void *iter = NULL;
	size_t n = 0;

	d = parse(src);
	if (!d)
		return;

	fymm_render_cfg_default(&rcfg);
	rcfg.color = FYMM_COLOR_256;
	rcfg.charset = FYMM_CHARSET_ASCII;
	plain = fymm_render(d, &rcfg);
	CHECK(plain != NULL, "the unthemed render produced nothing");

	while ((ti = fymm_theme_iterate(&iter)) != NULL) {
		n++;
		CHECK(ti->name && *ti->name, "a theme has no name");
		CHECK(ti->description && *ti->description,
		      "theme '%s' has no description", ti->name);

		rcfg.theme = ti->name;
		themed = fymm_render(d, &rcfg);
		CHECK(themed != NULL, "theme '%s' produced no render",
		      ti->name);
		if (themed && plain && strcmp(ti->name, "default"))
			CHECK(strcmp(themed, plain) != 0,
			      "theme '%s' rendered identically to the default",
			      ti->name);
		fymm_free(themed);
	}
	CHECK(n >= 3, "expected at least three built-in themes, got %zu", n);

	/* an unknown theme fails rather than falling back silently */
	rcfg.theme = "no-such-theme";
	CHECK(fymm_render(d, &rcfg) == NULL,
	      "an unknown theme should not render");

	fymm_free(plain);
	fymm_diagram_destroy(d);
}

/* The mono theme carries structure with attributes and emits no colour. */
static void test_theme_mono(void)
{
	static const char src[] = "gitGraph\n commit id: \"a\"\n";
	struct fymm_render_cfg rcfg;
	struct fymm_diagram *d;
	char *text;

	d = parse(src);
	if (!d)
		return;

	fymm_render_cfg_default(&rcfg);
	rcfg.color = FYMM_COLOR_TRUECOLOR;
	rcfg.charset = FYMM_CHARSET_ASCII;
	rcfg.theme = "mono";
	text = fymm_render(d, &rcfg);
	CHECK(text != NULL, "the mono render produced nothing");
	if (text) {
		CHECK(strstr(text, "\033[") != NULL,
		      "mono should still emit attributes");
		CHECK(strstr(text, ";38;") == NULL,
		      "mono should emit no foreground colour");
	}
	fymm_free(text);
	fymm_diagram_destroy(d);
}

/* A label breaks at `<br>` in each of its spellings, and only a markdown
 * string is formatted. */
static void test_rich_text(void)
{
	static const struct {
		const char *text;
		size_t lines;
		int width;
	} breaks[] = {
		{ "one",			1, 3 },
		{ "one<br>two",			2, 3 },
		{ "one<br/>two",		2, 3 },
		{ "one<br />two",		2, 3 },
		{ "one</br>two",		2, 3 },
		{ "a<br>bb<br>ccc",		3, 3 },
		{ "Line1<br>Line2<br/>Line3</br>Line4", 4, 5 },
		{ "one\ntwo",			2, 3 },
	};
	struct fymm_rich *r;
	size_t i;

	for (i = 0; i < sizeof(breaks) / sizeof(breaks[0]); i++) {
		r = fymm_rich_parse(breaks[i].text, false);
		CHECK(r != NULL, "'%s' did not parse", breaks[i].text);
		if (!r)
			continue;
		CHECK(fymm_rich_lines(r) == breaks[i].lines,
		      "'%s' gave %zu lines, expected %zu", breaks[i].text,
		      fymm_rich_lines(r), breaks[i].lines);
		CHECK(fymm_rich_width(r) == breaks[i].width,
		      "'%s' measured %d, expected %d", breaks[i].text,
		      fymm_rich_width(r), breaks[i].width);
		fymm_rich_destroy(r);
	}

	/* a plain label keeps its asterisks; a markdown one does not */
	r = fymm_rich_parse("The **cat** in the hat", false);
	CHECK(r && fymm_rich_width(r) == 22,
	      "a plain label should keep its asterisks, measured %d",
	      r ? fymm_rich_width(r) : -1);
	fymm_rich_destroy(r);

	r = fymm_rich_parse("The **cat** in the hat", true);
	CHECK(r && fymm_rich_width(r) == 18,
	      "a markdown label should drop its asterisks, measured %d",
	      r ? fymm_rich_width(r) : -1);
	fymm_rich_destroy(r);

	/* markdown and a break together */
	r = fymm_rich_parse("The **cat**<br>in the *hat*", true);
	CHECK(r && fymm_rich_lines(r) == 2, "expected two lines");
	CHECK(r && fymm_rich_line_width(r, 0) == 7, "line 0 measured %d",
	      r ? fymm_rich_line_width(r, 0) : -1);
	fymm_rich_destroy(r);

	r = fymm_rich_parse("", false);
	CHECK(r != NULL, "an empty label should parse");
	fymm_rich_destroy(r);

	r = fymm_rich_parse(NULL, false);
	CHECK(r != NULL, "NULL should parse as empty");
	fymm_rich_destroy(r);
}

/* The attributes a markdown span carries must reach the emitted escapes. */
static void test_rich_attributes(void)
{
	struct fymm_canvas *cv;
	struct fymm_rich *r;
	char *out;

	cv = fymm_canvas_create(40, 1, FYMM_CHARSET_UNICODE, FYMM_COLOR_256,
				NULL);
	CHECK(cv != NULL, "the canvas was not created");
	if (!cv)
		return;

	r = fymm_rich_parse("a **b** _c_ ~~d~~", true);
	CHECK(r != NULL, "the markdown label did not parse");
	if (r) {
		fymm_rich_draw_line(cv, 0, 0, r, 0, FYMM_COLOR_DEFAULT, 0);
		fymm_rich_destroy(r);
	}

	out = fymm_canvas_emit(cv);
	CHECK(out != NULL, "the canvas emitted nothing");
	if (out) {
		CHECK(strstr(out, ";1m") != NULL, "bold was not emitted");
		CHECK(strstr(out, ";3m") != NULL, "italic was not emitted");
		CHECK(strstr(out, ";9m") != NULL, "strikethrough was not emitted");
		free(out);
	}
	fymm_canvas_destroy(cv);
}

/* Does @text carry the escape that selects @rgb as 24 bit foreground? */
static bool has_truecolor(const char *text, unsigned int rgb)
{
	char want[32];

	snprintf(want, sizeof(want), ";38;2;%u;%u;%u", (rgb >> 16) & 0xff,
		 (rgb >> 8) & 0xff, rgb & 0xff);
	return text && strstr(text, want) != NULL;
}

/* Render @src and return the text, at the given colour depth. */
static char *render_at(const char *src, enum fymm_color_mode mode,
		       const char *theme, const char *theme_path)
{
	struct fymm_render_cfg rcfg;
	struct fymm_diagram *d;
	char *out;

	d = parse(src);
	if (!d)
		return NULL;
	fymm_render_cfg_default(&rcfg);
	rcfg.color = mode;
	rcfg.width = 80;
	rcfg.charset = FYMM_CHARSET_UNICODE;
	rcfg.theme = theme;
	rcfg.theme_path = theme_path;
	out = fymm_render(d, &rcfg);
	fymm_diagram_destroy(d);
	return out;
}

/*
 * A 24 bit terminal must get the colour that was asked for, byte for byte:
 * reducing it to a palette entry there would throw away what it can show.
 */
static void test_truecolor(void)
{
	static const char src[] = "gitGraph\n commit id: \"a\"\n";
	char *out;

	out = render_at(src, FYMM_COLOR_TRUECOLOR, NULL, NULL);
	CHECK(out != NULL, "the truecolor render produced nothing");
	/* the built-in git0 */
	CHECK(has_truecolor(out, 0x3b8eea),
	      "the branch colour did not reach the terminal unreduced");
	CHECK(out && strstr(out, "38;5;") == NULL,
	      "a truecolor render should emit no palette index");
	fymm_free(out);

	/* the same colour on a lesser terminal is reduced, not dropped */
	out = render_at(src, FYMM_COLOR_256, NULL, NULL);
	CHECK(out && strstr(out, "38;5;") != NULL,
	      "a 256 colour render should emit a palette index");
	CHECK(out && strstr(out, "38;2;") == NULL,
	      "a 256 colour render should emit no 24 bit colour");
	fymm_free(out);

	out = render_at(src, FYMM_COLOR_16, NULL, NULL);
	CHECK(out && strstr(out, "38;") == NULL,
	      "a 16 colour render should use the plain SGR colours");
	fymm_free(out);
}

/*
 * Mermaid's own theme variables reach the palette, and the layers apply in
 * order: the named theme, then the diagram's variables, then a theme file.
 */
static void test_theme_variables(void)
{
	static const char src[] =
		"%%{init: {'themeVariables': {'git0': '#ff0000',"
		" 'tagLabelColor': '#ffcc00'}}}%%\n"
		"gitGraph\n commit id: \"a\" tag: \"v1\"\n";
	static const char pie[] =
		"%%{init: {'themeVariables': {'pie1': '#010203'}}}%%\n"
		"pie\n \"ash\" : 60\n";
	char *out;
	FILE *fp;

	out = render_at(src, FYMM_COLOR_TRUECOLOR, NULL, NULL);
	CHECK(has_truecolor(out, 0xff0000),
	      "git0 from themeVariables did not reach the palette");
	CHECK(has_truecolor(out, 0xffcc00),
	      "tagLabelColor from themeVariables did not reach the palette");
	CHECK(!has_truecolor(out, 0x3b8eea),
	      "the built-in git0 should have been replaced");
	fymm_free(out);

	/* a pie names the same series colours differently */
	out = render_at(pie, FYMM_COLOR_TRUECOLOR, NULL, NULL);
	CHECK(has_truecolor(out, 0x010203),
	      "pie1 from themeVariables did not reach the palette");
	fymm_free(out);

	/* the diagram's variables beat the theme the caller named */
	out = render_at(src, FYMM_COLOR_TRUECOLOR, "light", NULL);
	CHECK(has_truecolor(out, 0xff0000),
	      "themeVariables should win over a named theme");
	fymm_free(out);

	/* and a theme file beats the diagram's variables */
	fp = fopen("theme-variables-test.yaml", "w");
	CHECK(fp != NULL, "could not write the theme file");
	if (fp) {
		fputs("colors:\n  git0: \"#0000ff\"\n", fp);
		fclose(fp);

		out = render_at(src, FYMM_COLOR_TRUECOLOR, NULL,
				"theme-variables-test.yaml");
		CHECK(has_truecolor(out, 0x0000ff),
		      "a theme file should win over themeVariables");
		CHECK(has_truecolor(out, 0xffcc00),
		      "a key the file leaves alone should keep its value");
		fymm_free(out);
		remove("theme-variables-test.yaml");
	}

	/* a variable a terminal cannot use is ignored, not an error */
	out = render_at("%%{init: {'themeVariables': {'fontSize': '80px',"
			" 'git0': 'nonsense'}}}%%\n"
			"gitGraph\n commit\n", FYMM_COLOR_TRUECOLOR, NULL,
			NULL);
	CHECK(out != NULL, "an unusable theme variable should not stop a render");
	CHECK(has_truecolor(out, 0x3b8eea),
	      "a colour that does not parse should leave the default");
	fymm_free(out);
}

/*
 * The colour depth a terminal is read as. A `-direct` terminfo entry means
 * 24 bit, not 256; reading it as 256 threw away what the terminal could show.
 */
static void test_color_detection(void)
{
	static const struct {
		const char *colorterm;
		const char *term;
		enum fymm_color_mode want;
		const char *why;
	} cases[] = {
		{ "truecolor", "xterm-256color", FYMM_COLOR_TRUECOLOR,
		  "COLORTERM wins over TERM" },
		{ "24bit", "xterm", FYMM_COLOR_TRUECOLOR, "24bit is truecolor" },
		{ NULL, "xterm-direct", FYMM_COLOR_TRUECOLOR,
		  "a direct-colour entry is 24 bit" },
		{ NULL, "tmux-direct", FYMM_COLOR_TRUECOLOR,
		  "so is tmux's" },
		{ NULL, "xterm-256color", FYMM_COLOR_256, "256 is 256" },
		{ NULL, "xterm", FYMM_COLOR_16, "a plain terminal is 16" },
		{ NULL, "dumb", FYMM_COLOR_NONE, "dumb has no colour" },
		{ NULL, "", FYMM_COLOR_NONE, "no TERM has no colour" },
	};
	enum fymm_color_mode got;
	size_t i;

	/* force colour on, so that not being a terminal does not decide it */
	setenv("CLICOLOR_FORCE", "1", 1);
	unsetenv("NO_COLOR");

	for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		if (cases[i].colorterm)
			setenv("COLORTERM", cases[i].colorterm, 1);
		else
			unsetenv("COLORTERM");
		setenv("TERM", cases[i].term, 1);

		got = fymm_detect_color_mode(-1);
		CHECK(got == cases[i].want,
		      "TERM=%s COLORTERM=%s gave %d, expected %d (%s)",
		      cases[i].term, cases[i].colorterm ? cases[i].colorterm : "",
		      (int)got, (int)cases[i].want, cases[i].why);
	}

	/* NO_COLOR beats everything */
	setenv("COLORTERM", "truecolor", 1);
	setenv("NO_COLOR", "1", 1);
	CHECK(fymm_detect_color_mode(-1) == FYMM_COLOR_NONE,
	      "NO_COLOR should win over COLORTERM");
	unsetenv("NO_COLOR");
	unsetenv("CLICOLOR_FORCE");
}

/*
 * What the terminal is drawn on. `$COLORFGBG` is what a terminal that sets it
 * says; kitty and ghostty do not set it, which is what the OSC 11 query is
 * for, and that needs a terminal to answer so it is not exercised here.
 */
static void test_background_detection(void)
{
	static const struct {
		const char *colorfgbg;
		enum fymm_background want;
		const char *why;
	} cases[] = {
		{ "15;0", FYMM_BG_DARK, "background 0 is dark" },
		{ "0;15", FYMM_BG_LIGHT, "background 15 is light" },
		{ "15;default", FYMM_BG_DARK, "an unnamed background is dark" },
		{ "7;0", FYMM_BG_DARK, "background 0 again" },
		{ "0;7", FYMM_BG_LIGHT, "7 is the light half" },
		{ "0;6", FYMM_BG_DARK, "6 is the dark half" },
		{ "12;8;0", FYMM_BG_DARK, "the last field is the background" },
		{ "12;8;15", FYMM_BG_LIGHT, "the last field again" },
	};
	size_t i;

	unsetenv("FYMM_BACKGROUND");
	for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		setenv("COLORFGBG", cases[i].colorfgbg, 1);
		CHECK(fymm_detect_background(-1) == cases[i].want,
		      "COLORFGBG=%s gave %d, expected %d (%s)",
		      cases[i].colorfgbg, (int)fymm_detect_background(-1),
		      (int)cases[i].want, cases[i].why);
	}

	/* an explicit answer settles it */
	setenv("COLORFGBG", "0;15", 1);
	setenv("FYMM_BACKGROUND", "dark", 1);
	CHECK(fymm_detect_background(-1) == FYMM_BG_DARK,
	      "FYMM_BACKGROUND should win over COLORFGBG");
	setenv("FYMM_BACKGROUND", "light", 1);
	CHECK(fymm_detect_background(-1) == FYMM_BG_LIGHT,
	      "FYMM_BACKGROUND light should be honoured");
	unsetenv("FYMM_BACKGROUND");

	/* nothing to go on, and no terminal to ask */
	unsetenv("COLORFGBG");
	CHECK(fymm_detect_background(-1) == FYMM_BG_DARK,
	      "a terminal is dark unless something says otherwise");
}

/* A light terminal takes the light theme, unless a theme was named. */
static void test_background_theme(void)
{
	static const char src[] = "gitGraph\n commit id: \"a\"\n";
	struct fymm_render_cfg rcfg;
	struct fymm_diagram *d;
	char *dark, *light, *named;

	d = parse(src);
	if (!d)
		return;

	fymm_render_cfg_default(&rcfg);
	rcfg.color = FYMM_COLOR_TRUECOLOR;
	rcfg.width = 80;
	rcfg.charset = FYMM_CHARSET_ASCII;

	rcfg.background = FYMM_BG_DARK;
	dark = fymm_render(d, &rcfg);
	rcfg.background = FYMM_BG_LIGHT;
	light = fymm_render(d, &rcfg);

	CHECK(dark && light && strcmp(dark, light) != 0,
	      "a light terminal should not get the dark palette");
	CHECK(has_truecolor(dark, 0x3b8eea),
	      "a dark terminal keeps the default branch colour");
	CHECK(has_truecolor(light, 0x0b5cad),
	      "a light terminal takes the light theme's branch colour");

	/* a named theme is the caller's decision, and stands */
	rcfg.theme = "default";
	named = fymm_render(d, &rcfg);
	CHECK(has_truecolor(named, 0x3b8eea),
	      "a named theme should survive a light terminal");

	fymm_free(dark);
	fymm_free(light);
	fymm_free(named);
	fymm_diagram_destroy(d);
}

int main(void)
{
	test_version();
	test_bad_input();
	test_model_shape();
	test_branch_order();
	test_errors();
	test_warnings();
	test_accessibility();
	test_strict();
	test_config_sources();
	test_render_modes();
	test_color_reduction();
	test_theme_catalogue();
	test_theme_mono();
	test_rich_text();
	test_rich_attributes();
	test_truecolor();
	test_theme_variables();
	test_color_detection();
	test_background_detection();
	test_background_theme();

	if (failures)
		fprintf(stderr, "%d check(s) failed\n", failures);
	return failures ? 1 : 0;
}
