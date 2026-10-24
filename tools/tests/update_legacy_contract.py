"""Compile actual legacy adapter, public headers, header parser and marker writer.
python tools/tests/update_legacy_contract.py [--cxx g++]
Uses explicit Qt/filesystem/decompressor doubles; not native crypto/Qt unpack/apply.
Native companion: test_update_unpack (Debug, auto-update enabled).
"""
from pathlib import Path
import argparse
import os
import re
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
checker = (root / 'Telegram/SourceFiles/core/update_checker.cpp').read_text('utf-8')
unpack = (root / 'Telegram/SourceFiles/core/update_unpack.cpp').read_text('utf-8')
legacy = checker[checker.index('bool UnpackUpdate('):checker.index('template <typename Callback>', checker.index('bool UnpackUpdate('))]
assert legacy.index('Updates::VerifyLegacyUpdate(') < legacy.index('base::Platform::DeleteDirectory(tempDirPath)') < legacy.index('Updates::ExtractUpdateFiles(')
assert unpack.index('RSA_verify(') < unpack.index('auto uncompressed = DecompressUpdatePayload(verified')
assert 'else if (BuildIsCanary)' in legacy and 'canary builds accept only v2 updates.' in legacy
assert 'ExtractUpdateFiles(stream' not in legacy
assert '<LzmaLib.h>' in unpack and '#ifndef TDESKTOP_DISABLE_AUTOUPDATE' in unpack
assert 'tupdates/temp/LuxuryGram.app/Contents/Frameworks/Updater' in checker
assert 'tupdates/temp/Telegram.app/Contents/Frameworks/Updater' not in checker
ready = checker[checker.index('bool checkReadyUpdate()'):]
alpha = ready[ready.index('if (versionNum == 0x7FFFFFFF)'):ready.index('} else if (versionNum == kVersionFileCanaryMarker)')]
assert '|| BuildIsCanary' in alpha and '|| !cAlphaVersion()' in alpha and 'alphaVersion <= cAlphaVersion()' in alpha
adapter = legacy[legacy.index('auto legacyError = QString();'):legacy.index('\n\tQString tempDirPath')]
adapter += legacy[legacy.index('\n\tif (!Updates::ExtractUpdateFiles('):legacy.index('\n\tif (!WriteUpdateReadyFile')]
writer = checker[checker.index('[[nodiscard]] bool WriteUpdateVersionFile('):checker.index('[[nodiscard]] bool WriteUpdateReadyFile(')]
header = unpack[unpack.index('auto stream = QDataStream(uncompressed);'):unpack.index('\n\tauto files = QSet<QString>();')]
linux = (root / 'Telegram/SourceFiles/_other/updater_linux.cpp').read_text('utf-8')
remap = linux[linux.index('} else if ((equal(tofname, exePath + "LuxuryGram")'):linux.index('\n\t\t\t\t\tif (fname == readyFilePath)', linux.index('} else if ((equal(tofname, exePath + "LuxuryGram")'))]
remap = remap.removeprefix('} else ')
# LOG is disabled only in the synthetic standalone translation unit.
cpp = r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include "core/update_unpack.h"
#include "core/update_verify.h"
#define LOG(...) do {} while (false)
using std::string;
using int32 = int32_t;
using VersionInt = uint32_t;
using VersionChar = wchar_t;
constexpr auto kVersionFileCanaryMarker = quint32(0x7FFFFFFE);
constexpr auto LuxuryUpdateVersion = int32(1000020);
constexpr auto hSigLen = 512;
constexpr auto hShaLen = 32;
quint64 runningAlpha = 0;
quint64 cAlphaVersion() { return runningAlpha; }
constexpr auto kMinimumEntrySize = 14;
struct Device {
 const QByteArray &data;
 size_t position = 0;
 size_t bytesAvailable() const { return data.bytes.size() - position; }
};
struct QDataStream {
 enum { Qt_5_1 = 1, Ok = 0, Bad = 1 };
 Device input;
 int state = Ok;
 explicit QDataStream(const QByteArray &data) : input{data} {}
 void setVersion(int) {}
 int status() const { return state; }
 Device *device() { return &input; }
 template<class T> QDataStream &operator>>(T &out) {
  if (input.bytesAvailable() < sizeof(T)) { state = Bad; return *this; }
  out = 0;
  for (size_t i = 0; i != sizeof(T); ++i) out = T((out << 8) | input.data.bytes[input.position++]);
  return *this;
 }
};
struct QIODevice { enum { WriteOnly = 1 }; };
struct QFileDevice { enum { NoError = 0, WriteError = 1 }; };
std::map<std::u16string, std::vector<unsigned char>> files;
int failWriteAt = -1, writes = 0;
bool failFlush = false, failClose = false;
struct QDir {
 QString path;
 explicit QDir(QString p = {}) : path(std::move(p)) {}
 QString absolutePath() const { return path; }
 bool mkdir(const QString&) { return true; }
};
struct QFile {
 QString path;
 bool opened = false;
 explicit QFile(QString p) : path(std::move(p)) {}
 bool open(int) { opened = true; files[path.value].clear(); return true; }
 int64_t write(const char *data, size_t size) {
  ++writes;
  if (writes == failWriteAt) return -1;
  auto &out = files[path.value]; out.insert(out.end(), data, data + size);
  return size;
 }
 bool flush() { return !failFlush; }
 void close() { opened = false; }
 int error() const { return failClose ? QFileDevice::WriteError : QFileDevice::NoError; }
 QString fileName() const { return path; }
};
void SetError(QString *error, const char*) { if (error) *error = QString(u"error"); }
struct Display { std::wstring toStdWString() const { return L"1.0.21"; } };
Display FormatVersionDisplay(quint32) { return {}; }
WRITER_HERE
int extractCalls = 0;
quint64 observedVersion = 0, observedAlpha = 0;
QByteArray observedPayload;
namespace Core::Updates {
std::optional<LegacyUpdate> VerifyLegacyUpdate(const QByteArray &bytes, const char*, Channel, QString*) {
 // Authentication/decompression is intentionally doubled here; exercised with
 // actual OpenSSL/XZ production helper by linux_protected_contract.py.
 observedPayload = bytes.mid(hSigLen + hShaLen); auto result = LegacyUpdate{observedPayload};
 QDataStream stream(result.uncompressed); stream.setVersion(QDataStream::Qt_5_1); stream >> result.version; if(result.version == 0x7FFFFFFF) stream >> result.alphaVersion;
 return stream.status() == QDataStream::Ok ? std::optional(result) : std::nullopt;
}
bool ExtractUpdateFiles(const QByteArray &uncompressed, quint64 expectedVersion, const QString&, QString *error, quint64 expectedLegacyAlphaVersion) {
 ++extractCalls;
 observedVersion = expectedVersion; observedAlpha = expectedLegacyAlphaVersion;
HEADER_HERE
 return true; // File/name/content parser remains covered by native companion.
}
}
using namespace Core;
const char *UpdatesPublicKey = "fixture";
constexpr auto BuildUpdateChannel = Updates::Channel::Stable;
bool run(const QByteArray &compressed, QDir &tempDir, const QString &tempDirPath) {
ADAPTER_HERE
 return true;
}
void word(QByteArray &out, uint64_t value, int size) {
 for (int i = size - 1; i >= 0; --i) out.bytes.push_back((value >> (i * 8)) & 255);
}
QByteArray payload(quint32 version, quint64 alpha = 0, quint32 count = 1) {
 QByteArray out; word(out, version, 4); if (alpha) word(out, alpha, 8); word(out, count, 4);
 out.bytes.resize(out.bytes.size() + 15, 0); return out;
}
QByteArray envelope(const QByteArray &body) {
 QByteArray out; out.bytes.resize(hSigLen + hShaLen, 0xA5);
 out.bytes.insert(out.bytes.end(), body.bytes.begin(), body.bytes.end()); return out;
}
uint64_t nativeWord(const std::vector<unsigned char> &bytes, size_t offset, size_t size) {
 uint64_t out = 0; memcpy(&out, bytes.data() + offset, size); return out;
}
void reset() { files.clear(); extractCalls = 0; writes = 0; failWriteAt = -1; failFlush = false; failClose = false; }
int main() {
 QDir dir(QString(u"temp")); QString path(u"temp");
 const auto newVersion = quint32(LuxuryUpdateVersion + 1);
 reset(); runningAlpha = 0;
 const auto stable = payload(newVersion);
 assert(run(envelope(stable), dir, path));
 assert(observedPayload.bytes == stable.bytes && observedVersion == Updates::MakeUpdateVersion(newVersion, 0) && !observedAlpha);
 auto bytes = files[u"temp/tdata/version"];
 assert(nativeWord(bytes, 0, 4) == newVersion && bytes.size() == 8 + 6 * sizeof(wchar_t));
 reset(); assert(!run(envelope(payload(LuxuryUpdateVersion)), dir, path) && !extractCalls && files.empty());
 reset(); assert(!run(envelope(payload(newVersion, 0, 0)), dir, path) && files.empty());
 reset(); assert(!run(envelope(payload(newVersion, 0, 1000)), dir, path) && files.empty());
 reset(); QByteArray truncated; word(truncated, newVersion, 4); assert(!run(envelope(truncated), dir, path));
 reset(); const auto nextAlpha = quint64(newVersion) * 1000 + 2; runningAlpha = nextAlpha - 1;
 assert(run(envelope(payload(0x7FFFFFFF, nextAlpha)), dir, path));
 bytes = files[u"temp/tdata/version"];
 assert(observedAlpha == nextAlpha && bytes.size() == 12 && nativeWord(bytes, 0, 4) == 0x7FFFFFFF && nativeWord(bytes, 4, 8) == nextAlpha);
 reset(); runningAlpha = 0; assert(!run(envelope(payload(0x7FFFFFFF, nextAlpha)), dir, path) && !extractCalls);
 reset(); runningAlpha = nextAlpha; assert(!run(envelope(payload(0x7FFFFFFF, nextAlpha)), dir, path) && !extractCalls);
 reset(); QString error;
 assert(!Updates::ExtractUpdateFiles(payload(0x7FFFFFFF, nextAlpha), Updates::MakeUpdateVersion(0x7FFFFFFF, 0), path, &error));
 assert(!Updates::ExtractUpdateFiles(payload(0x7FFFFFFF, nextAlpha), 0, path, &error, nextAlpha + 1));
 assert(!Updates::ExtractUpdateFiles(stable, Updates::MakeUpdateVersion(newVersion, 0), path, &error, nextAlpha));
 reset(); assert(WriteUpdateVersionFile(dir, path, newVersion, Updates::MakeUpdateVersion(newVersion, 7)));
 bytes = files[u"temp/tdata/version"]; assert(bytes.size() == 12 && nativeWord(bytes, 0, 4) == kVersionFileCanaryMarker && nativeWord(bytes, 4, 8) == Updates::MakeUpdateVersion(newVersion, 7));
 for (int fail = 1; fail != 4; ++fail) { reset(); failWriteAt = fail; assert(!run(envelope(stable), dir, path)); }
 reset(); failFlush = true; assert(!run(envelope(stable), dir, path));
 reset(); failClose = true; assert(!run(envelope(stable), dir, path));
 for (const auto writeprotected : {false, true}) for (auto name : {"LuxuryGram", "luxurygram", "LUXURYGRAM", "Telegram", "Renamed"}) {
  for (auto package : {"LuxuryGram", "Telegram", "resources/Telegram"}) {
   std::string exePath = "/install/", exeName = name, tofname = exePath + package;
   auto equal = [](std::string a, std::string b) { std::transform(a.begin(), a.end(), a.begin(), ::tolower); std::transform(b.begin(), b.end(), b.begin(), ::tolower); return a == b; };
   auto writeLog = [](auto&&...) {};
REMAP_HERE
   assert(tofname == exePath + (std::string(package) == "resources/Telegram" || (writeprotected && std::string(package) == "Telegram") ? package : name));
  }
 }
 std::cout << "PASS26: actual updater adapter/public headers/header-parser/marker writer/remap, synthetic Qt; native crypto/apply pending\n";
}
'''
cpp = cpp.replace('WRITER_HERE', writer).replace('ADAPTER_HERE', adapter).replace('HEADER_HERE', header).replace('REMAP_HERE', remap)
qbyte = '''#pragma once
#include <cstdint>
#include <vector>
using uchar = unsigned char;
using quint8 = uint8_t; using quint32 = uint32_t; using quint64 = uint64_t;
using qint32 = int32_t; using qint64 = int64_t;
struct QByteArray {
 std::vector<unsigned char> bytes;
 QByteArray mid(size_t offset) const { QByteArray out; if(offset <= bytes.size()) out.bytes.assign(bytes.begin() + offset, bytes.end()); return out; }
 size_t size() const { return bytes.size(); }
};
'''
qstring = '''#pragma once
#include <string>
struct QString {
 std::u16string value;
 QString() = default;
 QString(const char16_t *text) : value(text) {}
 QString(std::u16string text) : value(std::move(text)) {}
 friend QString operator+(const QString &a, const QString &b) { return a.value + b.value; }
};
inline QString operator""_q(const char16_t *text, size_t size) { return std::u16string(text, size); }
'''
parser = argparse.ArgumentParser()
parser.add_argument('--cxx', default=os.environ.get('CXX') or shutil.which('g++') or r'C:\Strawberry\c\bin\g++.exe')
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix='luxury-update-contract-') as directory:
    work = Path(directory)
    stubs = work / 'QtCore'; stubs.mkdir()
    (stubs / 'QByteArray').write_text(qbyte, encoding='utf-8')
    (stubs / 'QString').write_text(qstring, encoding='utf-8')
    test = work / 'updater.cpp'; test.write_text(cpp, encoding='utf-8')
    exe = work / ('updater.exe' if os.name == 'nt' else 'updater')
    subprocess.run([args.cxx, '-std=c++20', '-O0', '-D_DEBUG', '-DQ_OS_WIN', '-I', str(work), '-I', str(root / 'Telegram/SourceFiles'), str(test), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
