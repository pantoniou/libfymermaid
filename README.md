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
| `flowchart` / `graph` | 195/195 | layered, `TB` and `LR` both drawn |
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
| `agentflow` | 68/68 | the flowchart grammar with flows, connectors and agent shapes |
| `railroad` | 67/67 | four grammar notations, drawn as rails |
| `wardley` | 19/19 | evolution across, visibility up, names in a legend |
| `eventmodeling` | 7/7 | frames in the column their kind belongs to |
| `treeView` | 5/5 | an indentation tree, with comments beside entries |
| `info` | 4/4 | the library version |
| `ishikawa` | 3/3 | causes as ribs off a spine |

"corpus" is upstream mermaid's own parser suite; see Conformance.

`FUTURE.md` records what is known to be wrong or missing, with the evidence.

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

- A flowchart is drawn in the direction it asks for, `TB` or `LR`. `BT` and
  `RL` reverse an axis the renderer does not reverse, so they are drawn as
  `TB` and `LR` with a warning saying so. A gitGraph in `TB` or `BT` is drawn
  left to right, likewise with a warning.
- A radar is drawn as bars grouped by axis rather than as a polygon.
- A flowchart subgraph and an agentflow flow are framed, with the title on the
  top edge and a link that leaves the container crossing it. A C4 boundary and
  an architecture group still become a heading over their members: those two
  nest boxes that already carry their own borders, and a frame around them
  leaves neither legible.
- A Wardley map plots its nodes and numbers them, with the names in a legend
  beneath: at terminal resolution the names cannot sit on the plot without
  colliding. Its links are right-angled routes rather than straight lines.
- A commit written without an `id:` is labelled with its sequence number rather
  than with a random hash, so a diagram renders identically every time and the
  label stays usable as a `cherry-pick` target.

## Fitting the terminal

The width is a limit, not a hint. A diagram wider than it is closed up until it
fits and whatever is still over is clipped, so no line of the output goes over
the width. `fy-mermaid --fit` and `fymm_render_cfg.fit` choose between that
(`shrink`, the default), clipping at the natural size (`clip`), and emitting
the whole drawing however wide (`none`, for a pager that scrolls sideways).

Closing up reduces the gaps between the parts of the drawing, never the
content: a node keeps its label. An edge label that sits in a gap that has been
closed is cut and ends in an ellipsis, because the alternative is drawing it
over the node beside it.

```
$ fy-mermaid -w 60 --fit clip     three of six nodes, the rest off the right
$ fy-mermaid -w 60 --fit shrink   all six, in 58 columns
```

Closing up only goes so far. A gitGraph column is as wide as the message it
carries, so a graph of long messages cannot be closed up enough to fit a narrow
terminal without the messages becoming unreadable. `--fit legend` moves them
out instead, leaving a numbered marker in the colour of its legend entry:

```
$ fy-mermaid -w 44 --fit legend         (natural width: 131 columns)

    main *-----*-----+------+o------*
         1     2     |      |4      5
                     |      |
 feature             `*-----'
                      3
1 import the parser corpus
2 fix the redmean reduction
3 add the legend helper
4 merge the legend work
5 document the fit policy
```

Only `gitGraph` does this so far. Every other type treats `legend` as `shrink`.

**How far shrinking reaches.** `flowchart`, `er`, `state` and `usecase` close
up through the shared layered layout; `gitGraph` closes up its own columns.
`agentflow`, `architecture`, `block`, `gantt`, `info`, `ishikawa`, `pie`,
`quadrant`, `treeView` and `xychart` already size themselves to the width and
need no closing up. The rest -- `c4`, `class`, `eventmodeling`, `journey`,
`kanban`, `mindmap`, `packet`, `radar`, `railroad`, `sequence`, `timeline` and
`wardley` -- can overflow a narrow terminal and are clipped rather than closed
up.

## Spacing

`fymm_render_cfg.metrics` sets the spacing a diagram is drawn with. A field
left at zero keeps the default for the diagram type, so changing one thing
needs nothing else:

```c
struct fymm_metrics met = FYMM_METRICS_INIT;

