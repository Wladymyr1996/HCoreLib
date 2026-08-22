"""Class-level include graph for a Hatynka library, plus a cycle report.

Nodes are CLASSES (header stems), not directories: HFs/ alone holds HFs, HIFs
and HIFile, and keying by directory would report their normal facade-backend
shape as a cycle. This is the same granularity the .puml diagrams draw at.

Header edges and .cpp edges are kept apart on purpose: a cycle that exists only
through .cpp files never breaks a build, which is exactly what makes it the kind
that survives for years unnoticed.
"""
import os
import re
import sys
import collections

root = sys.argv[1]
skip = {'3rdParty', 'Tests', 'Docs', 'EtlProfile', 'build', 'build-tests',
        'WebUi', 'tools', '.git'}

# class name -> list of files that define or implement it
owner = {}
files = []
for dirpath, dirnames, filenames in os.walk(root):
    dirnames[:] = [d for d in dirnames if d not in skip]
    for f in filenames:
        if not f.endswith(('.hpp', '.cpp', '.h')):
            continue
        path = os.path.join(dirpath, f)
        files.append(path)
        if f.endswith(('.hpp', '.h')):
            owner.setdefault(os.path.splitext(f)[0], []).append(path)

known = set(owner)
inc = re.compile(r'#\s*include\s*[<"]([^">]+)[">]')
sep = re.compile(r'[^A-Za-z0-9_.]+')

hdr_edges = collections.defaultdict(set)
cpp_edges = collections.defaultdict(set)

for path in files:
    stem = os.path.splitext(os.path.basename(path))[0]
    if stem not in known:
        continue
    try:
        text = open(path, encoding='utf-8', errors='ignore').read()
    except OSError:
        continue
    target = hdr_edges if path.endswith(('.hpp', '.h')) else cpp_edges
    for m in inc.finditer(text):
        parts = [os.path.splitext(p)[0] for p in sep.split(m.group(1))]
        # The LAST recognised component is the header itself; earlier ones are
        # only the directory it lives in.
        hit = next((p for p in reversed(parts) if p in known), None)
        if hit is not None and hit != stem:
            target[stem].add(hit)


def cycles(edges):
    found, colour, stack = [], {}, []

    def visit(n):
        colour[n] = 1
        stack.append(n)
        for m in sorted(edges.get(n, ())):
            if colour.get(m, 0) == 0:
                visit(m)
            elif colour.get(m) == 1:
                found.append(stack[stack.index(m):] + [m])
        stack.pop()
        colour[n] = 2

    for n in sorted(known):
        if colour.get(n, 0) == 0:
            visit(n)
    return found


both = collections.defaultdict(set)
for d in (hdr_edges, cpp_edges):
    for k, v in d.items():
        both[k] |= v

print('--- {}: {} classes'.format(root, len(known)))

print('\nHEADER-ONLY cycles (these break builds):')
hc = cycles(hdr_edges)
print('  none' if not hc else '\n'.join('  ' + ' -> '.join(c) for c in hc))

print('\nCycles including .cpp edges (these do NOT break builds):')
cc = cycles(both)
print('  none' if not cc else '\n'.join('  ' + ' -> '.join(c) for c in cc))

if len(sys.argv) > 2 and sys.argv[2] == '-v':
    print('\nEdges per class   header | cpp-only:')
    for m in sorted(known):
        h = ','.join(sorted(hdr_edges.get(m, set()))) or '-'
        c = ','.join(sorted(cpp_edges.get(m, set()) - hdr_edges.get(m, set()))) or '-'
        print('  {:22} {}   |   {}'.format(m, h, c))
