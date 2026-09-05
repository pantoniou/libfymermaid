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

| diagram | state |
| --- | --- |
| `gitGraph` | implemented, `LR` orientation |
| everything else | not yet; the header is reported as unsupported |

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

- `TB` and `BT` parse and render, with a warning, as `LR`.
- A commit written without an `id:` is labelled with its sequence number rather
  than with a random hash, so a diagram renders identically every time and the
  label stays usable as a `cherry-pick` target.

## Conformance

`test/mermaid-suite/` carries upstream mermaid's own parser corpus: 1271 cases
across 29 diagram types, each one an `it()` from an upstream spec, reduced to
the diagram source and whether upstream expects it to parse or to fail.
`scripts/import-mermaid-suite.py` regenerates it from a mermaid checkout, and
`test/mermaid-suite/PROVENANCE` records the upstream commit.

gitGraph passes 63 of 63. The remaining suites are registered and disabled
until their diagram type exists, so the corpus is countable and `ctest` stays
green; `ctest -L unimplemented -N` lists what is still missing. Set
`-DFYMM_IMPLEMENTED_SUITES=<list>` to run a suite that is not implemented yet
and measure the gap.

The corpus found real bugs on the first run, which is the point of carrying it:
the `commit "message"` shorthand was unimplemented, and four error-versus-
warning decisions disagreed with upstream.

## Building

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Needs libfyaml with the generic API. `-DENABLE_ASAN=ON` for a sanitized build,
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
