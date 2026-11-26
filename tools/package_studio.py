"""waveod Studio as an app that needs no Python, zipped for a release: on Windows a folder with waveod-studio.exe,
on macOS waveod-studio.app (built on each system: PyInstaller does not cross-build).

    python tools/package_studio.py OUT.zip [--clonefit PATH] [--keep-ui]

1. the interface: studio/ui, npm ci + npm run build -> studio/waveod_studio/web (--keep-ui: the one built already);
2. the NAM -> Clone converter: tools/clonefit built by tools/wvbuild.py (clang++), or --clonefit an existing one;
3. PyInstaller: one folder (macOS: an .app), no console, the icon, the interface and the fonts as data, the
   converter beside the app's files (waveod_studio/clonefit.py finds it there);
4. the zip: waveod-studio/ with the app and LEIA-ME.txt (macOS: made by ditto, which keeps the bundle's links and
   permissions).

Needs Node.js (npm), PyInstaller and pywebview in this Python, and clang++ unless --clonefit; on macOS also Pillow
(the icon goes from .ico to .icns). The window is WebView2 on Windows (part of Windows 10 and 11) and WebKit on
macOS; the pedal's MIDI is winmm or CoreMIDI (usb/htmidi.py, usb/coremidi.py). Work files go to work/studio
(git-ignored).
"""
import argparse
import os
import shutil
import subprocess
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import wvbuild                                                      # noqa: E402

STUDIO = os.path.join(ROOT, 'studio')
UI = os.path.join(STUDIO, 'ui')
PKG = os.path.join(STUDIO, 'waveod_studio')
WORK = os.path.join(ROOT, 'work', 'studio')
NAME = 'waveod-studio'

MAC = sys.platform == 'darwin'

README = """waveod Studio {version}

The PC app of waveod, the open firmware for the Sonicake Matribox II Pro (QME-200).

Open waveod-studio.exe. No installation and no Python needed (Windows 10 or 11; the window uses Microsoft
Edge WebView2, which comes with Windows). Windows may warn that the app is from an unknown publisher (it is
not signed): More info > Run anyway.

Connect the pedal by USB with the waveod firmware installed; close the stock PC editor first, it holds the
pedal's MIDI port.

Source, firmware and documentation: https://github.com/zxmarcos/waveod
MIT licence; the parts from others keep their own terms (THIRD_PARTY.md in the repository).
"""

README_MAC = """waveod Studio {version} for macOS

The PC app of waveod, the open firmware for the Sonicake Matribox II Pro (QME-200).

Drag waveod-studio.app to Applications (or anywhere) and open it. No Python needed (macOS 11 or later, Apple
silicon). The app is not signed by a registered developer: the first time, right-click it > Open > Open (or allow
it in System Settings > Privacy & Security). If macOS says the app is damaged, it is the quarantine flag of the
download: run  xattr -dr com.apple.quarantine /path/to/waveod-studio.app  once.

Connect the pedal by USB with the waveod firmware installed; macOS sees it as a MIDI device (no driver).

Source, firmware and documentation: https://github.com/zxmarcos/waveod
MIT licence; the parts from others keep their own terms (THIRD_PARTY.md in the repository).
"""


def run(cmd, **kw):
    print('>', ' '.join(cmd), flush=True)
    subprocess.run(cmd, check=True, **kw)


def version():
    with open(os.path.join(ROOT, 'VERSION')) as f:
        return f.read().strip()


def build_ui():
    npm = shutil.which('npm.cmd') or shutil.which('npm')
    if not npm:
        sys.exit('npm não encontrado: instale o Node.js')
    run([npm, 'ci', '--no-audit', '--no-fund'], cwd=UI)
    run([npm, 'run', 'build'], cwd=UI)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('out', help='the zip to write')
    ap.add_argument('--clonefit', help='a converter already built (default: build it with tools/wvbuild.py)')
    ap.add_argument('--keep-ui', action='store_true', help='use the interface built already')
    a = ap.parse_args()
    if os.name != 'nt' and not MAC:
        sys.exit('o app é para Windows ou macOS')
    v = version()

    if not a.keep_ui:
        build_ui()
    if not os.path.exists(os.path.join(PKG, 'web', 'index.html')):
        sys.exit('a interface não está gerada (studio/waveod_studio/web)')
    clonefit = os.path.abspath(a.clonefit) if a.clonefit else wvbuild.clonefit(quiet=False)

    shutil.rmtree(WORK, ignore_errors=True)
    os.makedirs(WORK)
    sep = os.pathsep
    mac = ['--osx-bundle-identifier', 'waveod.studio'] if MAC else []
    run([sys.executable, '-m', 'PyInstaller', '--noconfirm', '--clean', '--windowed', '--name', NAME, *mac,
         '--icon', os.path.join(STUDIO, 'packaging', 'waveod-studio.ico'),
         '--paths', STUDIO,
         '--add-data', '%s%swaveod_studio/web' % (os.path.join(PKG, 'web'), sep),
         '--add-data', '%s%swaveod_studio/fonts' % (os.path.join(PKG, 'fonts'), sep),
         '--add-binary', '%s%s.' % (clonefit, sep),
         '--distpath', os.path.join(WORK, 'dist'), '--workpath', os.path.join(WORK, 'build'),
         '--specpath', WORK,
         os.path.join(STUDIO, 'packaging', 'launcher.py')])

    out = os.path.abspath(a.out)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    if MAC:                                     # waveod-studio/{waveod-studio.app, LEIA-ME.txt}
        stage = os.path.join(WORK, 'zip', NAME)
        os.makedirs(stage)
        os.rename(os.path.join(WORK, 'dist', NAME + '.app'), os.path.join(stage, NAME + '.app'))
        with open(os.path.join(stage, 'LEIA-ME.txt'), 'w', encoding='utf-8') as f:
            f.write(README_MAC.format(version='v' + v))
        if os.path.exists(out):
            os.remove(out)
        run(['ditto', '-c', '-k', '--sequesterRsrc', '--keepParent', stage, out])
        print('%s: waveod Studio v%s for macOS, %.1f MB' % (out, v, os.path.getsize(out) / 1e6))
        return
    app = os.path.join(WORK, 'dist', NAME)
    with open(os.path.join(app, 'LEIA-ME.txt'), 'w', encoding='utf-8', newline='\r\n') as f:
        f.write(README.format(version='v' + v))
    with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for base, _, files in os.walk(app):
            for name in files:
                full = os.path.join(base, name)
                z.write(full, os.path.join(NAME, os.path.relpath(full, app)))
    print('%s: waveod Studio v%s, %.1f MB' % (out, v, os.path.getsize(out) / 1e6))


if __name__ == '__main__':
    main()
