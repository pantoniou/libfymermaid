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
- `src/fymm-common.c`: the diagram type table, the accessibility statements,
  and the token and free text helpers every type shares.
- `src/fymm-<type>.c`: the statements and model of one diagram type.
- `src/fymm-render-<type>.c`: the renderer of one diagram type.
- `src/fymm-canvas.c`: the cell grid, box-drawing junctions, UTF-8 width, and
  ANSI emission.
- `src/fymm-render.c`: terminal capability probes and the render entry point.
- `src/fymm-layout.c`: the layered graph layout every graph type shares --
  ranking, back-edge marking and rank placement. Use it rather than writing a
  seventh copy.
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

Upstream tests its diagrams against the parsed data model, not against rendered
SVG. `test/mermaid-suite/` carries that corpus: one case for each upstream
`it()`, holding the diagram source and whether upstream expects it to parse or
to fail. `scripts/import-mermaid-suite.py` produces it from a mermaid checkout.
`test/mermaid-suite/PROVENANCE` records the upstream commit.

The importer is offline tooling. It is not part of the build and not part of
the test path. Re-running it is a corpus change: commit it on its own, with the
new upstream commit in `PROVENANCE`, and state which cases changed.

A corpus case is verbatim upstream input. Do not reformat one and do not strip
its trailing whitespace; one upstream case tests that whitespace. `.gitattributes`
exempts the corpus from the whitespace checks for this reason.

`FYMM_IMPLEMENTED_SUITES` in `test/CMakeLists.txt` lists the suites that must
pass. Every other suite is registered and disabled, so the corpus stays
countable and `ctest` stays green. Implementing a diagram type means adding its
suite to that list and making the whole suite pass.

A corpus case asserts the outcome of a parse and no more. It does not assert
the model. Use it to find a syntax the parser does not accept and a source it
accepts that it must reject. Pin behavior beyond that with a golden file or
with `test/fymm-api-test.c`.

A `fails` case in a suite that is not implemented passes for the wrong reason:
the diagram type is rejected before its syntax is read. Do not read a pass rate
for a disabled suite as coverage.

A diagram type declares its own separators in the ops table. Sequence ends a
statement at a `;` and opens a comment at a `#`; timeline carries both in its
text. Do not make either a global rule.

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

`test/mermaid-suite/` carries the upstream corpus; see Mermaid conformance.

Each test is one process that reads only its own arguments. It holds no shared
state and it writes no temporary file, so the suite runs under `ctest -j`.
Keep it that way. Confirm that a change does not break it:

```sh
ctest --test-dir build -j1  >/dev/null
ctest --test-dir build -j$(nproc)
```

Both runs must report the same result for each test.

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

`fymm_canvas_route_v()` draws a link that goes down, across and down again.
Use it rather than a pair of hline and vline calls: a run of one cell carries
no direction and draws nothing, which is how the corners went missing twice.

A solid line wins over a dashed one in a shared cell. A merge edge and a
cherry-pick edge run along the same lane, and a merge drawn dashed reports a
relationship that is not there.

A lane band is three rows: a tag row, the lane row, and a label row. Blank rows
at the top and the bottom are removed at emission.

The canvas measures UTF-8 with its own tables. It does not use `wcwidth()` and
it does not depend on the locale.

### Labels

Draw a label through `src/fymm-markdown.c`, never with `fymm_canvas_text()`
directly. `fymm_rich_text()` and `fymm_rich_measure()` are the one-row pair;
`fymm_rich_parse()` with `fymm_rich_draw_line()` is for a label that has a box
and may become several lines. Drawing a label with the plain canvas call
leaves `<br>` on the screen as markup, which is what they replaced.

Only a markdown string is formatted, and the parser records that: a label
written in backticks inside its quotes sets `markdown` in the model. A plain
label is never formatted, because a label may contain an asterisk.

The inline parsing goes through libfymd4c's `fymd_inline_*`. Do not hand-roll
a second one, and do not link md4c here: libfymd4c absorbs it and exports only
`fymd_*`, which is the interface this library depends on. A gap in the reading
of a label is fixed in libfymd4c and then used from here.

### Colour

A cell holds a palette index, never a colour and never an escape sequence.
`fymm_emit_sgr()` is the one place an escape is written. Add a colour by adding
a `enum fymm_palette` entry and its key in `fymm_palette_keys`, not by writing
an escape at a draw site.

A theme is a YAML file in `themes/`, embedded by `src/CMakeLists.txt` at
configure time and reachable through `fymm_theme_iterate()`. A theme is applied
over the built-in palette, so a file that sets one key keeps the rest. Adding a
theme is adding a file; the build finds it.

Colours are stored as 24-bit and reduced at emission to the palette the
terminal has, by redmean distance in `src/fymm-color.c`. Do not hand-pick a
per-depth value for a new colour; the reduction is the single path, so a theme
from a mermaid source degrades the same way a built-in one does.

`fymm_color_parse()` takes `#rgb`, `#rrggbb`, an xterm index and an ANSI name.
A bare number of up to three digits is an index, not a hex colour: `196` is
ambiguous otherwise.

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
