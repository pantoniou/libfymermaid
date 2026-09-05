/*
 * libfymermaid.c - the diagram object, the front end parse and diagnostics
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

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fymm-internal.h"

const char *fymm_library_version(void)
{
	return FYMM_VERSION_STRING;
}

void fymm_free(void *ptr)
{
	free(ptr);
}

const char *fymm_diagram_type_name(enum fymm_diagram_type type)
{
	switch (type) {
	case FYMM_DT_GITGRAPH:
		return "gitGraph";
	case FYMM_DT_UNKNOWN:
		break;
	}
	return "unknown";
}

enum fymm_diagram_type fymm_diagram_type(const struct fymm_diagram *d)
{
	return d ? d->type : FYMM_DT_UNKNOWN;
}

fy_generic fymm_diagram_model(const struct fymm_diagram *d)
{
	return d ? d->model : fy_invalid;
}

fy_generic fymm_diagram_diagnostics(const struct fymm_diagram *d)
{
	return d ? d->diags : fy_seq_empty;
}

bool fymm_diagram_has_errors(const struct fymm_diagram *d)
{
	return d && d->nerrors > 0;
}

struct fy_generic_builder *fymm_diagram_builder(const struct fymm_diagram *d)
{
	return d ? d->gb : NULL;
}

void fymm_diagf(struct fymm_parser *p, bool error, int line, int col,
		const char *fmt, ...)
{
	struct fymm_diagram *d = p->d;
	char msg[512];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(msg, sizeof(msg), fmt, ap);
	va_end(ap);

	/* FYMM_PF_STRICT promotes each warning to an error. */
	if (!error && (p->flags & FYMM_PF_STRICT))
		error = true;

	d->diags = fy_append(d->gb, d->diags,
		fy_mapping(d->gb,
			"level", error ? "error" : "warning",
			"file", d->filename,
			"line", (long long)line,
			"column", (long long)col,
			"message", msg));
	if (error)
		d->nerrors++;
}

char *fymm_diagram_diagnostics_string(const struct fymm_diagram *d)
{
	size_t i, count, size, pos;
	fy_generic diag;
	char *out, *nout;
	int rc;

	if (!d || !fy_is_sequence(d->diags) || fy_empty(d->diags))
		return NULL;
	count = fy_len(d->diags);

	size = 256;
	pos = 0;
	out = malloc(size);
	if (!out)
		return NULL;

	for (i = 0; i < count; i++) {
		diag = fy_get_at(d->diags, i);
		for (;;) {
			rc = snprintf(out + pos, size - pos,
				      "%s:%lld:%lld: %s: %s\n",
				      fy_get(diag, "file", "<input>"),
				      (long long)fy_get(diag, "line", 0LL),
				      (long long)fy_get(diag, "column", 0LL),
				      fy_get(diag, "level", "error"),
				      fy_get(diag, "message", ""));
			if (rc >= 0 && (size_t)rc < size - pos) {
				pos += (size_t)rc;
				break;
			}
			size = size * 2 + (rc > 0 ? (size_t)rc : 0);
			nout = realloc(out, size);
			if (!nout) {
				free(out);
				return NULL;
			}
			out = nout;
		}
	}
	return out;
}

char *fymm_diagram_model_to_yaml(const struct fymm_diagram *d, bool flow)
{
	fy_generic emitted;
	const char *text;

	if (!d || fy_is_invalid(d->model))
		return NULL;

	emitted = fy_emit(d->gb, d->model,
			  FYOPEF_DISABLE_DIRECTORY | FYOPEF_MODE_YAML_1_2 |
			  FYOPEF_WIDTH_INF |
			  (flow ? FYOPEF_STYLE_FLOW : FYOPEF_STYLE_PRETTY),
			  NULL);
	if (fy_is_invalid(emitted))
		return NULL;
	text = fy_castp(&emitted, "");
	return text ? strdup(text) : NULL;
}

/*
 * Merge @src over @dst one key at a time. The layering is shallow. A nested
 * mapping value, such as themeVariables, transfers whole.
 */
static fy_generic fymm_config_merge(struct fy_generic_builder *gb,
				    fy_generic dst, fy_generic src)
{
	fy_generic k, v;

	if (!fy_is_mapping(src))
		return dst;
	fy_foreach_key_value(k, v, src)
		dst = fy_assoc(gb, dst, k, v);
	return dst;
}

/*
 * Apply @src to @dst and hoist the diagram submap. Mermaid nests the settings
 * of a diagram under the diagram name, as in
 * `config: { gitGraph: { showBranches: false } }`. The renderer reads one flat
 * mapping. Sibling keys, such as theme and themeVariables, stay as they are.
 */
