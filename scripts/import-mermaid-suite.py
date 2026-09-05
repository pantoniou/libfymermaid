#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
#
# Import the parser test corpus from mermaid-js/mermaid.
#
# Upstream tests its diagrams against the parsed data model, not against
# rendered SVG, so each `it()` case carries a diagram source and an expectation
# that the source parses or fails. This script extracts those two facts and
# writes them as a corpus this repository can run.
#
# The script is offline tooling. It is not part of the build and it is not part
# of the test path; the corpus it writes is committed.
#
#   git clone --depth 1 https://github.com/mermaid-js/mermaid ~/src/mermaid
#   scripts/import-mermaid-suite.py --checkout ~/src/mermaid
#
# Re-running the import is a corpus change. Commit it on its own, with the
# upstream commit recorded in test/mermaid-suite/PROVENANCE.

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import textwrap

BASE = 'packages/mermaid/src/diagrams'

# A spec file is useful here when it parses a diagram source. A renderer spec
# asserts over SVG geometry and a style spec asserts over CSS; neither says
# anything about a parse, so both are excluded by name.
EXCLUDE = re.compile(
    r'render|svgdraw|shapes|styles?\.spec|colorindex|sizing|font|fill|'
    r'picking|lifeline|notenode|noteedge|layout|palette|seed|integration',
    re.I)

# The suite name this repository uses for each upstream diagram directory.
# Anything not listed keeps its directory name.
RENAME = {
    'git': 'gitgraph',
    'user-journey': 'journey',
    'quadrant-chart': 'quadrant',
}

# A diagram source opens with the keyword that names its type. Some specs
# parse a fragment instead -- one member of a class, one label -- and those
# are not diagrams, so a case whose first line does not open with the
# keyword of its suite is not one either.
#
# The suite name is the keyword for most types; these are the exceptions.
KEYWORDS = {
    'flowchart': ('flowchart', 'graph', 'swimlane-beta'),
    'gitgraph': ('gitgraph',),
    'journey': ('journey',),
    'quadrant': ('quadrant',),
    'treeView': ('treeview', 'treemap'),
    'eventmodeling': ('eventmodeling', 'event-modeling'),
}


def looks_like_diagram(src, suite):
    """Does @src open with the diagram keyword its suite implies?"""
    for raw in src.splitlines():
        line = raw.strip()
        if not line or line.startswith('%%'):
            continue
        if line.startswith('---'):          # frontmatter; trust it
            return True
        head = line.lower()
        for kw in KEYWORDS.get(suite, (suite.lower(),)):
            if head.startswith(kw):
                return True
        return False
    return False

ESC = {'n': '\n', 't': '\t', 'r': '\r', '\\': '\\',
       '`': '`', "'": "'", '"': '"', '0': '\0'}


def read_str_arg(s, i):
    """Read a JS string expression at i: a template literal, a quoted string,
    or a `+` concatenation of them. Returns (text, next index). The text is
    None when the expression is not a literal, such as one that interpolates.
    """
    out = []
    while True:
        while i < len(s) and s[i] in ' \t\n\r':
            i += 1
        if i >= len(s) or s[i] not in '`\'"':
            break
        quote, i = s[i], i + 1
        buf = []
        while i < len(s):
            c = s[i]
            if c == '\\':
                buf.append(ESC.get(s[i + 1], '\\' + s[i + 1]))
                i += 2
                continue
            if c == quote:
                i += 1
                break
            if quote == '`' and c == '$' and s[i + 1:i + 2] == '{':
                return None, i
            buf.append(c)
            i += 1
        out.append(''.join(buf))
        j = i
        while j < len(s) and s[j] in ' \t\n\r':
            j += 1
        if j < len(s) and s[j] == '+':
            i = j + 1
            continue
        break
    return (''.join(out) if out else None), i


def strip_js_comments(text):
    """Remove JS comments, leaving string literals untouched.

    A spec may leave an assertion commented out beside the live one, and a
    `//` or `/* */` that still mentions toThrow would otherwise decide the
    outcome of a case that upstream expects to parse. A `//` inside a string
    is not a comment: a URL carries one.
    """
    out = []
    i, n = 0, len(text)
    quote = None
    while i < n:
        c = text[i]
        if quote:
            out.append(c)
            if c == '\\' and i + 1 < n:
                out.append(text[i + 1])
                i += 2
                continue
            if c == quote:
                quote = None
            i += 1
            continue
        if c in '\'"`':
            quote = c
            out.append(c)
            i += 1
            continue
        if c == '/' and i + 1 < n and text[i + 1] == '/':
            while i < n and text[i] != '\n':
                i += 1
            continue
        if c == '/' and i + 1 < n and text[i + 1] == '*':
            i += 2
            while i + 1 < n and not (text[i] == '*' and text[i + 1] == '/'):
                i += 1
            i += 2
            continue
        out.append(c)
        i += 1
    return ''.join(out)


