"""Exercise the Linux build wrapper with shell command doubles; no client build."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "Telegram/build/docker/centos_env/build.sh"
BASH = sys.argv[1] if len(sys.argv) > 1 else "bash"

with tempfile.TemporaryDirectory(prefix="luxury-linux-python-") as directory:
    work = Path(directory)
    (work / "bin").mkdir()
    (work / "Telegram").mkdir()
    (work / "trace").mkdir()
    for name, target in (
        ("python", work / "bin/python3"),
        ("configure", work / "Telegram/configure.sh"),
        ("cmake", work / "bin/cmake"),
    ):
        target.write_text(
            '#!/bin/bash\n'
            f'printf "%s\\n" "$@" > "$TRACE/{name}"\n'
            + ('exit "${CHECK_EXIT:-0}"\n' if name == "python" else ''),
            encoding="utf-8",
            newline="\n",
        )
        target.chmod(0o755)

    def run(settings, *arguments):
        for trace in (work / "trace").iterdir():
            trace.unlink()
        env = dict(os.environ)
        for key in ("CONFIG", "KEEP_GOING", "CHECK_EXIT"):
            env.pop(key, None)
        env.update(settings)
        result = subprocess.run(
            [BASH, "-c", 'export PATH="$PWD/bin:$PATH" TRACE="$PWD/trace"; bash "$@"',
             "wrapper-test", SCRIPT.as_posix(), *arguments],
            cwd=work, env=env, check=False,
        )
        traces = {p.name: p.read_text().splitlines() for p in (work / "trace").iterdir()}
        return result.returncode, traces

    for settings, config, tail in (
        ({}, "Release", []),
        ({"CONFIG": "Debug"}, "Debug", []),
        ({"CONFIG": "Debug", "KEEP_GOING": "1"}, "Debug", ["--", "-k", "0"]),
    ):
        code, traces = run(settings, "-D", "TEST_VALUE=with spaces")
        assert code == 0, (code, traces)
        assert traces["python"] == [
            "-c", 'from xml.etree import ElementTree; ElementTree.fromstring("<check/>")'
        ]
        configure = traces["configure"]
        assert configure[0] == "-D" and configure[1].startswith("Python3_EXECUTABLE=")
        assert configure[1].endswith("/bin/python3"), configure
        assert configure[2:] == ["-D", "TEST_VALUE=with spaces"], configure
        assert traces["cmake"] == ["--build", "../out", "--config", config, *tail]
    code, traces = run({"CHECK_EXIT": "23"})
    assert code == 23 and set(traces) == {"python"}, (code, traces)
print("PASS: selected Python reaches CMake, arguments/config/keep-going preserved, XML failure stops before build")