static fy_generic fymm_config_apply(struct fy_generic_builder *gb,
				    fy_generic dst, fy_generic src,
				    const char *diagram_key)
{
	fy_generic sub;

	dst = fymm_config_merge(gb, dst, src);
	sub = fy_get(src, diagram_key);
	if (fy_is_mapping(sub)) {
		dst = fy_disassoc(gb, dst, diagram_key);
		dst = fymm_config_merge(gb, dst, sub);
	}
	return dst;
}

/*
 * Remove the `---` fenced YAML frontmatter from the head of @work and return
 * it as a generic. The removed bytes become spaces. Newlines stay, so each
 * later line number agrees with the source.
 */
static fy_generic fymm_take_frontmatter(struct fy_generic_builder *gb,
					char *work, size_t len)
{
	fy_generic_sized_string input;
	fy_generic fm;
	char *p, *q, *nl, *le, *body, *end;

	if (len < 4 || memcmp(work, "---", 3))
		return fy_invalid;
	p = work + 3;
	while (p < work + len && (*p == ' ' || *p == '\t' || *p == '\r'))
		p++;
	if (p >= work + len || *p != '\n')
		return fy_invalid;
	body = ++p;

	/* The terminator is a line that holds exactly --- or ... */
	end = NULL;
	while (p < work + len) {
		nl = memchr(p, '\n', (size_t)(work + len - p));
		le = nl ? nl : work + len;

		while (le > p && (le[-1] == '\r' || le[-1] == ' ' ||
				  le[-1] == '\t'))
			le--;
		if ((size_t)(le - p) == 3 &&
		    (!memcmp(p, "---", 3) || !memcmp(p, "...", 3))) {
			end = p;
			p = nl ? nl + 1 : work + len;
			break;
		}
		if (!nl)
			return fy_invalid;	/* unterminated: not frontmatter */
		p = nl + 1;
	}
	if (!end)
		return fy_invalid;

	input.data = body;
	input.size = (size_t)(end - body);
	fm = fy_parse(gb, input,
		      FYMM_YAML_PARSE_FLAGS | FYOPPF_INPUT_TYPE_STRING, NULL);

	for (q = work; q < p; q++) {
		if (*q != '\n')
			*q = ' ';
	}
	return fm;
}

/*
 * Collect each `%%{ ... }%%` directive, parse its body as YAML, and remove the
 * bytes from @work. Newlines stay, so each line number agrees with the source.
 */
static fy_generic fymm_take_directives(struct fy_generic_builder *gb,
				       char *work, size_t len)
{
	fy_generic_sized_string input;
	fy_generic seq = fy_seq_empty, v;
	char *p, *open, *close;

	for (p = work; (size_t)(p - work) + 3 < len; ) {
		open = memmem(p, len - (size_t)(p - work), "%%{", 3);
		if (!open)
			break;
		close = memmem(open, len - (size_t)(open - work), "}%%", 3);
		if (!close)
			break;

		input.data = open + 3;
		input.size = (size_t)(close - (open + 3));
		v = fy_parse(gb, input,
			     FYMM_YAML_PARSE_FLAGS | FYOPPF_INPUT_TYPE_STRING,
			     NULL);
		if (fy_is_mapping(v))
			seq = fy_append(gb, seq, v);

		for (p = open; p < close + 3; p++) {
			if (*p != '\n')
				*p = ' ';
		}
	}
	return seq;
}

/* Match an orientation keyword. TD is an accepted alias of TB. */
static const char *fymm_orientation_name(const struct fymm_token *t)
{
	if (fymm_token_ieq(t, "LR"))
		return "LR";
	if (fymm_token_ieq(t, "TB") || fymm_token_ieq(t, "TD"))
		return "TB";
	if (fymm_token_ieq(t, "BT"))
		return "BT";
	return NULL;
}