def brace_block(s, i):
    """Return the text of the {...} block at or after i."""
    depth, start = 0, None
    while i < len(s):
        if s[i] == '{':
            depth += 1
            if start is None:
                start = i
        elif s[i] == '}':
            depth -= 1
            if depth == 0:
                return s[start:i + 1], i + 1
        i += 1
    return '', i


# The entry points the specs parse through.
PARSE_CALL = re.compile(r'\.parse\s*\(|Diagram\.fromText\s*\(')
# The forms that say the case must fail. A jison parser throws; a test either
# expects the rejection or asserts that the call did not return. `not.toThrow`
# and `resolves.not.toThrow` say the opposite, so they are removed first.
NOT_THROW = re.compile(r'\bnot\.toThrow\b')
FAILS = re.compile(r'rejects\.toThrow|\.toThrow\(|expect\(true\)\.toBe\(false\)')
# A third shape sets a flag in a catch and asserts it afterwards:
#     let error = false;
#     try { await parse(...) } catch (e) { error = true }
#     expect(error).toBe(true);
CAUGHT = re.compile(r'catch\s*\([^)]*\)\s*\{[^{}]*\b(\w+)\s*=\s*true')


def expects_failure(body):
    text = NOT_THROW.sub('', body)
    if FAILS.search(text):
        return True
    m = CAUGHT.search(text)
    return bool(m and re.search(r'expect\(\s*%s\s*\)\.toBe\(true\)'
                                % re.escape(m.group(1)), text))


def outcome_is_ambiguous(body):
    """Does the case assert both a success and a failure?

    Some `it()` bodies parse several sources and expect different things of
    each. Only the first source is extracted, so the outcome of the case is
    not the outcome of that source, and guessing would pin the wrong
    expectation.
    """
    return bool(NOT_THROW.search(body)) and expects_failure(body)


def extract(text):
    """Return (cases, skipped) for one spec file."""
    cases, skipped = [], []

    describes = []
    for m in re.finditer(r'\bdescribe\s*\(\s*([\'"`])(.*?)\1', text, re.S):
        body, _ = brace_block(text, m.end())
        describes.append((m.group(2), m.start(), m.start() + len(body)))

    for m in re.finditer(r'\bit\s*\(\s*([\'"`])(.*?)\1', text, re.S):
        name = m.group(2)
        body, _ = brace_block(text, m.end())
        body = strip_js_comments(body)
        context = [n for (n, a, b) in describes if a <= m.start() <= b]

        # Local bindings, so that `const str = ...; parser.parse(str)` resolves.
        binds = {}
        for b in re.finditer(r'\b(?:const|let|var)\s+([A-Za-z_$][\w$]*)\s*=', body):
            value, _ = read_str_arg(body, b.end())
            if value is not None:
                binds[b.group(1)] = value

        src = None
        for call in PARSE_CALL.finditer(body):
            literal, _ = read_str_arg(body, call.end())
            if literal is not None:
                src = literal
                break
            ident = re.match(r'\s*([A-Za-z_$][\w$]*)\s*\)', body[call.end():])
            if ident and ident.group(1) in binds:
                src = binds[ident.group(1)]
                break
        # Several suites parse through a helper, as in
        # `expect(parserFnConstructor(str)).not.toThrow()`. The source is
        # still the one string the case binds and then passes to a call.
        if src is None and len(binds) == 1:
            ident, value = next(iter(binds.items()))
            if re.search(r'\(\s*%s\s*[,)]' % re.escape(ident), body):
                src = value

        if src is not None and not src.strip():
            src = None                          # nothing was really extracted
        if src is None:
            why = ('no parse call' if not PARSE_CALL.search(body)
                   else 'source is not a literal')
            skipped.append({'name': name, 'reason': why})
            continue

        if outcome_is_ambiguous(body):
            skipped.append({'name': name,
                            'reason': 'the case asserts both outcomes'})
            continue

        cases.append({
            'suite': '/'.join(context),
            'name': name,
            'src': textwrap.dedent(src).strip('\n') + '\n',
            'outcome': 'fails' if expects_failure(body) else 'parses',
        })
    return cases, skipped