met.margin = 2;             /* an indent and blank rows around the drawing */
met.max_height = 40;
rcfg.metrics = &met;        /* the gaps stay whatever this type uses */
```

`fymm_metrics_default()` reports what a type is drawn with, when you want to
read the values rather than replace them:

```c
struct fymm_metrics met;

fymm_metrics_default(&met, FYMM_DT_FLOWCHART);      /* col_gap 2, rank_gap 2 */
```

`margin` is drawn at emission rather than by each renderer, and counts against
what the drawing may occupy. `max_width` and `max_height` bound it; with
`--fit none` neither applies, because that asks for the whole drawing.

`fymm_measure()` answers the cells a render would take, under the same
configuration and so under the same fit policy. It measures by rendering, so
it costs what a render costs.

## Charsets

Three levels, plus a probe. `--charset ascii` is seven bit; `unicode` is the
box drawing and geometric shapes, and what `auto` settles on when the locale
says UTF-8.

`--charset rich` adds what asks more of the font than the box drawing does: a
line series is plotted on braille, at twice the horizontal and four times the
vertical resolution of the cells, and a decision takes the heavy border weight
so it stops looking like a gateway.

```
  unicode                        rich
100 │                       100 │                  ⢀⡀
    │              ╭──●          │             ⣀⠤⠒⠒⠉⠁
    │         ╭──● ╯             │         ⣀⠤⠒⠉
    │    ╭──● ╯                  │      ⡠⠊
    │  ● ╯                       │  ⢀⠔⠁
```

`auto` never selects it: probing the locale says the terminal speaks UTF-8, not
which glyphs its font carries. It is what a caller asks for when it knows.

## Colour

Three depths, chosen automatically or with `--color`: 24-bit, the xterm-256
cube, and the sixteen ANSI colours. `auto` honours `NO_COLOR`,
`CLICOLOR_FORCE`, `isatty`, `COLORTERM` and `TERM`, in that order. A
`-direct` terminfo entry (`xterm-direct`, `tmux-direct`) is read as 24-bit,
which is what it means.

On a 24-bit terminal a colour reaches it byte for byte; the reduction to a
palette only happens where the terminal cannot do better.

`TERM` does not describe every terminal — kitty is `xterm-kitty`, ghostty
`xterm-ghostty`, and neither says anything about colour — so the probe also
knows those names, the `TERM_PROGRAM` values, and the variables a terminal
sets for itself (`KITTY_WINDOW_ID`, `GHOSTTY_RESOURCES_DIR`, `WEZTERM_PANE`
and the rest). Without that they fell back to sixteen colours.

### Light and dark

A palette chosen for a dark terminal washes out on a light one, so the
background is detected and a light terminal takes the `light` theme:

```sh
fy-mermaid --background light graph.mmd    # or dark, or auto
FYMM_BACKGROUND=light fy-mermaid graph.mmd
```

`auto` reads `$FYMM_BACKGROUND`, then `$COLORFGBG`, and then asks the terminal
itself with an OSC 11 query. The query goes to `/dev/tty` rather than to the
output, so a redirected render never has escape bytes in it, and it is polled
with a short timeout so a terminal that does not answer cannot hang anything.
Naming a `--theme` says what you want, and stands.

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

Mermaid's own `themeVariables` are honoured, so a diagram that sets its
colours keeps them:

```
%%{init: {'themeVariables': {'git0': '#ff0000', 'tagLabelColor': '#ffcc00'}}}%%
```

Mermaid names its colours per diagram — `git0` for a gitGraph branch, `pie1`
for a slice, `cScale0` elsewhere — and all of them land on the same eight
series colours here. The layers apply in order: the built-in palette, then
`--theme`, then the diagram's own variables, then `--style`, so whoever runs
the tool has the last word.

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
