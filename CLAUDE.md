# CLAUDE.md

Use this file when you change this repository.

## Project

`libfymermaid` renders mermaid diagrams on a terminal. It is a library with one
front-end tool, `fy-mermaid`. It has no daemon and no resident state. One call
parses one source and returns one diagram object.

The diagram model is a libfyaml generic. It is the public interface. A consumer
walks the model, re-emits it, or renders it with its own code.

### Architecture rules

- Keep the three layers separate: parse, canvas, render.
- Keep the canvas free of mermaid knowledge. It draws cells, lines, and text.
- Keep the renderer free of literal escape sequences. Use palette indices, and
  emit escapes only in `fymm_emit_sgr()`.
- Do not add a global styling state or a process-wide configuration.
- Add a diagram type as new model keys and a new renderer entry point. Do not
  add a parallel C structure hierarchy.

### Comments

Write comments in concise Technical English. State the required invariant,
ownership, or behavior. Do not narrate implementation history, restate the
code, address the reader, or use colloquial explanations. Remove a comment when
the code is self-explanatory.

## Data model

Use libfyaml generics as the native data model. Build values with `fy_mapping()`,
`fy_sequence()`, `fy_append()`, and `fy_assoc()`. Do not use the older
`fy_document` and `fy_node` API. `~/work/fyai` is the reference for correct use
of the generic API.

Use typed accessor defaults:

```c
fy_get(commit, "id", "")
fy_get(commit, "seq", 0LL)
fy_get(config, "showBranches", true)
fy_get(model, "commits", fy_invalid)
```

Prefer `fy_castp()` to `fy_cast()`. A short string can be in the `fy_generic`
word, so a pointer from `fy_cast(v, "")` can point into a local copy. Use
`fy_castp(&v, "")` at the use site.

Use the short generic API: `fy_is_*()`, `fy_empty()`, `fy_len()`, `fy_foreach()`,
`fy_foreach_key_value()`, `fy_foreach_idx_item()`, `fy_str()`, and
`fy_number()`. Use `fy_gb_intern_string()` for a stable `const char *`.

Every generic reachable from `fymm_diagram_model()` lives in the builder arena
of the diagram. It becomes invalid at `fymm_diagram_destroy()`.

### Model shape

Each diagram type produces a mapping with `type`, `config`, `title`,
`accTitle`, and `accDescr`. Each type adds its own keys. A gitGraph adds
`orientation`, `branches`, and `commits`.

Keep the model stable. It is the interface the tests pin and the interface a
consumer reads. Add keys; do not rename or remove them.

## Source layout

- `src/libfymermaid.c`: the diagram object, frontmatter, `%%{init}%%`
  directives, header dispatch, and diagnostics.
- `src/fymm-lex.c`: the line and token scanner.
- `src/fymm-gitgraph.c`: gitGraph statements and model construction.
- `src/fymm-canvas.c`: the cell grid, box-drawing junctions, UTF-8 width, and
  ANSI emission.
- `src/fymm-render.c`: terminal capability probes and the gitGraph renderer.
- `src/fymm-internal.h`: shared internal declarations.
- `include/libfymermaid.h`: the umbrella header.
- `include/libfymermaid/libfymermaid-{util,diagram,render}.h`: the public API.
- `fy-mermaid/`: the front-end tool and its manual page.
- `test/`: the API test, the golden driver, and the golden data.

## Diagnostics

Report through `fymm_diagf()`. A diagnostic has a level, a file, a line, a
column, and a message. The diagnostics are a generic sequence, so a consumer
places them without a text parse.

`fymm_parse()` returns a diagram for a source that has errors. Only allocation
failure returns NULL. The caller tests `fymm_diagram_has_errors()` before it
renders.

Report one diagnostic for one mistake. Do not let one error cascade. Skip the
statement and continue.

`FYMM_PF_STRICT` promotes each warning to an error.

## Mermaid conformance

`packages/mermaid/src/diagrams/git/gitGraph.spec.ts` in `mermaid-js/mermaid` is
the reference for gitGraph behavior. It asserts against the parsed data model,
which matches what this library produces.

Follow upstream where the behavior is observable. These cases are settled:

- A repeated commit id on `commit` is a warning. A repeated id on `merge` is an
  error.
- `merge` reports, in this order: an unknown branch, a merge of a branch with
  itself, no commit on the current branch, no commit on the merged branch, and
  two branches with the same head.
- A cherry-pick of a merge commit requires a `parent` attribute, and the parent
  must be an immediate parent of the merge.
