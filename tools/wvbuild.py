"""Where waveod's builds are and how the tools make them (CMakeLists.txt, CMakePresets.json): one place for the paths
and the toolchains.

    build/fw/waveod.elf          the firmware (preset fw: Arm Toolchain for Embedded)
    build/host/waveod-tests.exe  the unit tests, build/host/waveod-sim.exe the simulator (preset host: clang)

Found in this order:
    CMake      WAVEOD_CMAKE, cmake on the PATH, the one Visual Studio installs
    ATfE       ATFE_ROOT, D:/DevTools/ATfE-23.1.0-Windows-x86_64 (the firmware and the modules, tools/wvfx.py)
    host clang WAVEOD_HOST_CC, the one Visual Studio installs (component "C++ Clang Compiler for Windows"), clang on
               the PATH (the PC programs the checks compile: tools/nam_host.c, tools/circuits/*.c)
    host C++   WAVEOD_HOST_CXX, Visual Studio's clang++, clang++ on the PATH (tools/clonefit, waveod Studio's
               NAM -> Clone converter: out/clonefit/clonefit.exe)

    python tools/wvbuild.py fw|host [TARGET]      configure if needed, then build
    python tools/wvbuild.py fw-locktest           build/fw-locktest/waveod.elf: the firmware with the flash lock
                                                  test command (WAVEOD_LOCK_TEST), never for a release
    python tools/wvbuild.py clonefit              the converter, built if older than its sources
"""
import glob
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(ROOT, 'build')
EXE = '.exe' if os.name == 'nt' else ''
FW_ELF = os.path.join(BUILD, 'fw', 'waveod.elf')
TESTS = os.path.join(BUILD, 'host', 'waveod-tests' + EXE)
SIM = os.path.join(BUILD, 'host', 'waveod-sim' + EXE)
ATFE = os.environ.get('ATFE_ROOT') or r'D:\DevTools\ATfE-23.1.0-Windows-x86_64'

# the PC programs: the same flags as the host preset (CMakeLists.txt)
HOST_CFLAGS = ['-O2', '-std=gnu11', '-march=native', '-ffp-contract=off', '-fno-strict-aliasing', '-DWAVEOD_HOST']
if os.name == 'nt':
    HOST_CFLAGS += ['-D_CRT_SECURE_NO_WARNINGS', '-D_USE_MATH_DEFINES']


def _vs(pattern):
    pf = os.environ.get('ProgramFiles', r'C:\Program Files')
    found = sorted(glob.glob(os.path.join(pf, 'Microsoft Visual Studio', '*', '*', pattern)))
    return found[-1] if found else None


def cmake():
    c = os.environ.get('WAVEOD_CMAKE') or shutil.which('cmake') or \
        _vs(r'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe')
    if not c:
        sys.exit('cmake não encontrado: instale o CMake ou defina WAVEOD_CMAKE')
    return c


def atfe(tool):
    """a program of the Arm toolchain: clang, ld.lld, llvm-objdump, ..."""
    p = os.path.join(ATFE, 'bin', tool + EXE)
    if not os.path.exists(p):
        sys.exit('%s não encontrado: defina ATFE_ROOT (Arm Toolchain for Embedded)' % p)
    return p


def host_cc():
    c = os.environ.get('WAVEOD_HOST_CC') or _vs(r'VC\Tools\Llvm\x64\bin\clang.exe') or shutil.which('clang')
    if not c:
        sys.exit('clang não encontrado: instale o componente "C++ Clang Compiler for Windows" do Visual Studio '
                 'ou defina WAVEOD_HOST_CC')
    return c


