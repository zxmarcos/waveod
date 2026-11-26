"""waveod's version: VERSION at the root is the one source; the Studio's package files follow it.

    python tools/version.py                 the version, and the string a build here would show on the pedal
    python tools/version.py bump X.Y.Z      VERSION, studio/pyproject.toml and studio/ui/package.json set to X.Y.Z
    python tools/version.py check           exit 1 when the Studio's files do not match VERSION

Semantic versioning: X.Y.Z with no prefix in the files, tags and the pedal's string as vX.Y.Z (cmake/version.cmake:
vX.Y.Z on the tagged commit, vX.Y.Z+<hash> on any other). Releasing: docs/releasing.md.
"""
import json
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VERSION = os.path.join(ROOT, 'VERSION')
PYPROJECT = os.path.join(ROOT, 'studio', 'pyproject.toml')
PACKAGE = os.path.join(ROOT, 'studio', 'ui', 'package.json')
LOCK = os.path.join(ROOT, 'studio', 'ui', 'package-lock.json')
SEMVER = re.compile(r'^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$')
PY_LINE = re.compile(r'^version = "([^"]*)"$', re.M)


def read(path):
    with open(path, encoding='utf-8', newline='') as f:
        return f.read()


def write(path, text):
    with open(path, 'w', encoding='utf-8', newline='') as f:
        f.write(text)


def current():
    return read(VERSION).strip()


def studio_versions():
    py = PY_LINE.search(read(PYPROJECT))
    return (py.group(1) if py else None), json.loads(read(PACKAGE)).get('version')


def build_string():
    """what cmake/version.cmake would write for this checkout"""
    v = 'v' + current()
    git = lambda *a: subprocess.run(['git', *a], cwd=ROOT, capture_output=True, text=True).stdout.strip()
    h = git('rev-parse', '--short', 'HEAD')
    if not h:
        return v + '+dev'
    dirty = bool(git('status', '--porcelain', '--untracked-files=no'))
    if v in git('tag', '--points-at', 'HEAD').split() and not dirty:
        return v
    return v + '+' + h + ('.dirty' if dirty else '')


def bump(new):
    if not SEMVER.match(new):
        sys.exit('versão inválida: %s (use X.Y.Z, sem o v)' % new)
    write(VERSION, new + '\n')
    text = read(PYPROJECT)
    write(PYPROJECT, PY_LINE.sub('version = "%s"' % new, text, count=1))
    text = read(PACKAGE)                    # only the version line, the file's layout kept
    write(PACKAGE, re.sub(r'("version"\s*:\s*")[^"]*(")', r'\g<1>%s\g<2>' % new, text, count=1))
    text = read(LOCK)                       # the lock's root package: its first two "version" lines
    write(LOCK, re.sub(r'("version"\s*:\s*")[^"]*(")', r'\g<1>%s\g<2>' % new, text, count=2))
    print('VERSION, studio/pyproject.toml, studio/ui/package.json e package-lock.json: %s' % new)
    print('depois: CHANGELOG.md, commit, git tag v%s (docs/releasing.md)' % new)


def main():
    args = sys.argv[1:]
    if not args:
        print(current(), '->', build_string())
    elif args[0] == 'bump' and len(args) == 2:
        bump(args[1])
    elif args[0] == 'check':
        v, (py, pkg) = current(), studio_versions()
        if py != v or pkg != v:
            sys.exit('VERSION %s, pyproject %s, package.json %s' % (v, py, pkg))
        print('ok', v)
    else:
        sys.exit(__doc__)


if __name__ == '__main__':
    main()
