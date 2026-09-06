# Future work

What is known to be wrong or missing, with the evidence for it. Ordered by
what it costs against what it buys. An item here has been reproduced, not
guessed; where a claim is unverified it says so.

Fixing one of these is an ordinary patch series: implementation, tests,
documentation, in that order. See `CLAUDE.md`.

## 1. A C4 boundary is drawn beside what it holds

`Enterprise_Boundary(b0, "Bank")` holding two elements renders as

```
┌────────────┐   ╭──────────╮
│ Bank       │   │ Customer │
│ [boundary] │   │ [person] │
└────────────┘   ╰──────────╯
```

The boundary is a sibling box of the elements it contains, which states the
opposite of what the source says. This is the fault that composite states had
until `src/fymm-render-state.c` was fixed: a container drawn as a peer.

`src/fymm-frame.c` already does this work for a flowchart subgraph, an
agentflow flow and a composite state. Wiring `fymm-render-c4.c` to it is the
same shape of change: read the parent of each element, drop the containers
from the node list, and let the frame carry the label.

**This is a correctness fault, not a want of polish, and the machinery for it
is written and tested. Take it first.**

`README.md` also says a C4 boundary "becomes a heading over its members". It
does not; it becomes a box. Correct that with the code.

## 2. An architecture group is a heading

```
API ─────────
┌──────────┐
│ Database │
```

This one really is a heading, and it is documented as deliberate. The reason
given -- that character cells cannot nest a box inside a box and keep either
legible -- was written before frames existed and the flowchart has since
disproved it.

Either frame the group or rewrite the reason. Leaving both as they are keeps a
claim in `README.md` that the rest of the tree contradicts.

## 3. A sequence block has no bracket

```
alt credentials valid          │           │
       │           │           │           │
else invalid       │           │           │
```

`alt`, `else`, `loop`, `opt` and `par` are drawn as bare words in the left
gutter. Nothing shows where a block starts or ends, or that an `else` belongs
to the `alt` above it, so a diagram with two blocks cannot be read.

Mermaid draws a labelled box around the block. A sequence diagram is columns
and rows rather than ranks, so `fymm-frame.c` does not fit: the bracket runs
between two participant columns over a span of rows, and the drawing is new
rather than reused.

**The largest gain in legibility left. It is also the most work of the five.**

## 4. The Wardley map carries its own legend

`src/fymm-legend.c` holds the labels a drawing has no room for, and gitGraph
uses it. The Wardley map numbers its nodes and lists them beneath with code of
its own, written before the shared one existed.

One of the two has to go, and it is not the shared one. This removes code
rather than adding it.

## 5. Twelve types clip rather than close up

`flowchart`, `er`, `state` and `usecase` close up through the shared layered
layout, and `gitGraph` closes up its own columns. These do not, and are cut
instead:

`c4`, `class`, `eventmodeling`, `journey`, `kanban`, `mindmap`, `packet`,
`radar`, `railroad`, `sequence`, `timeline`, `wardley`.

Measured at eighty columns against a sixty column terminal, `class` wants
sixty-two cells and `eventmodeling` seventy-three. Those two are near enough
to fit. `sequence`, `kanban` and `mindmap` grow with their content and have no
bound at all.

A type that ranks its nodes gets this by adopting `fymm_layout_layered()`. A
type that does not -- `sequence` with its columns, `kanban` with its lists --
needs its own, which is why this is one item and not twelve.

## 6. The corpus leaves 758 upstream cases unimported

`test/mermaid-suite/PROVENANCE` records them. The reasons are in each suite's
`manifest.json`:

| cases | reason | worth having? |
| --- | --- | --- |
| 528 | no parse call | no: the case tests a JavaScript API, not a parse |
| 116 | source is not a literal | yes: a real source the extractor cannot resolve |
| 111 | not a `<type>` source | partly: a fragment, not a whole diagram |
| 2 | asserts both outcomes | no: there is no single expectation to import |

Two causes account for most of the 116, and both are contained changes to
`scripts/import-mermaid-suite.py`:

- **`describe.each` parameterisation.** All twenty-five C4 cases are
  `describe.each(['Boundary'])` with `${macroName}` inside a template literal,
  which `read_str_arg()` refuses. `c4System.spec.ts` has six rows, so
  substituting recovers more cases than it skips.
- **A helper wrapping the literal.** The forty flowchart comment cases are
  `parse(cleanupComments('graph TD;\n%% Comment\n A-->B;'))`. Unwrapping one
  level of `IDENT(<literal>)` recovers them, and the raw source with its
  comments tests more than the stripped one does.

The 111 fragments are mostly class members (`new ClassMember('+getTime()')`),
which `fymm-class.c` does parse; they need wrapping in a diagram, which is a
per-suite decision rather than one rule.

Do not chase the forty-four `er` cases. They are one axis swept by a
`forEach` over a string split at run time, and extracting them statically
needs a JavaScript interpreter. Write a few cases by hand for the rule they
test instead.

A re-import is its own commit, with the upstream hash in `PROVENANCE` and a
note of which cases changed.

## 7. The gitGraph renderer is in the wrong file

`CLAUDE.md` says `src/fymm-render-<type>.c` holds the renderer of one diagram
type. `fymm_render_gitgraph()` is in `src/fymm-render.c`, beside the render
entry point and the terminal probes.

This cost a real bug: a sweep that converted every renderer to
`fymm_canvas_create_cfg()` globbed `fymm-render-*.c` and missed it, so
gitGraph ignored the width limit until a later sweep caught it. Move it, in a
commit that moves and does not rewrite.

## Not planned

- **More Unicode.** `FYMM_CHARSET_RICH` covers the cases where the cell
  resolution was the limit. What is left is structural: a renderer knowing
  what the model already tells it. Adding glyphs will not fix a boundary drawn
  in the wrong place.
- **Emoji anywhere.** Width is not stable across terminals, and a column of
  error in a frame takes the frame apart. The same goes for anything that
  needs a patched font.
- **Nesting a box inside a box for C4 and architecture.** A frame is dashed
  and carries its title on its own edge; that is what makes it readable around
  boxes that have borders of their own. A second solid border is not.
