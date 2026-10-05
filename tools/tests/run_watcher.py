"""Focused Watcher checks: python tools/tests/run_watcher.py.
Requires Python 3 and g++ with C++20; does not build Qt or use a profile.
"""
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]
for script in ('watcher_contract.py', 'button_keyboard.py', 'watcher_recording.py'):
    subprocess.run([sys.executable, str(root / 'tools/tests' / script)], check=True)
with tempfile.TemporaryDirectory(prefix='luxury-watcher-test-') as tmp:
    for name in ('watcher_revisions', 'watcher_presence'):
        exe = Path(tmp) / (name + '.exe')
        subprocess.run([
            'g++', '-std=c++20', '-Wall', '-Wextra', '-Werror',
            '-I', str(root / 'Telegram/SourceFiles'),
            str(root / 'tools/tests' / (name + '.cpp')), '-o', str(exe),
        ], check=True)
        subprocess.run([str(exe)], check=True)
print('Focused Watcher checks: PASS (not a native app build or visual test)')
