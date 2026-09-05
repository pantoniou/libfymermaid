/*
 * libfymermaid-diagram.h - parsing mermaid source into a generic diagram model
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

#ifndef LIBFYMERMAID_DIAGRAM_H
#define LIBFYMERMAID_DIAGRAM_H

#include <stdbool.h>
#include <stddef.h>

#include <libfyaml.h>
#include <libfyaml/libfyaml-generic.h>

#include <libfymermaid/libfymermaid-util.h>

#ifdef __cplusplus
extern "C" {
#endif

/* FYMM_NT - pass as a length to mean "the text is NUL terminated" */
#define FYMM_NT ((size_t)-1)

/**
 * enum fymm_diagram_type - the mermaid diagram kinds this library knows about
 *
 * @FYMM_DT_UNKNOWN: the header did not name a diagram we can parse
 * @FYMM_DT_GITGRAPH: a gitGraph diagram
 * @FYMM_DT_PIE: a pie chart
 * @FYMM_DT_TIMELINE: a timeline
 * @FYMM_DT_JOURNEY: a user journey
 * @FYMM_DT_MINDMAP: a mindmap
 * @FYMM_DT_SEQUENCE: a sequence diagram
 * @FYMM_DT_FLOWCHART: a flowchart, also spelled `graph`
 * @FYMM_DT_CLASS: a class diagram
 * @FYMM_DT_QUADRANT: a quadrant chart
 * @FYMM_DT_XYCHART: an xy chart of bars and lines
 */
enum fymm_diagram_type {
	FYMM_DT_UNKNOWN = 0,
	FYMM_DT_GITGRAPH,
	FYMM_DT_PIE,
	FYMM_DT_TIMELINE,
	FYMM_DT_JOURNEY,
	FYMM_DT_MINDMAP,
	FYMM_DT_SEQUENCE,
	FYMM_DT_FLOWCHART,
	FYMM_DT_CLASS,
	FYMM_DT_QUADRANT,
	FYMM_DT_XYCHART,
};

/* the opaque parsed diagram */
struct fymm_diagram;

/**
 * enum fymm_parse_flags - knobs for fymm_parse()
 *
 * @FYMM_PF_STRICT: treat warnings as errors
 */
enum fymm_parse_flags {
	FYMM_PF_STRICT = 1U << 0,
};

/**
 * struct fymm_parse_cfg - configuration for a parse
 *
 * @struct_size: sizeof(struct fymm_parse_cfg), the forward compatibility guard
 * @filename: the name reported in diagnostics; NULL means "<stdin>"
 * @parent_gb: an optional builder to parent the diagram's own builder to
 * @flags: a mask of enum fymm_parse_flags
 *
 * A NULL cfg selects the defaults for every field.
 */
struct fymm_parse_cfg {
	size_t struct_size;
	const char *filename;
	struct fy_generic_builder *parent_gb;
	unsigned int flags;
};

/**
 * fymm_parse() - parse mermaid source into a diagram
 *
 * The diagram is returned even when the source has errors, so that the
 * caller can retrieve the diagnostics; only an out of memory condition
 * yields NULL.  Use fymm_diagram_has_errors() before rendering.
 *
 * @text: the mermaid source
 * @len: its length, or FYMM_NT when @text is NUL terminated
 * @cfg: the parse configuration, or NULL for the defaults
 *
 * Returns:
 * The diagram, to be released with fymm_diagram_destroy(), or NULL.
 */
struct fymm_diagram *
fymm_parse(const char *text, size_t len, const struct fymm_parse_cfg *cfg)
	FYMM_EXPORT;

/**
 * fymm_parse_file() - parse mermaid source read from a file
 *
 * @path: the file to read; "-" means standard input
 * @cfg: the parse configuration, or NULL for the defaults
 *
 * Returns:
 * The diagram, to be released with fymm_diagram_destroy(), or NULL.
 */
struct fymm_diagram *
fymm_parse_file(const char *path, const struct fymm_parse_cfg *cfg)
	FYMM_EXPORT;

/* fymm_diagram_destroy() - release a diagram and everything it owns */
void
fymm_diagram_destroy(struct fymm_diagram *d)
	FYMM_EXPORT;

/* fymm_diagram_type() - the kind of diagram that was parsed */
enum fymm_diagram_type
fymm_diagram_type(const struct fymm_diagram *d)
	FYMM_EXPORT;

/* fymm_diagram_type_name() - the mermaid keyword for @type, e.g. "gitGraph" */
const char *
fymm_diagram_type_name(enum fymm_diagram_type type)
	FYMM_EXPORT;

/**
 * fymm_diagram_model() - the diagram as a generic value
 *
 * The model is a mapping; every diagram kind carries `type`, `config` and
 * `title`, and each adds its own keys (a gitGraph adds `orientation`,
 * `branches` and `commits`).  It stays valid until fymm_diagram_destroy().
 *
 * Returns:
 * The model mapping, or fy_invalid when nothing was parsed.
 */
fy_generic
fymm_diagram_model(const struct fymm_diagram *d)
	FYMM_EXPORT;

/**
 * fymm_diagram_diagnostics() - the diagnostics produced by the parse
 *
 * A sequence of mappings, each with `level` ("error" or "warning"), `line`,
 * `column`, `message` and, when there was one, the offending source `text`.
 *
 * Returns:
 * The sequence, empty when the parse was clean.
 */
fy_generic
fymm_diagram_diagnostics(const struct fymm_diagram *d)
	FYMM_EXPORT;

/* fymm_diagram_has_errors() - true when the parse produced any error */
bool
fymm_diagram_has_errors(const struct fymm_diagram *d)
	FYMM_EXPORT;

/**
 * fymm_diagram_diagnostics_string() - the diagnostics as human readable text
 *
 * One `file:line:col: level: message` line per diagnostic.
 *
 * Returns:
 * A string to release with fymm_free(), or NULL when there are none.
 */
char *
fymm_diagram_diagnostics_string(const struct fymm_diagram *d)
	FYMM_EXPORT;

/**
 * fymm_diagram_model_to_yaml() - the model emitted as YAML
 *
 * Mostly useful for tests and for `fy-mermaid --dump-model`.
 *
 * @d: the diagram
 * @flow: emit in flow (JSON-ish) style rather than block style
 *
 * Returns:
 * A string to release with fymm_free(), or NULL on error.
 */
char *
fymm_diagram_model_to_yaml(const struct fymm_diagram *d, bool flow)
	FYMM_EXPORT;

/**
 * fymm_diagram_builder() - the generic builder backing the diagram
 *
 * Everything reachable from fymm_diagram_model() lives in this builder's
 * arena.  Useful when a caller wants to derive further generics with the
 * same lifetime as the diagram.
 */
struct fy_generic_builder *
fymm_diagram_builder(const struct fymm_diagram *d)
	FYMM_EXPORT;

#ifdef __cplusplus
}
#endif

#endif /* LIBFYMERMAID_DIAGRAM_H */