struct fymm_diagram *fymm_parse(const char *text, size_t len,
				const struct fymm_parse_cfg *cfg)
{
	struct fy_generic_builder_cfg gb_cfg;
	struct fymm_diagram *d;
	struct fymm_parser p;
	struct fymm_token tok;
	fy_generic frontmatter, directives, config, title, dir, init;
	const char *orientation, *o;
	char *work = NULL;

	if (!text)
		return NULL;
	if (len == FYMM_NT)
		len = strlen(text);

	d = malloc(sizeof(*d));
	if (!d)
		return NULL;
	memset(d, 0, sizeof(*d));

	memset(&gb_cfg, 0, sizeof(gb_cfg));
	gb_cfg.flags = FYGBCF_SCOPE_LEADER | FYGBCF_DEDUP_ENABLED;
	if (cfg && cfg->parent_gb) {
		gb_cfg.flags |= FYGBCF_CREATE_ALLOCATOR;
		gb_cfg.parent = cfg->parent_gb;
	}
	d->gb = fy_generic_builder_create(&gb_cfg);
	if (!d->gb) {
		free(d);
		return NULL;
	}
	d->own_gb = true;
	d->model = fy_invalid;
	d->diags = fy_seq_empty;
	d->filename = fy_gb_intern_string(d->gb,
			cfg && cfg->filename ? cfg->filename : "<stdin>");

	memset(&p, 0, sizeof(p));
	p.d = d;
	p.flags = cfg ? cfg->flags : 0;

	/* Remove the frontmatter and the directives from a scratch copy before
	 * the statement scanner reads it. */
	work = malloc(len + 1);
	if (!work)
		goto err_out;
	memcpy(work, text, len);
	work[len] = '\0';

	frontmatter = fymm_take_frontmatter(d->gb, work, len);
	directives = fymm_take_directives(d->gb, work, len);

	title = fy_get(frontmatter, "title");
	config = fy_map_empty;
	config = fymm_config_apply(d->gb, config,
				   fy_get(frontmatter, "config"), "gitGraph");
	fy_foreach(dir, directives) {
		init = fy_get(dir, "init");
		if (fy_is_invalid(init))
			init = fy_get(dir, "initialize");
		if (fy_is_invalid(init))
			init = dir;
		config = fymm_config_apply(d->gb, config, init, "gitGraph");
	}

	fymm_lex_init(&p.lex, work, len);
	memset(&tok, 0, sizeof(tok));

	/* The first statement line names the diagram. */
	while (fymm_lex_next_line(&p.lex)) {
		if (fymm_lex_token(&p.lex, &tok))
			break;
	}

	if (tok.type != FYMM_TOK_WORD) {
		fymm_diagf(&p, true, p.lex.line, 1,
			   "empty input: no diagram declaration found");
		goto out;
	}

	if (!fymm_token_ieq(&tok, "gitGraph")) {
		fymm_diagf(&p, true, tok.line, tok.col,
			   "unsupported diagram type '%.*s'",
			   (int)tok.len, tok.text);
		goto out;
	}

	d->type = FYMM_DT_GITGRAPH;
	orientation = "LR";
	if (fymm_lex_token(&p.lex, &tok) && tok.type == FYMM_TOK_WORD) {
		o = fymm_orientation_name(&tok);
		if (!o) {
			fymm_diagf(&p, true, tok.line, tok.col,
				   "expected an orientation (LR, TB or BT), got '%.*s'",
				   (int)tok.len, tok.text);
			goto out;
		}
		orientation = o;
		if (strcmp(orientation, "LR"))
			fymm_diagf(&p, false, tok.line, tok.col,
				   "the %s orientation is not implemented yet; rendering left to right",
				   orientation);
		fymm_lex_token(&p.lex, &tok);
	}
	if (tok.type != FYMM_TOK_EOL && tok.type != FYMM_TOK_COLON)
		fymm_diagf(&p, false, tok.line, tok.col,
			   "ignoring trailing text after the gitGraph header");

	fymm_parse_gitgraph(&p, config, orientation, title);

out:
	fymm_token_reset(&tok);
	free(work);
	if (fy_is_invalid(d->model))
		d->model = fy_mapping(d->gb,
			"type", fymm_diagram_type_name(d->type));
	return d;

err_out:
	fymm_diagram_destroy(d);
	return NULL;
}

struct fymm_diagram *fymm_parse_file(const char *path,
				     const struct fymm_parse_cfg *cfg)
{
	struct fymm_parse_cfg lcfg;
	struct fymm_diagram *d;
	size_t size, alloc, rd;
	char *buf, *nbuf;
	FILE *fp;

	if (!path)
		return NULL;

	if (!strcmp(path, "-")) {
		fp = stdin;
	} else {
		fp = fopen(path, "rb");
		if (!fp)
			return NULL;
	}

	alloc = 65536;
	size = 0;
	buf = malloc(alloc);
	if (!buf)
		goto err_close;

	while ((rd = fread(buf + size, 1, alloc - size, fp)) > 0) {
		size += rd;
		if (size < alloc)
			continue;
		alloc *= 2;
		nbuf = realloc(buf, alloc);
		if (!nbuf)
			goto err_free;
		buf = nbuf;
	}
	if (ferror(fp))
		goto err_free;

	if (cfg) {
		lcfg = *cfg;
	} else {
		memset(&lcfg, 0, sizeof(lcfg));
		lcfg.struct_size = sizeof(lcfg);
	}
	if (!lcfg.filename)
		lcfg.filename = !strcmp(path, "-") ? "<stdin>" : path;

	d = fymm_parse(buf, size, &lcfg);
	free(buf);
	if (fp != stdin)
		fclose(fp);
	return d;

err_free:
	free(buf);
err_close:
	if (fp != stdin)
		fclose(fp);
	return NULL;
}

void fymm_diagram_destroy(struct fymm_diagram *d)
{
	if (!d)
		return;
	if (d->gb && d->own_gb)
		fy_generic_builder_destroy(d->gb);
	free(d);
}
