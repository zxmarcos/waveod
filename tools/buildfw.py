"""Build the firmware of a git revision apart from the working tree, and keep its ELF beside the update file.

    python tools/buildfw.py [OUT.bin] [--rev HEAD] [--base BASE.bin]

OUT defaults to out/QME-200_waveod_grid.bin (what studio/waveod-studio-web-emu.bat runs); BASE to OUT itself when it
exists (its sections 1-3 are kept, tools/mkfw.py), else out/QME-200_waveod_grid.bin, else out/QME-200_waveod.bin
(an old image without the drum machine's sounds: the drum tests fail on it). Writes OUT and the ELF of the same
name (.elf): the emulator (studio/waveod_studio/emulated.py, tools/check_editor.py users) runs the ELF beside the
firmware, so the pair always matches. The revision is extracted into out/build/fw (git archive; third_party/lvgl
linked, not copied) and built there with CMake (preset fw, tools/wvbuild.py): uncommitted changes and build/ play
no part. A revision from before CMake (no CMakeLists.txt) is not built.
"""
import argparse
import io
import os
import shutil
import subprocess
import sys
import tarfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import wvbuild                                      # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(ROOT, 'out', 'build', 'fw')
KEEP = ('build',)                                   # kept between builds: the next one is quicker


def git(*a):
    return subprocess.run(['git', *a], cwd=ROOT, check=True, capture_output=True).stdout


def link_dir(link, target):
    """a directory link (a junction on Windows: no administrator rights needed)"""
    if sys.platform == 'win32':
        import _winapi
        _winapi.CreateJunction(target, link)
    else:
        os.symlink(target, link, target_is_directory=True)


def extract(rev):
    os.makedirs(BUILD, exist_ok=True)
    for name in os.listdir(BUILD):
        p = os.path.join(BUILD, name)
        if name in KEEP:
            continue
        if _is_junction(p):
            os.unlink(p)
        elif os.path.isdir(p):
            shutil.rmtree(p, onerror=_unlink_links)
        else:
            os.remove(p)
    with tarfile.open(fileobj=io.BytesIO(git('archive', '--format=tar', rev))) as t:
        t.extractall(BUILD, filter='data')
    lvgl = os.path.join(BUILD, 'third_party', 'lvgl')
    if os.path.isdir(lvgl) and not os.listdir(lvgl):
        os.rmdir(lvgl)
    if not os.path.exists(lvgl):
        os.makedirs(os.path.dirname(lvgl), exist_ok=True)
        link_dir(lvgl, os.path.join(ROOT, 'third_party', 'lvgl'))


def _is_junction(p):
    try:
        return bool(os.readlink(p))
    except (OSError, ValueError):
        return False


def _unlink_links(func, path, exc):
    """rmtree met a junction (lvgl): drop the link, never what it points at"""
    if _is_junction(path):
        os.unlink(path)
    else:
        raise exc[1]


def build(rev):
    """build the extracted revision; the ELF's path"""
    semver = open(os.path.join(BUILD, 'VERSION')).read().strip() if os.path.exists(os.path.join(BUILD, 'VERSION')) \
        else '0.0.0'
    # as cmake/version.cmake writes it: vX.Y.Z for the commit tagged so, vX.Y.Z+<hash> for any other
    tagged = 'v' + semver in git('tag', '--points-at', rev).decode().split()
    env = dict(os.environ, WAVEOD_VERSION='v%s' % semver if tagged else 'v%s+%s' % (semver, rev))
    if not os.path.exists(os.path.join(BUILD, 'CMakeLists.txt')):
        sys.exit('a revisão %s é anterior ao CMake: não há como compilá-la' % rev)
    c = wvbuild.cmake()
    if not os.path.exists(os.path.join(BUILD, 'build', 'fw', 'build.ninja')):
        subprocess.run([c, '--preset', 'fw'], cwd=BUILD, check=True, env=env)
    subprocess.run([c, '--build', '--preset', 'fw'], cwd=BUILD, check=True, env=env)
    return os.path.join(BUILD, 'build', 'fw', 'waveod.elf')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('out', nargs='?', default=os.path.join(ROOT, 'out', 'QME-200_waveod_grid.bin'))
    ap.add_argument('--rev', default='HEAD')
    ap.add_argument('--base')
    a = ap.parse_args()
    out = os.path.abspath(a.out)
    grid = os.path.join(ROOT, 'out', 'QME-200_waveod_grid.bin')
    base = a.base or next((b for b in (out, grid) if os.path.exists(b)), os.path.join(ROOT, 'out', 'QME-200_waveod.bin'))
    if not os.path.exists(base):
        sys.exit('base não encontrada: %s (passe --base um firmware do pedal)' % base)
    rev = git('rev-parse', '--short', a.rev).decode().strip()
    print('revisão %s em %s' % (rev, BUILD), flush=True)
    extract(rev)
    elf = build(rev)
    tmp = out + '.tmp'
    subprocess.run([sys.executable, os.path.join(BUILD, 'tools', 'mkfw.py'), base, tmp, '--elf', elf], check=True)
    os.replace(tmp, out)
    shutil.copyfile(elf, os.path.splitext(out)[0] + '.elf')
    print('%s (%d bytes) e %s: waveod %s' % (out, os.path.getsize(out), os.path.splitext(out)[0] + '.elf', rev))


if __name__ == '__main__':
    main()
