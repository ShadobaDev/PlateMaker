#!/usr/bin/env python3
"""Print one version's release notes from the changelog.

    release_notes.py VERSION [CHANGELOG]

The notes are the section from the `## [VERSION] — date` heading to the next `## [` heading, the heading
itself left out (a GitHub Release's title is its tag). The changelog is wrapped at a fixed width, and a
Release's body breaks a line wherever the text does, so each paragraph, list item and quote is joined
back into one line. Exits 1 when the changelog has no section for VERSION, or an empty one.
"""
import re
import sys

BLOCK_START = re.compile(r'(#|\||[-*+] |\d+[.)] |```)')


def section(text, version):
    lines, inside = [], False
    for line in text.splitlines():
        if line.startswith('## ['):
            inside = line.startswith(f'## [{version}]')
            continue
        if inside:
            lines.append(line)
    return lines


def unwrap(lines):
    out, fence = [], False
    for line in lines:
        s = line.strip()
        if s.startswith('```'):
            fence = not fence
        prev = out[-1].strip() if out else ''
        quoted = s.startswith('>')
        joins = (not fence and s and prev and s != '>' and prev != '>'
                 and not prev.startswith(('#', '|', '```'))
                 and not BLOCK_START.match(s.lstrip('> ') if quoted else s)
                 and quoted == prev.startswith('>'))
        if joins:
            out[-1] = out[-1].rstrip() + ' ' + (s[1:].strip() if quoted else s)
        else:
            out.append(line)
    return out


def main():
    version = sys.argv[1]
    path = sys.argv[2] if len(sys.argv) > 2 else 'docs/CHANGELOG.md'
    notes = '\n'.join(unwrap(section(open(path, encoding='utf-8').read(), version))).strip()
    if not notes:
        sys.exit(f'{path} has no release notes for {version}')
    sys.stdout.reconfigure(encoding='utf-8')
    print(notes)


if __name__ == '__main__':
    main()