def slug(name, taken):
    s = re.sub(r'[^a-z0-9]+', '-', name.lower()).strip('-')[:60] or 'case'
    out, n = s, 2
    while out in taken:
        out, n = '%s-%d' % (s, n), n + 1
    taken.add(out)
    return out


def discover(root):
    """Group the parser spec files of a mermaid checkout by suite name."""
    base = os.path.join(root, BASE)
    suites = {}
    for entry in sorted(os.listdir(base)):
        d = os.path.join(base, entry)
        if not os.path.isdir(d):
            continue
        name = RENAME.get(entry, entry)
        for dirpath, _, files in os.walk(d):
            for f in sorted(files):
                if not re.search(r'\.spec\.[jt]s$', f) or EXCLUDE.search(f):
                    continue
                rel = os.path.relpath(os.path.join(dirpath, f), base)
                suites.setdefault(name, []).append(rel)
    return suites


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--checkout', required=True,
                    help='a mermaid checkout to import from')
    ap.add_argument('--out', default='test/mermaid-suite',
                    help='the corpus directory')
    ap.add_argument('--suite', action='append', help='import only these suites')
    args = ap.parse_args()

    root = args.checkout
    sha = subprocess.run(['git', '-C', root, 'rev-parse', 'HEAD'],
                         capture_output=True, text=True, check=True).stdout.strip()

    suites = discover(root)
    if args.suite:
        suites = {k: v for k, v in suites.items() if k in args.suite}

    shutil.rmtree(args.out, ignore_errors=True)
    os.makedirs(args.out)

    report, total, total_fail, total_skip = [], 0, 0, 0
    for name in sorted(suites):
        cases, skipped, taken = [], [], set()
        for path in suites[name]:
            text = open(os.path.join(root, BASE, path)).read()
            c, s = extract(text)
            for case in c:
                if looks_like_diagram(case['src'], name):
                    cases.append(case)
                else:
                    s.append({'name': case['name'],
                              'reason': 'not a %s source' % name})
            skipped.extend(s)
        if not cases:
            continue

        d = os.path.join(args.out, name)
        os.makedirs(d)
        entries = []
        for i, c in enumerate(cases):
            base = '%03d-%s' % (i, slug(c['name'], taken))
            with open(os.path.join(d, base + '.mmd'), 'w') as fp:
                fp.write(c['src'])
            entries.append({'file': base + '.mmd', 'outcome': c['outcome'],
                            'suite': c['suite'], 'name': c['name']})
        nfail = sum(1 for e in entries if e['outcome'] == 'fails')
        with open(os.path.join(d, 'manifest.json'), 'w') as fp:
            json.dump({'cases': entries, 'skipped': skipped}, fp, indent=1)
            fp.write('\n')
        # A flat list as well, so that CMake reads the corpus with
        # file(STRINGS) and needs no JSON parser.
        with open(os.path.join(d, 'manifest.list'), 'w') as fp:
            for e in entries:
                fp.write('%s:%s\n' % (e['outcome'], e['file']))
        total += len(entries)
        total_fail += nfail
        total_skip += len(skipped)
        report.append('%-14s %4d cases (%3d must fail), %3d skipped'
                      % (name, len(entries), nfail, len(skipped)))

    with open(os.path.join(args.out, 'PROVENANCE'), 'w') as fp:
        fp.write('Corpus imported from mermaid-js/mermaid by\n'
                 'scripts/import-mermaid-suite.py.\n\n'
                 'upstream: https://github.com/mermaid-js/mermaid\n'
                 'commit:   %s\n'
                 'licence:  MIT, see LICENSE in this directory\n\n'
                 'Each case is one `it()` from an upstream parser spec: the\n'
                 'diagram source it parses, and whether upstream expects that\n'
                 'parse to succeed or to fail. A case whose source is not a\n'
                 'literal, and one that never parses, is skipped and counted\n'
                 'in the manifest of its suite.\n\n'
                 'total: %d cases, %d of which must fail; %d skipped\n\n'
                 % (sha, total, total_fail, total_skip))
        for line in report:
            fp.write('  %s\n' % line)

    print('\n'.join(report))
    print('%-14s %4d cases (%3d must fail), %3d skipped'
          % ('TOTAL', total, total_fail, total_skip))
    print('\nupstream commit %s' % sha)


if __name__ == '__main__':
    sys.exit(main())
