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

	/* a real mermaid diagram whose type this library does not implement */
	d = parse("block-beta\n  columns 1\n  a b c\n");
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

	if (failures)
		fprintf(stderr, "%d check(s) failed\n", failures);
	return failures ? 1 : 0;
}
