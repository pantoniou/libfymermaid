/*
 * fymm-golden-test.c - compare a rendered diagram against a golden file
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

/*
 * Driven by ctest, one case per input and mode:
 *
 *	fymm-golden-test <mode> <input.mmd> [expected]
 *
 * The golden modes are `model', `unicode', `ascii' and `diag'. Each compares
 * the output against @expected. The corpus modes are `parses' and `fails',
 * which take no expected file and assert only the outcome of the parse; they
 * run the cases imported from upstream mermaid.
 *
 * There is no interpreter and no subprocess in the loop. The driver links the
 * library and compares the bytes it produced, so a failure names the code.
 * Each run is independent and reads only its own arguments, so ctest runs the
 * suite in parallel.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include <libfymermaid.h>

/* the width every golden is rendered at; see the render modes below */
#define FYMM_GOLDEN_WIDTH 80

static char *read_file(const char *path, size_t *lenp)
{
	size_t size = 0, alloc = 65536, rd;
	char *buf, *nbuf;
	FILE *fp;

	fp = fopen(path, "rb");
	if (!fp)
		return NULL;
	buf = malloc(alloc);
	if (!buf) {
		fclose(fp);
		return NULL;
	}
	while ((rd = fread(buf + size, 1, alloc - size, fp)) > 0) {
		size += rd;
		if (size < alloc)
			continue;
		alloc *= 2;
		nbuf = realloc(buf, alloc);
		if (!nbuf) {
			free(buf);
			fclose(fp);
			return NULL;
		}
		buf = nbuf;
	}
	fclose(fp);
	buf[size] = '\0';
	if (lenp)
		*lenp = size;
	return buf;
}

/* Report the first line that differs, which is what a reader needs. */
static int compare(const char *got, const char *want)
{
	const char *g = got, *w = want;
	int line = 1;

	while (*g && *w) {
		const char *ge = strchr(g, '\n');
		const char *we = strchr(w, '\n');
		size_t gl = ge ? (size_t)(ge - g) : strlen(g);
		size_t wl = we ? (size_t)(we - w) : strlen(w);

		if (gl != wl || memcmp(g, w, gl)) {
			fprintf(stderr, "mismatch on line %d\n"
					"  want: %.*s\n"
					"  got : %.*s\n",
				line, (int)wl, w, (int)gl, g);
			return -1;
		}
		if (!ge || !we)
			break;
		g = ge + 1;
		w = we + 1;
		line++;
	}
	if (*g || *w) {
		fprintf(stderr, "output length differs at line %d\n", line);
		return -1;
	}
	return 0;
}

int main(int argc, char *argv[])
{
	struct fymm_render_cfg rcfg;
	struct fymm_parse_cfg pcfg;
	struct fymm_diagram *d;
	char *src, *want, *got, *diags;
	const char *mode;
	bool corpus, errors;
	int rc = 1;

	if (argc < 3 || argc > 4) {
		fprintf(stderr,
			"usage: %s <model|unicode|ascii|diag> <input> <expected>\n"
			"       %s <parses|fails> <input>\n", argv[0], argv[0]);
		return 2;
	}
	mode = argv[1];
	corpus = !strcmp(mode, "parses") || !strcmp(mode, "fails");
	if (corpus != (argc == 3)) {
		fprintf(stderr, "mode '%s' takes %s expected file\n", mode,
			corpus ? "no" : "an");
		return 2;
	}

	src = read_file(argv[2], NULL);
	if (!src) {
		fprintf(stderr, "cannot read %s\n", argv[2]);
		return 2;
	}
	want = NULL;
	if (!corpus) {
		want = read_file(argv[3], NULL);
		if (!want) {
			fprintf(stderr, "cannot read %s\n", argv[3]);
			free(src);
			return 2;
		}
	}

	memset(&pcfg, 0, sizeof(pcfg));
	pcfg.struct_size = sizeof(pcfg);
	pcfg.filename = "input.mmd";

	d = fymm_parse(src, FYMM_NT, &pcfg);
	if (!d) {
		fprintf(stderr, "parse failed outright\n");
		goto out;
	}

	/*
	 * A corpus case carries one expectation: upstream mermaid accepts the
	 * source, or upstream rejects it. Compare that and stop.
	 */
	if (corpus) {
		errors = fymm_diagram_has_errors(d);
		if (errors == !strcmp(mode, "fails")) {
			rc = 0;
		} else if (errors) {
			diags = fymm_diagram_diagnostics_string(d);
			fprintf(stderr, "expected this to parse, but:\n%s",
				diags ? diags : "");
			free(diags);
		} else {
			fprintf(stderr, "expected this to fail, but it parsed\n");
		}
		goto out;
	}

	if (!strcmp(mode, "diag")) {
		diags = fymm_diagram_diagnostics_string(d);
		got = diags ? diags : strdup("");
	} else if (fymm_diagram_has_errors(d)) {
		diags = fymm_diagram_diagnostics_string(d);
		fprintf(stderr, "unexpected errors:\n%s", diags ? diags : "");
		free(diags);
		goto out;
	} else if (!strcmp(mode, "model")) {
		got = fymm_diagram_model_to_yaml(d, false);
	} else {
		fymm_render_cfg_default(&rcfg);
		rcfg.color = FYMM_COLOR_NONE;
		/* a golden must not depend on the terminal it runs in, and
		 * FYMM_WIDTH_AUTO would read one */
		rcfg.width = FYMM_GOLDEN_WIDTH;
		rcfg.charset = !strcmp(mode, "ascii") ? FYMM_CHARSET_ASCII :
							FYMM_CHARSET_UNICODE;
		got = fymm_render(d, &rcfg);
	}

	if (!got) {
		fprintf(stderr, "no output produced for mode '%s'\n", mode);
		goto out;
	}
	rc = compare(got, want) ? 1 : 0;
	fymm_free(got);

out:
	fymm_diagram_destroy(d);
	free(src);
	free(want);
	return rc;
}
