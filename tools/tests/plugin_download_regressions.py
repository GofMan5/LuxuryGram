"""Compile the production download loop with real Qt files and synchronous HTTP doubles.

Windows Debug/static Qt check, no account or network access:
  python tools/tests/plugin_download_regressions.py --qt-root <Qt prefix> --vcvars <vcvars64.bat>
The old loop must fail the directory swap; removing its close-error guard must
also fail the source contract. All files live in a disposable temporary directory.
"""
from pathlib import Path
import argparse
import os
import subprocess
import tempfile

from library_regressions import ROOT, function, source

INSTALLER = 'Telegram/SourceFiles/luxury/plugins/plugin_installer.cpp'
CPP = r'''
#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QTemporaryDir>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <vector>
#include "luxury/plugins/core/plugin_manifest.h"
#include "luxury/plugins/core/plugin_registry.h"
#include "luxury/plugins/plugin_install_logic.h"

#define EXPECT(x) do { if (!(x)) { std::cerr << "line " << __LINE__ << ": " #x "\n"; std::exit(1); } } while (false)
template <typename T> using Fn = std::function<T>;
QString operator""_q(const char16_t *text, std::size_t length) {
    return QString::fromUtf16(reinterpret_cast<const ushort*>(text), int(length));
}
namespace base {
template <typename T> struct Weak { T *value; T *get() const { return value; } };
template <typename T> Weak<T> make_weak(T *value) { return { value }; }
}
namespace Luxury::Plugins {
QString Text(const std::string &text) { return QString::fromStdString(text); }
class Installer {
public:
    struct DownloadState;
    void download(const PluginSource&, std::vector<std::string>, QString, Fn<void(QString)>);
    void downloadNext(std::shared_ptr<DownloadState>);
    int status = 200;
    QString transportError;
    int requests = 0;
    const QByteArray payload = QByteArray("export default {};\n\0end", 23);
    void get(const std::string&, qint64, Fn<void(int, QByteArray, QString)> done) {
        ++requests;
        done(status, payload, transportError);
    }
};
STATE
DOWNLOAD
NEXT
}
using namespace Luxury::Plugins;
namespace fs = std::filesystem;
fs::path Path(const QString &text) { return fs::path(text.toStdWString()); }
void Install(const QString &root, const std::vector<std::string> &files, bool update) {
    Installer installer;
    const auto staged = root + "/staging/code";
    const auto target = root + "/plugins/test/code";
    if (update) {
        EXPECT(QDir().mkpath(target));
        QFile old(target + "/old.js");
        EXPECT(old.open(QIODevice::WriteOnly));
        EXPECT(old.write("old") == 3);
        old.close();
    }
    int callbacks = 0;
    installer.download({ .owner = "test", .repo = "plugin", .ref = "test" }, files, staged, [&](QString error) {
        ++callbacks;
        EXPECT(error.isEmpty());
        // Runs synchronously before downloadNext's QFile leaves its scope.
        const auto swapped = SwapWithJournal(Path(staged), Path(target), "test", !update, false);
        if (!swapped.empty()) std::cerr << swapped << '\n';
        EXPECT(swapped.empty());
        FinishSwap(Path(target));
        for (const auto &name : files) {
            QFile installed(target + '/' + Text(name));
            EXPECT(installed.open(QIODevice::ReadOnly));
            EXPECT(installed.readAll() == installer.payload);
        }
        EXPECT(!QFile::exists(target + "/old.js"));
        EXPECT(QFile::exists(target + ".prev/old.js") == update);
        if (update) {
            QFile previous(target + ".prev/old.js");
            EXPECT(previous.open(QIODevice::ReadOnly));
            EXPECT(previous.readAll() == "old");
        }
    });
    EXPECT(callbacks == 1);
    EXPECT(installer.requests == int(files.size()));
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir work;
    EXPECT(work.isValid());
    Install(work.path() + "/one", { "main.js" }, false);
    Install(work.path() + "/many", { "main.js", "lib/helper.js", "empty.js" }, false);
    Install(work.path() + "/update", { "main.js" }, true);
    for (const bool transport : { false, true }) {
        Installer installer;
        installer.status = transport ? 0 : 404;
        installer.transportError = transport ? "offline" : "";
        int callbacks = 0;
        installer.download({ .owner = "test", .repo = "plugin", .ref = "test" }, { "main.js", "other.js" },
            work.path() + "/failed", [&](QString error) { ++callbacks; EXPECT(!error.isEmpty()); });
        EXPECT(callbacks == 1 && installer.requests == 1);
    }
    std::cout << "Plugin download PASS: real Qt file handles, synchronous completion, single/multiple files, "
        "update/rollback bytes, HTTP/network failure; no profile or network\n";
}
'''


