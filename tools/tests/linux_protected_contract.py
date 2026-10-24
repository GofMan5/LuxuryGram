"""U5: real RSA4096/SHA256 + XZ, actual protected production slices, Debug/O0.

python -B tools/tests/linux_protected_contract.py [--cxx g++]
Qt wire types and protected filesystem/v2 verification are explicit doubles.
No application, profiles, elevation, installed-file writes, network or private-key access.
"""
from pathlib import Path
import argparse
import hashlib
import os
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'Telegram/SourceFiles'


def between(text, start, end):
    return text[text.index(start):text.index(end, text.index(start))]


unpack = (SOURCE / 'core/update_unpack.cpp').read_text('utf-8')
install = (SOURCE / 'platform/linux/update_install_linux.cpp').read_text('utf-8')
verify = (SOURCE / 'core/update_verify.cpp').read_text('utf-8')
config = (SOURCE / 'config.h').read_text('utf-8')
assert 'static const char *UpdatesPublicKey' in config and 'BEGIN RSA PUBLIC KEY' in config
packer = (SOURCE / '_other/packer.cpp').read_text('utf-8')
# Bind the helper to the exact current Packer format/policy, without its private header.
assert 'hSigLen = 512, hShaLen = 32' in packer
assert 'RSA_sign(NID_sha256' in packer and 'stream << quint32(version);' in packer
assert 'compressed.mid(hSigLen + hShaLen)' in packer
helper = between(unpack, 'std::optional<LegacyUpdate> VerifyLegacyUpdate(', 'bool ExtractUpdateFiles(')
assert helper.index('RSA_verify(') < helper.index('auto uncompressed = DecompressUpdatePayload(')
assert 'RSA_size(key.get()) != kSignatureSize' in helper
assert 'UpdatesPublicBetaKey' not in helper
assert '#include "config.h"' in install
assert 'bytes,\n\t\tUpdatesPublicKey,\n\t\tCore::BuildUpdateChannel' in install
assert 'Core::RunningUpdateVersion(),' in install and '<= LuxuryUpdateVersion' in install
# Both unprivileged prepare/validate and root apply re-authenticate the package.
assert 'if (!VerifyPackage(bytes, beta, error))' in install
assert 'auto verified = VerifyPackage(*bytes, beta, error);' in install
assert 'ReadPackage(packagePath, error)' in install
assert 'Core::Updates::ExtractUpdateFiles(\n\t\t\tverified->uncompressed,\n\t\t\tverified->version,' in install
# Preserve the security-bearing sinks outside the executable-name change.
for invariant in ('O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC', 'O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK',
                  'O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC',
                  'running.st_dev != entry.st_dev', 'running.st_ino != entry.st_ino',
                  '(info.st_mode & 07777) != 0700', 'PreflightDestination(directory, "Updater", error)',
                  'InstalledFileMatches(installation->directory.get(), file, error)',
                  'Staged entries map to conflicting destinations.'):
    assert invariant in install, invariant
required = between(install, '\tfor (const auto required :', '\tReportProgress("Checking authenticated files')
assert '{ "LuxuryGram", "Updater" }' in required and 'AT_SYMLINK_NOFOLLOW' in required

