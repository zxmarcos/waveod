"""A .nam converted into a capture of the original's own Clone format (docs/clone_fit.md): what the original's PC
editor does, here by tools/clonefit (built by `python tools/wvbuild.py clonefit`).

The model renders a 70 s reference DI (the converter's own, or the original editor's nam_input_wav.wav when it is
installed: its fit then follows the editor's) and the pedal's Clone model is fitted to it. Any .nam the official
NeuralAmpModelerCore loads converts: WaveNet A1 of any size, A2 / SlimmableContainer, LSTM, ConvNet. The result plays
on the pedal's Clone block (blocks/clone/clone.c, the original's algorithm), lighter than the NAM engine.
"""
import glob
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
EXE_NAME = 'clonefit.exe' if os.name == 'nt' else 'clonefit'
STORED = 0x2000                                 # what a Clone slot keeps of the capture
STEPS = 14                                      # render, fit start, saturation, 10 iterations, the last IR 2 pass


def exe():
    """the converter, or None when it is not built: WAVEOD_CLONEFIT, the one packed with the Windows app
    (tools/package_studio.py puts it beside the app's files), else the repository's build"""
    packed = os.path.join(getattr(sys, '_MEIPASS', ''), EXE_NAME) if getattr(sys, 'frozen', False) else None
    for p in (os.environ.get('WAVEOD_CLONEFIT'), packed, os.path.join(REPO, 'out', 'clonefit', EXE_NAME)):
        if p and os.path.isfile(p):
            return p
    return None


def editor_di():
    """the original editor's reference DI when it is installed (or named in WAVEOD_CLONE_DI), else None"""
    p = os.environ.get('WAVEOD_CLONE_DI')
    if p and os.path.isfile(p):
        return p
    tail = os.path.join('data', 'flutter_assets', 'assets', 'wavs', 'nam_input_wav.wav')
    roots = [os.environ.get(k) for k in ('ProgramFiles', 'ProgramFiles(x86)', 'LOCALAPPDATA')]
    for r in filter(None, roots):
        for pat in (os.path.join(r, '*', tail), os.path.join(r, 'Programs', '*', tail), os.path.join(r, '*', '*', tail)):
            found = sorted(glob.glob(pat))
            if found:
                return found[0]
    return None


def is_capture(data):
    """whether bytes are a capture of the original's format (the converter's header)"""
    return len(data) >= 0x88 and data[:4] == b'VTSI'


def convert(nam_path, progress=None, di=None):
    """the capture for a .nam (0x2288 bytes; a slot keeps the first 0x2000). progress(done, total) while it runs;
    di: a reference DI WAV, 'builtin' for the converter's own, None for the editor's when installed. Raises
    RuntimeError with the converter's message when the model does not load or the fit fails."""
    x = exe()
    if not x:
        raise RuntimeError('o conversor não foi encontrado (no código-fonte: rode  python tools/wvbuild.py clonefit)')
    di = di or editor_di() or 'builtin'
    fd, out = tempfile.mkstemp(suffix='.clo')
    os.close(fd)
    done, log = 0, []
    try:
        p = subprocess.Popen([x, nam_path, di, out], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True,
                             creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        for line in p.stderr:
            log.append(line.rstrip())
            if line.startswith(('rendering', 'fitting', 'saturation')) or 'iteration' in line or \
                    line.startswith('wrote'):
                done += 1
                if progress:
                    progress(min(done, STEPS), STEPS)
        if p.wait():
            msg = next((l for l in reversed(log) if l), 'falhou')
            raise RuntimeError('a conversão não deu certo: %s' % msg)
        data = open(out, 'rb').read()
    finally:
        try:
            os.remove(out)
        except OSError:
            pass
    if not is_capture(data):
        raise RuntimeError('a conversão não gerou uma captura do Clone')
    return data
