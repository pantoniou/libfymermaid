# libfymermaid

A mermaid diagram renderer for the terminal, in C, built on
[libfyaml](https://github.com/pantoniou/libfyaml).

```
$ fy-mermaid release.mmd
                                   [v1.0]
    main ●─────●──────┬───────────┬◎───────●
         init  second │           │m1      after
                      │           │
 develop              ╰●─────●────╯
                       dev1  dev2
```

## What it does

Parses mermaid sources into a **`fy_generic` model** and draws that model on a
character cell canvas with box drawing glyphs and ANSI colour, degrading to
seven bit ASCII and to no colour when the terminal or the caller asks for it.

The model is the API. `fymm_diagram_model()` hands back a plain libfyaml
generic mapping, so a consumer can walk the diagram, re-emit it as YAML or
JSON, or feed it to its own renderer without linking against any private
structure:

```yaml
type: gitGraph
orientation: LR
title: null
config: { showBranches: true, showCommitLabel: true, mainBranchName: main, ... }
branches:
- { name: main, index: 0, lane: 0, order: null, tip: 5 }
- { name: develop, index: 1, lane: 1, order: null, tip: 3 }
commits:
- { id: init, seq: 0, branch: 0, type: NORMAL, label: init, tags: [], parents: [], cherryFrom: -1 }
- ...
```

`fy-mermaid --dump-model` prints exactly that, which is also what the golden
test suite pins.

## Status

| diagram | corpus | notes |
| --- | --- | --- |
| `flowchart` / `graph` | 195/195 | layered top to bottom |
| `classDiagram` | 133/133 | boxed compartments, UML relation ends |
| `sequenceDiagram` | 130/130 | participant columns, arrows, notes, blocks |
| `stateDiagram` | 74/74 | composite states, pseudo-states, forks |
| `gitGraph` | 63/63 | `LR` orientation |
| `erDiagram` | 59/59 | attribute compartments, crow's foot ends |
| `xychart` | 57/57 | bars and stepped lines on a scaled axis |
| `usecase` | 52/52 | actors, use cases, system boundaries |
| `kanban` | 28/28 | columns of cards |
| `mindmap` | 26/26 | indented tree |
| `C4Context` and friends | 25/25 | ranked boxes, `$named` arguments kept |
| `block` | 24/24 | a grid, with spans |
| `quadrantChart` | 23/23 | plotted points on a divided square |
| `architecture` | 17/17 | services ranked, groups as headings |
| `packet` | 15/15 | bit fields in rows of thirty-two |
| `pie` | 13/13 | proportional bars |
| `timeline` | 11/11 | down the page, not across |
| `radar` | 10/10 | bars grouped by axis, not a polygon |
| `journey` | 7/7 | scores drawn as pips |
| `gantt` | 7/7 | day-resolved bars, `after`/`until` honoured |
| `agentflow`, `railroad`, `wardley`, `treeView`, `eventmodeling`, `info`, `ishikawa` | — | not yet; the header is reported as unsupported |

"corpus" is upstream mermaid's own parser suite; see Conformance.

Within `gitGraph`: `commit` (with `id`, `msg`, `tag`, `type`), `branch` (with
`order`), `checkout` / `switch`, `merge` (with `id`, `tag`), `cherry-pick`
(with `id`, `parent`, `tag`), `accTitle` / `accDescr`, `%%` comments, YAML
frontmatter, and `%%{init: ...}%%` directives. The config keys honoured are
`showBranches`, `showCommitLabel`, `mainBranchName`, `mainBranchOrder` and
`parallelCommits`; `rotateCommitLabel` is parsed and ignored, because a
terminal cannot rotate text.

Behaviour follows upstream mermaid's own `gitGraph.spec.ts`, including which
mistakes are errors and which are warnings: a repeated commit id warns, a
repeated merge id fails; a cherry-pick of a merge commit needs an immediate
`parent:`; a merge reports an unknown branch, a self merge, an empty current
branch, an empty merged branch and two branches with the same head.

Two deliberate differences:

- A gitGraph in `TB` or `BT`, and a flowchart in any direction but `TB`, parse
  and render with a warning in the direction that is implemented.
- A radar is drawn as bars grouped by axis rather than as a polygon, and a
  composite shape that mermaid nests -- a subgraph, a C4 boundary, an
  architecture group -- becomes a heading over its members. Character cells
  cannot nest a box inside a box and keep either legible.
- A commit written without an `id:` is labelled with its sequence number rather
  than with a random hash, so a diagram renders identically every time and the
  label stays usable as a `cherry-pick` target.

## Colour

Three depths, chosen automatically or with `--color`: 24-bit, the xterm-256
cube, and the sixteen ANSI colours. `auto` honours `NO_COLOR`,
`CLICOLOR_FORCE`, `isatty`, `COLORTERM` and `TERM`, in that order.

Themes are YAML, embedded into the library at build time:

```sh
fy-mermaid --list-themes
fy-mermaid --theme light graph.mmd
fy-mermaid --style my-colours.yaml graph.mmd    # applied over --theme
```

```yaml
colors:
  git0: "#3b8eea"          # git0..git7 cycle over the branches
  commitLabel: "#bdc3c7"
  tag: { color: "#ffd479", bold: true }
```

A value is `#rgb`, `#rrggbb`, an xterm palette index, or an ANSI colour name,
and is reduced to the nearest entry of whatever palette the terminal actually
has, so a theme survives on a 256- or 16-colour terminal. A theme is applied
over the built-in palette, so one that sets a single key keeps the rest; `mono`
uses that to carry the structure with bold and dim alone.

## Labels

A `<br>` breaks a label, in each of the spellings mermaid accepts (`<br>`,
`<br/>`, `<br />`, `</br>`). Where the diagram gives the label a box of its
own it becomes a second line; where the label is one row of a list — a
timeline event, a gantt task, an axis name — the lines are joined with a
space, because the row is the unit.

Mermaid's markdown strings are formatted. A label written in backticks inside
its quotes is read as CommonMark inline content and drawn with the terminal's
own attributes:

```
flowchart TB
    A["`The **cat** in _the_ hat`"] --> B["The dog<br/>in the hog"]
```

`**bold**` is bold, `_italic_` italic, `~~struck~~` struck through, `` `code` ``
reversed and a link underlined. A plain label is never formatted, because a
label is allowed to contain an asterisk.

The inline parsing goes through libfymd4c's `fymd_inline_*`, which reports a
text as runs carrying the attributes that apply to each. md4c stays absorbed
inside libfymd4c, so this library links libfymd4c and never md4c itself, and
the edge cases are CommonMark's rather than mine.

## Conformance

`test/mermaid-suite/` carries upstream mermaid's own parser corpus: 1144 cases
across 27 diagram types, each one an `it()` from an upstream spec, reduced to
the diagram source and whether upstream expects it to parse or to fail.
`scripts/import-mermaid-suite.py` regenerates it from a mermaid checkout, and
`test/mermaid-suite/PROVENANCE` records the upstream commit.

The implemented types pass 971 of 971. The remaining suites are registered and disabled
until their diagram type exists, so the corpus is countable and `ctest` stays
green; `ctest -L unimplemented -N` lists what is still missing. Set
`-DFYMM_IMPLEMENTED_SUITES=<list>` to run a suite that is not implemented yet
and measure the gap.

The corpus earns its keep. On gitGraph it found the unimplemented
`commit "message"` shorthand and four error-versus-warning decisions that
disagreed with upstream. On sequence it found twelve more: `;` as a statement
separator, `#` as a comment, `title:` with a colon, `autonumber` arguments,
`par_over`, the branching arrow forms, participant `@{}` metadata, and an
activation that closes nothing.

## Building

[![ci](https://github.com/pantoniou/libfymermaid/actions/workflows/ci.yml/badge.svg)](https://github.com/pantoniou/libfymermaid/actions/workflows/ci.yml)


```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Needs libfyaml with the generic API and libfymd4c (labels may carry markdown;
its `fymd_inline_*` reads them). `-DENABLE_ASAN=ON` for a sanitized build,
`-DBUILD_FYMERMAID_EXECUTABLE=OFF` for the library alone.

The suite is process-per-case and shares no state, so `ctest -j$(nproc)` is
safe and gives the same result as a serial run.

## Using it

```c
#include <libfymermaid.h>

struct fymm_diagram *d = fymm_parse(src, FYMM_NT, NULL);

if (fymm_diagram_has_errors(d)) {
	char *why = fymm_diagram_diagnostics_string(d);

	fputs(why, stderr);
	fymm_free(why);
} else {
	fymm_render_fp(d, NULL, stdout);
}
fymm_diagram_destroy(d);
```

Diagnostics are themselves a generic sequence (`fymm_diagram_diagnostics()`),
one mapping per complaint with `level`, `file`, `line`, `column` and `message`,
so a caller that wants to place them in an editor gutter does not have to
re-parse the human readable form.

## Licence

MIT. See `LICENSE`.