qbyte = r'''#pragma once
#include <cstdint>
#include <string>
using uchar = unsigned char;
using quint8 = uint8_t; using quint32 = uint32_t; using quint64 = uint64_t;
using qint32 = int32_t; using qint64 = int64_t; using qsizetype = int64_t;
namespace Qt { enum Initialization { Uninitialized }; }
struct QByteArray {
 std::string bytes;
 QByteArray() = default;
 QByteArray(const char *s) : bytes(s) {}
 QByteArray(std::string s) : bytes(std::move(s)) {}
 QByteArray(qsizetype n, Qt::Initialization) : bytes(size_t(n), '\0') {}
 const char *constData() const { return bytes.data(); }
 char *data() { return bytes.data(); }
 qsizetype size() const { return qsizetype(bytes.size()); }
 bool isEmpty() const { return bytes.empty(); }
 QByteArray mid(qsizetype n) const { return bytes.substr(size_t(n)); }
 bool startsWith(const char *s) const { return bytes.starts_with(s); }
 auto begin() { return bytes.begin(); } auto end() { return bytes.end(); }
 auto begin() const { return bytes.begin(); } auto end() const { return bytes.end(); }
 std::vector<QByteArray> split(char c) const {
  std::vector<QByteArray> out; size_t pos = 0;
  do { auto end = bytes.find(c, pos); out.emplace_back(bytes.substr(pos, end - pos));
   if (end == std::string::npos) break; pos = end + 1; } while (true);
  return out;
 }
 friend QByteArray operator+(const QByteArray &a, const QByteArray &b) { return a.bytes + b.bytes; }
 friend QByteArray operator+(const QByteArray &a, char b) { return a.bytes + b; }
 friend bool operator==(const QByteArray&, const QByteArray&) = default;
 friend bool operator<(const QByteArray &a, const QByteArray &b) { return a.bytes < b.bytes; }
};
'''
qstring = r'''#pragma once
#include <string>
struct QString {
 std::u16string value;
 QString() = default;
 QString(const char16_t *s) : value(s) {}
 static QString fromLatin1(const char *s) { QString out; while (*s) out.value += char16_t(*s++); return out; }
};
inline QString operator""_q(const char16_t *s, size_t n) { QString out; out.value.assign(s, n); return out; }
'''
cpp = r'''
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include <openssl/pem.h>
#include <openssl/sha.h>
#include <lzma.h>
#include "core/update_unpack.h"
#include "core/update_verify.h"
using namespace Core::Updates;
struct QDataStream {
 enum { Qt_5_1 = 1, Ok = 0, Bad = 1 };
 const QByteArray &input; size_t position = 0; int state = Ok;
 explicit QDataStream(const QByteArray &bytes) : input(bytes) {}
 void setVersion(int) {}
 int status() const { return state; }
 template<class T> QDataStream &operator>>(T &out) {
  if (input.bytes.size() - position < sizeof(T)) { state = Bad; return *this; }
  out = 0;
  for (size_t i = 0; i != sizeof(T); ++i) out = T((out << 8) | uchar(input.bytes[position++]));
  return *this;
 }
};
int decodeCalls = 0;
// Real installed liblzma, instrumented only to prove auth precedes decoder entry.
lzma_ret observedDecoder(lzma_stream *stream, uint64_t limit, uint32_t flags) {
 ++decodeCalls; assert(limit == 1024ULL * 1024 * 1024 && flags == LZMA_CONCATENATED);
 return lzma_stream_decoder(stream, limit, flags);
}
#define lzma_stream_decoder observedDecoder
namespace Core::Updates {
UNPACK_INTERNALS
UNPACK_HELPER
}
#undef lzma_stream_decoder

namespace Core {
// Build facts are mutable only in this explicit standalone lane double.
Channel BuildUpdateChannel = Channel::Stable;
bool BuildIsCanary = false;
quint64 RunningUpdateVersion() { return MakeUpdateVersion(7002010, 11); }
}
constexpr auto LuxuryUpdateVersion = 1000020;
constexpr auto AppBetaVersion = true;
const char *UpdatesPublicKey = nullptr; // Ephemeral fixture, not a production key.
struct QDateTime { static qint64 currentSecsSinceEpoch() { return 100; } };
struct KeyString { QByteArray toLatin1() const { return "linux"; } };
KeyString AutoUpdateKey() { return {}; }
int v2Calls = 0;
bool acceptV2 = false, heldValid = true;
VerifiedUpdate offeredV2;
namespace Core::Updates {
QByteArray RootPublicKeyPem() { return "root-double"; }
QByteArray EmbeddedManifest() { return "manifest-double"; }
QByteArray EmbeddedManifestSignature() { return "signature-double"; }
std::optional<Manifest> ParseVerifiedManifest(const QByteArray&, const QByteArray&, const QByteArray &root, QString*) {
 assert(root == "root-double"); return heldValid ? std::optional(Manifest{}) : std::nullopt;
}
std::optional<Target> TargetFromPlatformKey(const QByteArray &key) {
 assert(key == "linux"); return Target{Os::Linux, Arch::X64};
}
V2_MAGIC
std::optional<VerifiedUpdate> VerifyUpdate(const QByteArray&, Channel build, bool beta, Target target,
 quint64 running, const std::optional<Manifest> &held, const QByteArray &root, qint64 now, QString*) {
 ++v2Calls;
 assert(build == Core::BuildUpdateChannel && beta && target == (Target{Os::Linux, Arch::X64}));
 assert(running == Core::RunningUpdateVersion() && held && root == "root-double" && now == 100);
 // Explicit double: actual v2 cryptography is not claimed by this test.
 return acceptV2 && offeredV2.envelope.version > running ? std::optional(offeredV2) : std::nullopt;
}
}
ROOT_SUPPORT
PACKAGE

// Protected filesystem calls are explicit in-memory doubles, never host paths.
using FakeOffset = int64_t;
using FakeMode = unsigned;
#define off_t FakeOffset
#define mode_t FakeMode
constexpr auto S_IFREG = 0100000U, S_IFDIR = 0040000U, S_IFLNK = 0120000U;
constexpr auto S_IWGRP = 0020U, S_IWOTH = 0002U, S_IXUSR = 0100U;
#define S_ISDIR(m) (((m) & 0170000U) == S_IFDIR)
#define S_ISREG(m) (((m) & 0170000U) == S_IFREG)
constexpr auto O_RDONLY = 1, O_DIRECTORY = 2, O_NOFOLLOW = 4, O_CLOEXEC = 8, O_NONBLOCK = 16;
constexpr auto AT_SYMLINK_NOFOLLOW = 32;
constexpr auto kDirectoryFlags = O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC;
struct FakeStat { mode_t st_mode = 0; int st_uid = 0; int st_nlink = 1; off_t st_size = 0; int st_dev = 1, st_ino = 0; };
#define stat FakeStat
struct Entry { std::string name; FakeStat info; int id = -1; bool openable = true; };
std::map<int, std::vector<Entry>> directories;
std::map<int, FakeStat> attributes;
int nextId = 100, nofollowCalls = 0;
int fstat(int fd, FakeStat *out) {
 auto it = attributes.find(fd); if (it == attributes.end()) return -1; *out = it->second; return 0;
}
int fstatat(int fd, const char *name, FakeStat *out, int flags) {
 assert(flags == AT_SYMLINK_NOFOLLOW); ++nofollowCalls;
 for (const auto &e : directories[fd]) if (e.name == name) { *out = e.info; return 0; }
 errno = ENOENT; return -1;
}
int openat(int fd, const char *name, int flags) {
 assert(flags & O_NOFOLLOW); ++nofollowCalls;
 if (std::string(name) == ".") return fd;
 for (const auto &e : directories[fd]) if (e.name == name) return e.openable && (e.info.st_mode & 0170000U) != S_IFLNK ? e.id : -1;
 return -1;
}
struct FileDescriptor {
 int value = -1;
 explicit FileDescriptor(int fd = -1) : value(fd) {}
 int get() const { return value; }
 explicit operator bool() const { return value >= 0; }
};
struct DirectoryCursor { int id = 0; size_t position = 0; };
struct dirent { const char *d_name = nullptr; };
using DirectoryStream = std::unique_ptr<DirectoryCursor>;
DirectoryStream OpenDirectoryStream(int directory, QString&) {
 if (!directories.contains(directory)) return nullptr;
 return std::make_unique<DirectoryCursor>(DirectoryCursor{directory});
}
dirent *readdir(DirectoryCursor *cursor) {
 static dirent result;
 auto &entries = directories[cursor->id];
 if (cursor->position == entries.size()) return nullptr;
 result.d_name = entries[cursor->position++].name.c_str(); return &result;
}
template<class T> struct QSet {
 std::set<T> values;
 bool contains(const T &v) const { return values.contains(v); }
 void insert(const T &v) { values.insert(v); }
};
FAIL_HELPER
TRUST_HELPERS
SAFE_HELPERS
TRUSTED_OPEN
struct ExpectedFile { QByteArray path, digest; off_t size = 0; mode_t mode = 0; };
QByteArray FileDigest(int file, off_t size, QString&) { assert(attributes[file].st_size == size); return "digest-double"; }
INVENTORY
PREFLIGHT
int ReportError(const QString&) { return 1; }
int CheckRequired(int directory) { QString error; FileDescriptor staged(directory);
REQUIRED
 return 0;
}
#undef stat

void word(QByteArray &out, uint64_t value, int size) {
 for (int i = size - 1; i >= 0; --i) out.bytes += char((value >> (i * 8)) & 255);
}
QByteArray body(quint32 version) {
 // QDataStream Qt_5_1 wire double for Packer's two executable entries.
 QByteArray out; word(out, version, 4); word(out, 2, 4);
 for (const auto name : {"LuxuryGram", "Updater"}) {
  word(out, strlen(name) * 2, 4); for (const auto ch : std::string(name)) word(out, uchar(ch), 2);
  word(out, 12, 4); word(out, 12, 4); out.bytes += "fixture-data"; word(out, 1, 1);
 }
 return out;
}
QByteArray compress(const QByteArray &input) {
 QByteArray out(4 + lzma_stream_buffer_bound(input.size()), Qt::Uninitialized);
 const auto size = qint32(input.size()); memcpy(out.data(), &size, 4);
 size_t pos = 4;
 assert(lzma_easy_buffer_encode(6, LZMA_CHECK_CRC64, nullptr,
  reinterpret_cast<const uint8_t*>(input.constData()), input.size(),
  reinterpret_cast<uint8_t*>(out.data()), &pos, out.size()) == LZMA_OK);
 out.bytes.resize(pos); return out;
}
QByteArray sign(RSA *key, const QByteArray &payload) {
 // Packer's exact RSA4096/SHA256 signature+hash+payload layout.
 QByteArray out(544 + payload.size(), Qt::Uninitialized);
 memcpy(out.data() + 544, payload.constData(), payload.size());
 SHA256(reinterpret_cast<const uchar*>(payload.constData()), payload.size(), reinterpret_cast<uchar*>(out.data() + 512));
 unsigned size = 0;
 assert(RSA_sign(NID_sha256, reinterpret_cast<const uchar*>(out.data() + 512), 32,
  reinterpret_cast<uchar*>(out.data()), &size, key) == 1 && size == 512);
 return out;
}
std::unique_ptr<RSA, decltype(&RSA_free)> key(int bits) {
 auto out = std::unique_ptr<RSA, decltype(&RSA_free)>(RSA_new(), RSA_free);
 auto e = std::unique_ptr<BIGNUM, decltype(&BN_free)>(BN_new(), BN_free);
 assert(e && BN_set_word(e.get(), RSA_F4) && out && RSA_generate_key_ex(out.get(), bits, e.get(), nullptr));
 return out;
}
std::string publicPem(RSA *key) {
 auto bio = std::unique_ptr<BIO, decltype(&BIO_free)>(BIO_new(BIO_s_mem()), BIO_free);
 assert(PEM_write_bio_RSAPublicKey(bio.get(), key));
 char *data = nullptr; auto length = BIO_get_mem_data(bio.get(), &data);
 return std::string(data, size_t(length));
}
void lane(Channel channel) {
 Core::BuildUpdateChannel = channel;
 Core::BuildIsCanary = channel == Channel::CanaryPublic || channel == Channel::CanaryPrivate;
}
int add(int parent, const char *name, mode_t mode = S_IFREG | 0755, int owner = 0, bool openable = true) {
 int id = nextId++; FakeStat info{mode, owner, 1, 12, 1, id};
 directories[parent].push_back({name, info, id, openable}); attributes[id] = info;
 if (S_ISDIR(mode)) directories[id] = {};
 return id;
}
void resetFs() { directories.clear(); attributes.clear(); nextId = 100; directories[1] = {}; attributes[1] = FakeStat{S_IFDIR | 0755}; }
bool inventory(const char *executable, std::vector<ExpectedFile> &files) {
 QSet<QByteArray> destinations; QString error;
 return InventoryDirectory(1, {}, executable, files, destinations, error);
}
int main() {
 QString error;
#ifdef TDESKTOP_DISABLE_AUTOUPDATE
 assert(!RootModeSupported(error));
 assert(!VerifyLegacyUpdate({}, nullptr, Channel::Stable, &error) && !decodeCalls);
 std::cout << "PASS: automatic-update-disabled helper/root mode fail closed\n";
#else
 auto signer = key(4096), wrong = key(4096), small = key(2048);
 const auto pem = publicPem(signer.get()), wrongPem = publicPem(wrong.get()), smallPem = publicPem(small.get());
 UpdatesPublicKey = pem.c_str();
 const auto inner = body(LuxuryUpdateVersion + 1);
 const auto package = sign(signer.get(), compress(inner));
 lane(Channel::Stable);
 auto stable = VerifyPackage(package, false, error);
 assert(stable && stable->version == MakeUpdateVersion(LuxuryUpdateVersion + 1, 0) && stable->uncompressed == inner);
 assert(stable->version < Core::RunningUpdateVersion()); // Product counter, not AppVersion.
 assert(decodeCalls == 1);
 auto rejectedBeforeDecode = [&](const QByteArray &bytes, const char *publicKey, Channel channel) {
  const auto before = decodeCalls; assert(!VerifyLegacyUpdate(bytes, publicKey, channel, &error)); assert(decodeCalls == before);
 };
 auto changed = package; changed.bytes.back() ^= 1;
 rejectedBeforeDecode(changed, pem.c_str(), Channel::Stable);
 changed = package; changed.bytes[0] ^= 1; rejectedBeforeDecode(changed, pem.c_str(), Channel::Stable);
 changed = package; changed.bytes[512] ^= 1; rejectedBeforeDecode(changed, pem.c_str(), Channel::Stable);
 rejectedBeforeDecode(package, wrongPem.c_str(), Channel::Stable);
 rejectedBeforeDecode(package, smallPem.c_str(), Channel::Stable);
 rejectedBeforeDecode(package, nullptr, Channel::Stable);
 rejectedBeforeDecode(QByteArray(544, Qt::Uninitialized), pem.c_str(), Channel::Stable);
 for (auto channel : {Channel::CanaryPublic, Channel::CanaryPrivate}) {
  lane(channel); const auto before = decodeCalls;
  assert(!VerifyPackage(package, false, error) && decodeCalls == before);
  rejectedBeforeDecode(package, pem.c_str(), channel);
 }
 assert(!RootModeSupported(error)); lane(Channel::CanaryPublic); assert(RootModeSupported(error)); lane(Channel::Stable);
 for (auto version : {quint32(LuxuryUpdateVersion), quint32(LuxuryUpdateVersion - 1), quint32(0x80000000U)})
  assert(!VerifyPackage(sign(signer.get(), compress(body(version))), false, error));
 auto alpha = body(0x7FFFFFFF); alpha.bytes.resize(4); word(alpha, 1000021002ULL, 8);
 assert(!VerifyPackage(sign(signer.get(), compress(alpha)), false, error));
 auto compressed = compress(inner); qint32 size = 1024 * 1024 * 1024 + 1; memcpy(compressed.data(), &size, 4);
 auto before = decodeCalls; assert(!VerifyPackage(sign(signer.get(), compressed), false, error) && decodeCalls == before);
 compressed = compress(inner); size = -1; memcpy(compressed.data(), &size, 4);
 before = decodeCalls; assert(!VerifyPackage(sign(signer.get(), compressed), false, error) && decodeCalls == before);
 compressed = compress(inner); compressed.bytes.pop_back(); assert(!VerifyPackage(sign(signer.get(), compressed), false, error));
 compressed = compress(inner); compressed.bytes += "bad"; assert(!VerifyPackage(sign(signer.get(), compressed), false, error));
 compressed = compress(inner); size = inner.size() + 1; memcpy(compressed.data(), &size, 4);
 assert(!VerifyPackage(sign(signer.get(), compressed), false, error));
 assert(!VerifyPackage(sign(signer.get(), compress(QByteArray("x"))), false, error));
 // A rejected v2-looking package never reaches decode via unsigned-v1 fallback.
 acceptV2 = false; before = decodeCalls; assert(!VerifyPackage("TDUPinvalid", false, error) && decodeCalls == before);
 offeredV2.envelope.payload = compress(body(7002010)); offeredV2.envelope.version = MakeUpdateVersion(7002010, 12);
 offeredV2.envelope.channel = Channel::CanaryPublic; acceptV2 = true; lane(Channel::CanaryPublic);
 before = decodeCalls; assert(VerifyPackage("TDUPfixture", false, error) && decodeCalls == before + 1);
 for (auto channel : {Channel::Stable, Channel::Beta, Channel::CanaryPrivate}) {
  offeredV2.envelope.channel = channel; before = decodeCalls;
  assert(!VerifyPackage("TDUPfixture", false, error) && decodeCalls == before);
 }
 offeredV2.envelope.channel = Channel::CanaryPublic; offeredV2.envelope.version = Core::RunningUpdateVersion();
 before = decodeCalls; assert(!VerifyPackage("TDUPfixture", false, error) && decodeCalls == before);
 offeredV2.envelope.version += 1; heldValid = false;
 before = decodeCalls; assert(!VerifyPackage("TDUPfixture", false, error) && decodeCalls == before); heldValid = true;
 assert(v2Calls >= 6);
 // Actual staged inventory, canonical-only remap, collision and trust checks.
 resetFs(); add(1, "LuxuryGram"); add(1, "Updater"); add(1, "Telegram");
 auto resources = add(1, "resources", S_IFDIR | 0755); add(resources, "LuxuryGram"); add(1, "ready", S_IFREG | 0644);
 std::vector<ExpectedFile> files;
 assert(CheckRequired(1) == 0 && inventory("Renamed", files));
 std::set<std::string> paths; for (const auto &file : files) paths.insert(file.path.bytes);
 assert(paths == (std::set<std::string>{"Renamed", "Updater", "Telegram", "resources/LuxuryGram"}));
 for (const auto alias : {"Renamed", "renamed", "Luxurygram", "luxurygram", "UPDATER", "Ready", "TDATA"}) {
  resetFs(); add(1, "LuxuryGram"); add(1, "Updater"); add(1, alias); files.clear(); assert(!inventory("Renamed", files));
 }
 resetFs(); add(1, "Telegram"); add(1, "Updater"); assert(CheckRequired(1) != 0);
 for (auto mode : {S_IFDIR | 0755, S_IFLNK | 0755, S_IFREG | 0644, S_IFREG | 0775}) {
  resetFs(); add(1, "LuxuryGram", mode); add(1, "Updater"); files.clear(); assert(CheckRequired(1) != 0 || !inventory("Renamed", files));
 }
 resetFs(); add(1, "LuxuryGram", S_IFREG | 0755, 1000); add(1, "Updater"); files.clear(); assert(CheckRequired(1) != 0 && !inventory("Renamed", files));
 resetFs(); add(1, "LuxuryGram", S_IFREG | 0755, 0, false); add(1, "Updater"); files.clear(); assert(!inventory("Renamed", files));
 assert(!SafeExecutableName("Updater") && !SafeExecutableName("UPDATER") && !SafeExecutableName("ready") && !SafeExecutableName("tdata"));
 assert(SafeExecutableName("Renamed") && !SafeExecutableName("..") && !SafeExecutableName("bad\\name"));
 // Actual preflight rejects symlinks/non-root/untrusted ancestors.
 resetFs(); assert(PreflightDestination(1, "missing", error)); add(1, "ok", S_IFREG | 0644);
 assert(PreflightDestination(1, "ok", error)); add(1, "link", S_IFLNK | 0755); assert(!PreflightDestination(1, "link", error));
 add(1, "user", S_IFREG | 0644, 1000); assert(!PreflightDestination(1, "user", error));
 add(1, "writeable", S_IFREG | 0664); assert(!PreflightDestination(1, "writeable", error));
 auto trusted = add(1, "trusted", S_IFDIR | 0755); add(trusted, "file", S_IFREG | 0644);
 assert(PreflightDestination(1, "trusted/file", error)); add(1, "unsafe", S_IFDIR | 0775); assert(!PreflightDestination(1, "unsafe/file", error));
 assert(nofollowCalls > 0);
 std::cout << "PASS: stable-v1 real RSA4096/SHA256+XZ; auth-before-decode/corrupt/wrong-key/downgrade/bounds; protected canary lane, LuxuryGram-only inventory/collisions/trust/nofollow (explicit v2+FS doubles)\n";
#endif
}
'''
parts = {
    'UNPACK_INTERNALS': between(unpack, 'void SetError(', '[[nodiscard]] bool SafeName('),
    'UNPACK_HELPER': helper,
    'V2_MAGIC': between(verify, 'bool IsV2UpdateFile(', 'std::optional<Envelope> ParseEnvelope('),
    'ROOT_SUPPORT': between(install, '[[nodiscard]] bool RootModeSupported(', '#ifndef TDESKTOP_DISABLE_AUTOUPDATE\n\nconstexpr'),
    'PACKAGE': between(install, 'struct VerifiedPackage {', 'PrivateDirectory::~PrivateDirectory()'),
    'FAIL_HELPER': between(install, '[[nodiscard]] bool Fail(', 'void ReportProgress('),
    'TRUST_HELPERS': between(install, '[[nodiscard]] bool TrustedDirectory(', '[[nodiscard]] FileDescriptor OpenTrustedDirectory('),
    'SAFE_HELPERS': between(install, '[[nodiscard]] bool SafePathComponent(', '[[nodiscard]] std::optional<Installation> OpenInstallation('),
    'TRUSTED_OPEN': between(install, '[[nodiscard]] FileDescriptor OpenTrustedDirectory(', '[[nodiscard]] bool CurrentExecutableMatches('),
    'INVENTORY': between(install, '[[nodiscard]] bool InventoryDirectory(', '[[nodiscard]] bool PreflightDestination('),
    'PREFLIGHT': between(install, '[[nodiscard]] bool PreflightDestination(', '[[nodiscard]] bool FileMatches('),
    'REQUIRED': required,
}
# TrustedFile is a separate production predicate used by both inventory/preflight.
parts['TRUST_HELPERS'] += between(install, '[[nodiscard]] bool TrustedFile(', '[[nodiscard]] QByteArray FileDigest(')
# Balance the selected #ifndef around the unpack-only implementation internals.
parts['UNPACK_INTERNALS'] += '\n#endif // !TDESKTOP_DISABLE_AUTOUPDATE\n'
cpp = re.sub(r'(?m)^(' + '|'.join(parts) + r')$', lambda match: parts[match[1]], cpp)

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--cxx', default=os.environ.get('CXX') or shutil.which('g++') or r'C:\Strawberry\c\bin\g++.exe')
args = parser.parse_args()
compiler = Path(args.cxx)
includes = compiler.parent.parent / 'include'
libraries = compiler.parent.parent / 'lib'
with tempfile.TemporaryDirectory(prefix='luxury-protected-contract-') as directory:
    work = Path(directory)
    qt = work / 'QtCore'; qt.mkdir()
    (qt / 'QByteArray').write_text(qbyte, encoding='utf-8')
    (qt / 'QString').write_text(qstring, encoding='utf-8')
    test = work / 'protected.cpp'; test.write_text(cpp, encoding='utf-8')
    for disabled in (False, True):
        exe = work / ('protected-disabled.exe' if disabled else 'protected.exe')
        command = [str(compiler), '-std=c++20', '-O0', '-g', '-D_DEBUG', '-Wno-deprecated-declarations',
                   '-I', str(work), '-I', str(SOURCE), '-I', str(includes), str(test),
                   '-L', str(libraries), '-lcrypto', '-llzma', '-o', str(exe)]
        if disabled:
            command.insert(1, '-DTDESKTOP_DISABLE_AUTOUPDATE')
        print('COMPILE:', subprocess.list2cmdline(command), flush=True)
        subprocess.run(command, check=True)
        env = os.environ.copy(); env['PATH'] = str(compiler.parent) + os.pathsep + env.get('PATH', '')
        subprocess.run([str(exe)], check=True, env=env)
for path in ('core/update_unpack.cpp', 'core/update_unpack.h', 'platform/linux/update_install_linux.cpp'):
    print('SHA256', path, hashlib.sha256((SOURCE / path).read_bytes()).hexdigest())
print('Remaining native gap: full Linux Qt/v2 signatures/root staging+apply/relaunch; production private key never accessed.')
