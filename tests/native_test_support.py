"""Compile small Win32 harnesses against actual production functions."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def compile_native(source, name):
    vswhere = Path(os.environ['ProgramFiles(x86)']) / 'Microsoft Visual Studio/Installer/vswhere.exe'
    vs = subprocess.check_output([str(vswhere), '-latest', '-products', '*', '-requires',
                                  'Microsoft.VisualStudio.Component.VC.Tools.x86.x64',
                                  '-property', 'installationPath'], text=True).strip()
    if not vs:
        raise RuntimeError('MSVC x86/x64 tools are required')
    setup = str(Path(vs) / 'VC/Auxiliary/Build/vcvars32.bat')
    output = subprocess.check_output('cmd.exe /d /s /c ""' + setup +
                                     '" >nul && set"', text=True)
    env = dict(os.environ)
    for line in output.splitlines():
        if '=' in line and not line.startswith('='):
            key, value = line.split('=', 1)
            env[key] = value
    folder = Path(tempfile.mkdtemp(prefix='dragon-test-'))
    cpp = folder / (name + '.cpp')
    cpp.write_text(source, encoding='utf-8')
    exe = folder / (name + '.exe')
    compiler = Path(env['VCToolsInstallDir']) / 'bin/Hostx86/x86/cl.exe'
    result = subprocess.run([str(compiler), '/nologo', '/EHsc', '/std:c++14',
                             '/I' + str(ROOT), str(cpp), '/Fe:' + str(exe)],
                            env=env, cwd=folder, capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(result.stdout + result.stderr)
    return exe


def function_body(path, signature):
    source = (ROOT / path).read_bytes().decode('cp1252')
    start = source.index(signature)
    brace = source.index('{', start)
    depth = 1
    pos = brace + 1
    while depth:
        if source[pos] == '{':
            depth += 1
        elif source[pos] == '}':
            depth -= 1
        pos += 1
    return source[start:pos]
