"""Run all focused custom-feature repair checks, not a native application build.

python tools/tests/run_custom_regressions.py [--receipt path]
Only temporary synthetic fixtures; no account, network, release or installed files.
"""
from pathlib import Path
import argparse
import json
import os
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
TESTS = (
    'database_migration.py', 'update_legacy_contract.py',
    'release_contract.py', 'canary_discovery_regressions.py',
    'linux_protected_contract.py', 'message_shot_lifetime.py',
    'filter_regressions.py', 'promo_notifications.py',
    'chat_folder_refresh.py', 'language_lifecycle.py',
    'messaging_intent.py', 'translation_regressions.py',
    'ui_generation_regressions.py', 'ghost_settings_regressions.py',
    'ghost_owner_lifecycle.py', 'ghost_read_intent.py',
    'watched_media_regressions.py', 'attachment_music_lifetime.py',
    'forward_lifecycle_regressions.py', 'run_watcher.py',
    'library_regressions.py', 'ui_library_regressions.py',
    'ui_client_regressions.py',
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--receipt', type=Path)
    parser.add_argument('--log', type=Path)
    args = parser.parse_args()
    environment = os.environ.copy()
    compiler = Path(environment.get('CXX', r'C:\Strawberry\c\bin\g++.exe'))
    if compiler.is_file():
        environment['CXX'] = str(compiler)
        environment['PATH'] = str(compiler.parent) + os.pathsep + environment.get('PATH', '')
    results = []
    output = []
    environment['PYTHONIOENCODING'] = 'utf-8'
    for name in TESTS:
        heading = f'\n=== {name} ===\n'
        print(heading, end='', flush=True)
        started = time.monotonic()
        result = subprocess.run([sys.executable, '-B', str(ROOT / 'tools/tests' / name)], cwd=ROOT, env=environment,
                                capture_output=True, text=True, encoding='utf-8', errors='replace')
        text = result.stdout + result.stderr
        print(text, end='', flush=True)
        output.append(heading + text)
        results.append({'test': name, 'exit_code': result.returncode, 'seconds': round(time.monotonic() - started, 2)})
    if args.log:
        args.log.write_text(''.join(output), encoding='utf-8')
    receipt = {'scope': 'focused extracted-production/library/source checks; NOT native Qt application/platform/network QA',
               'python': sys.executable, 'compiler': str(compiler), 'results': results}
    if args.receipt:
        args.receipt.write_text(json.dumps(receipt, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')
    failures = [r['test'] for r in results if r['exit_code']]
    print(f'\nFocused custom regressions: {len(results) - len(failures)}/{len(results)} PASS', flush=True)
    if failures:
        print('FAILED: ' + ', '.join(failures), flush=True)
    return bool(failures)


if __name__ == '__main__':
    raise SystemExit(main())