def build(preset, target=None, quiet=True):
    """configure the preset's tree if it has none, then build (TARGET, or everything)"""
    c = cmake()
    out = dict(stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True) if quiet else {}
    if not os.path.exists(os.path.join(BUILD, preset, 'build.ninja')):
        r = subprocess.run([c, '--preset', preset], cwd=ROOT, **out)
        if r.returncode:
            sys.exit('cmake --preset %s falhou:\n%s' % (preset, r.stdout or ''))
    cmd = [c, '--build', '--preset', preset] + (['--target', target] if target else [])
    r = subprocess.run(cmd, cwd=ROOT, **out)
    if r.returncode:
        sys.exit('%s falhou:\n%s' % (' '.join(cmd), r.stdout or ''))


def fw_elf():
    """the firmware ELF, built if out of date"""
    build('fw')
    return FW_ELF


def tests(args, **kw):
    """the unit tests' program with ARGS, built if out of date; cwd = the repo"""
    build('host', 'waveod-tests')
    return subprocess.run([TESTS] + list(args), cwd=ROOT, **kw)


CLONEFIT = os.path.join(ROOT, 'out', 'clonefit', 'clonefit' + EXE)
NAM_CORE = os.path.join(ROOT, 'third_party', 'nam')


def host_cxx():
    c = os.environ.get('WAVEOD_HOST_CXX') or _vs(r'VC\Tools\Llvm\x64\bin\clang++.exe') or shutil.which('clang++')
    if not c:
        sys.exit('clang++ não encontrado: instale o componente "C++ Clang Compiler for Windows" do Visual Studio '
                 'ou defina WAVEOD_HOST_CXX')
    return c


def clonefit(quiet=True):
    """tools/clonefit (waveod Studio's NAM -> Clone converter, with NeuralAmpModelerCore from third_party/nam),
    built when older than any of its sources; its path"""
    srcs = [os.path.join(ROOT, 'tools', 'clonefit', 'clonefit.cpp')] + \
        sorted(glob.glob(os.path.join(NAM_CORE, 'NAM', '**', '*.cpp'), recursive=True))
    deps = srcs + glob.glob(os.path.join(NAM_CORE, 'NAM', '**', '*.h'), recursive=True)
    if os.path.exists(CLONEFIT) and os.path.getmtime(CLONEFIT) >= max(os.path.getmtime(p) for p in deps):
        return CLONEFIT
    os.makedirs(os.path.dirname(CLONEFIT), exist_ok=True)
    cmd = [host_cxx(), '-O2', '-std=c++20', '-DNDEBUG', '-D_USE_MATH_DEFINES', '-Wno-deprecated-declarations',
           '-I', NAM_CORE, '-I', os.path.join(NAM_CORE, 'NAM'), '-I', os.path.join(NAM_CORE, 'Dependencies', 'eigen'),
           '-I', os.path.join(NAM_CORE, 'Dependencies', 'nlohmann'), '-o', CLONEFIT] + srcs
    if os.name == 'nt':                 # the C and C++ runtimes linked in: runs without the Visual C++ redistributable
        cmd.insert(1, '-fms-runtime-lib=static')
    if not quiet:
        print('compilando o conversor (um minuto ou dois)...', flush=True)
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        sys.exit('clang++ falhou:\n%s%s' % (r.stdout, r.stderr))
    return CLONEFIT


def host_compile(out, sources, flags=(), **kw):
    """a PC program from C sources with the host clang; raises SystemExit with the compiler's output on failure"""
    cmd = [host_cc()] + HOST_CFLAGS + list(flags) + ['-o', out] + list(sources)
    r = subprocess.run(cmd, capture_output=True, text=True, **kw)
    if r.returncode:
        sys.exit('clang falhou:\n%s%s' % (r.stdout, r.stderr))
    return out


if __name__ == '__main__':
    if len(sys.argv) >= 2 and sys.argv[1] == 'clonefit':
        print(clonefit(quiet=False))
        sys.exit(0)
    if len(sys.argv) < 2 or sys.argv[1] not in ('fw', 'fw-locktest', 'host'):
        sys.exit(__doc__)
    build(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else None, quiet=False)