- A commit with no `id:` gets its sequence number. Mermaid generates a random
  id. A deterministic id keeps a render reproducible and stays usable as a
  cherry-pick target. This is a deliberate difference; keep it.

State any other deliberate difference in this section and in `README.md`.

## Build and test

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

CMake is the only build system. There is no Makefile, no autotools, and no
interpreter in the test path.

### Tests

`test/gitgraph/` is the oracle. Each `<name>.mmd` runs against the golden files
beside it:

- `<name>.model`: the model, as YAML.
- `<name>.txt`: the Unicode render.
- `<name>.ascii`: the ASCII render.
- `<name>.diag`: the diagnostics, for a source that must fail.

A missing golden file removes that case. CTest registers one test for each pair
as `gitgraph/<name>/<mode>`. `test/fymm-api-test.c` holds the assertions that a
golden file cannot express: lane order, config layering, and one diagnostic for
one mistake.

Do not regenerate a golden file to make a change pass. A moved expectation is a
behavior change. State it in the commit message with a reason.

Verify that a test is not vacuous. Change a real value in the implementation
and confirm that the case fails before you trust it.

### Sanitizers

```sh
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DENABLE_ASAN=ON
cmake --build build-asan -j && ctest --test-dir build-asan
```

Run the ASAN suite before you complete work that allocates.

### Exported ABI

Only `fymm_`-prefixed symbols leave the shared object. The build uses
`-fvisibility=hidden`, explicit trailing `FYMM_EXPORT` tags, and
`-Wl,--exclude-libs,ALL`. Confirm this before you complete a change:

```sh
nm -D --defined-only build/src/libfymermaid.so.* \
  | awk '$2 ~ /^[A-Z]$/ {print $3}' | grep -v '^fymm_'
```

The output must be empty. A new public function needs the `FYMM_EXPORT` tag. A
new internal helper must not get one.

## Rendering

A line cell holds a direction mask, `FYMM_LN_*`, and not a glyph. A horizontal
run that crosses a vertical run resolves to a junction glyph without either
call knowing about the other. Add a line shape by extending the mask table.

A literal glyph, such as a commit or a label, is never overwritten by a line.
Column widths come from the label widths, which keeps a collision rare.

A lane band is three rows: a tag row, the lane row, and a label row. Blank rows
at the top and the bottom are removed at emission.

The canvas measures UTF-8 with its own tables. It does not use `wcwidth()` and
it does not depend on the locale.

## C style

Use Linux kernel C style:

- Use hard tabs with 8-column stops.
- Use kernel braces and spacing.
- Declare local variables at the start of the function.
- Do not declare variables inside branches or loops.
- Use `lower_snake_case` for C names.
- Use uppercase names for CMake options.
- Use four spaces in CMake files.
- Compile as GNU C2x with `-Wall -Wextra -Wsign-compare` and
  `-Wdeclaration-after-statement`.
- Add SPDX headers to new source files. A public header carries the full MIT
  text. A source file carries the short form.
- Avoid whitespace-only alignment changes.

Fix a warning; do not suppress it.

Do not put an operation inside an error-check predicate. Run the operation,
store its result, and then test the result.

Use ASD-STE100 Simplified Technical English for changed documentation, retained
comments, and commit messages.

## API style

- Use a bare forward-declared `struct fymm_diagram`. Do not use a typedef and
  do not use a `_t` suffix.
- A configuration structure starts with `size_t struct_size`. A NULL
  configuration selects each default.
- Return heap memory through `fymm_free()`, not `free()`.
- Tag each exported prototype with a trailing `FYMM_EXPORT`.

## Commits and patch series

Use an imperative commit subject with a subsystem prefix, for example:

```text
parse: report a repeated commit id as a warning
```

Subsystem prefixes: `parse:`, `render:`, `canvas:`, `api:`, `build:`, `test:`,
`doc:`.

Use two or three short body lines. State what changed and why. Wrap at 80
columns. End with exactly this trailer:

```text
Signed-off-by: Pantelis Antoniou <pantelis.antoniou@konsulko.com>
```

Do not add another attribution trailer.

Make each patch one logical change. Build each intermediate patch. Keep these
changes in separate patches and in this order:

1. implementation;
2. tests; and
3. documentation.

Fold a fix into the patch that introduced the defect. Do not add a later fixup
patch. Run the applicable tests after each test patch. Run the normal and the
ASAN suites on the final patch. Run `git diff --check` on each patch.

Stage named paths. Do not use `git add -A`.