def execute(command, cwd):
    result = subprocess.run(command, cwd=cwd, capture_output=True, text=True, errors='replace')
    if result.returncode:
        raise AssertionError(f'exit {result.returncode}\n{result.stdout}{result.stderr}')
    return result.stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--qt-root', type=Path, required=True)
    parser.add_argument('--vcvars', type=Path, required=True)
    args = parser.parse_args()
    assert os.name == 'nt', 'this native file-lock regression requires Windows'
    qt = args.qt_root.resolve(strict=True)
    vcvars = args.vcvars.resolve(strict=True)
    text = source(INSTALLER)
    state = text[text.index('struct Installer::DownloadState {'):text.index('\n};', text.index('struct Installer::DownloadState {')) + 3]
    download = function(text, 'void Installer::download(')
    next_file = function(text, 'void Installer::downloadNext(')
    guard = 'if (file.error() != QFileDevice::NoError) {'
    assert next_file.index('file.close();') < next_file.index(guard) < next_file.index('++state->index;')
    assert 'state->done(name + u": "_q + file.errorString());\n\t\t\treturn;' in next_file[next_file.index(guard):]
    with tempfile.TemporaryDirectory(prefix='luxury-plugin-download-') as directory:
        work = Path(directory)
        cpp = CPP.replace('STATE', state).replace('DOWNLOAD', download).replace('NEXT', next_file)
        (work / 'download.cpp').write_text(cpp, encoding='utf-8')
        command = (
            f'@echo off\ncall "{vcvars}" >nul\nif errorlevel 1 exit /b %errorlevel%\n'
            'cl /nologo /std:c++20 /EHsc /MTd /Od /D_DEBUG /DQT_STATIC /DQT_CORE_LIB /DNOMINMAX /Zc:__cplusplus '
            f'/I"{qt / "include"}" /I"{qt / "include/QtCore"}" /I"{ROOT / "Telegram/SourceFiles"}" '
            f'download.cpp "{ROOT / "Telegram/SourceFiles/luxury/plugins/core/plugin_source.cpp"}" '
            f'"{ROOT / "Telegram/SourceFiles/luxury/plugins/core/plugin_manifest.cpp"}" '
            f'"{ROOT / "Telegram/SourceFiles/luxury/plugins/core/plugin_permissions.cpp"}" '
            f'"{ROOT / "Telegram/SourceFiles/luxury/plugins/core/plugin_version.cpp"}" '
            f'/Fe:download.exe /link /LIBPATH:"{qt / "lib"}" Qt5Cored.lib qtpcre2d.lib '
            'mpr.lib userenv.lib version.lib netapi32.lib ws2_32.lib advapi32.lib kernel32.lib '
            'ole32.lib shell32.lib uuid.lib user32.lib winmm.lib\n'
            'if errorlevel 1 exit /b %errorlevel%\ndownload.exe\n')
        build = work / 'build.cmd'
        build.write_text(command, encoding='utf-8')
        print(execute([os.environ['COMSPEC'], '/d', '/c', str(build)], work).strip())
        # Prove this test rejects the original open-handle lifetime bug.
        (work / 'download.cpp').write_text(cpp.replace('file.close();', '', 1), encoding='utf-8')
        try:
            execute([os.environ['COMSPEC'], '/d', '/c', str(build)], work)
        except AssertionError as error:
            assert 'Access is denied' in str(error) or 'swapped.empty()' in str(error), str(error)
        else:
            raise AssertionError('the regression accepts an open file during directory replacement')
        print('Original open-handle mutant: rejected')


if __name__ == '__main__':
    main()
