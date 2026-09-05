/*
 * fymm-theme.c - the built-in theme catalogue and theme file loading
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
#include "fymm-color.h"
#include "fymm-internal.h"

/* struct fymm_embedded_theme - one theme compiled into the library */
struct fymm_embedded_theme {
	const char *name;
	const char *description;
	const unsigned char *data;
	size_t len;
};

/* Generated from the themes directory at configure time: FYMM_THEMES,
 * FYMM_THEME_COUNT and FYMM_THEME_COUNT_MAX. */
#include "fymm_themes.inc"

static struct fymm_theme_info fymm_theme_infos[FYMM_THEME_COUNT_MAX];

const struct fymm_theme_info *fymm_theme_iterate(void **prevp)
{
	size_t i;

	if (!prevp)
		return NULL;
	i = (size_t)(uintptr_t)*prevp;
	if (i >= FYMM_THEME_COUNT)
		return NULL;
	*prevp = (void *)(uintptr_t)(i + 1);

	fymm_theme_infos[i].name = FYMM_THEMES[i].name;
	fymm_theme_infos[i].description = FYMM_THEMES[i].description;
	return &fymm_theme_infos[i];
}

/* Parse a theme document and apply its `colors` mapping over @theme. */
static int fymm_theme_apply_text(struct fymm_theme *theme,
				 struct fy_generic_builder *gb,
				 const char *data, size_t len)
{
	fy_generic_sized_string input;
	fy_generic doc;

	input.data = data;
	input.size = len;
	doc = fy_parse(gb, input,
		       FYMM_YAML_PARSE_FLAGS | FYOPPF_INPUT_TYPE_STRING, NULL);
	if (!fy_is_mapping(doc))
		return -1;
	fymm_theme_apply(theme, fy_get(doc, "colors"));
	return 0;
}

static const struct fymm_embedded_theme *fymm_theme_find(const char *name)
{
	size_t i;

	for (i = 0; i < FYMM_THEME_COUNT; i++) {
		if (!strcmp(FYMM_THEMES[i].name, name))
			return &FYMM_THEMES[i];
	}
	return NULL;
}

int fymm_theme_resolve(struct fymm_theme *theme,
		       const struct fymm_render_cfg *cfg,
		       struct fy_generic_builder *gb)
{
	const struct fymm_embedded_theme *et;
	fy_generic doc;

	fymm_theme_default(theme);
	if (!cfg)
		return 0;

	/* a named theme is applied over the default, and a theme file over
	 * whatever that produced, so a file may carry one key */
	if (cfg->theme && *cfg->theme) {
		et = fymm_theme_find(cfg->theme);
		if (!et)
			return -1;
		if (fymm_theme_apply_text(theme, gb, (const char *)et->data,
					  et->len))
			return -1;
	}

	if (cfg->theme_path && *cfg->theme_path) {
		doc = fy_parse_file(gb, FYMM_YAML_PARSE_FLAGS, cfg->theme_path);
		if (!fy_is_mapping(doc))
			return -1;
		fymm_theme_apply(theme, fy_get(doc, "colors"));
	}
	return 0;
}
