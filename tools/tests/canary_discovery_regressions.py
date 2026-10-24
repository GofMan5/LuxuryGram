"""Compile the actual canary selection/no-update block with thin JSON doubles.

Run: python tools/tests/canary_discovery_regressions.py [--cxx g++]
Uses the bundled nlohmann JSON header and an O0 standalone C++ harness; no
native application build, network, account, profile, or package unpacking.
Authentication order, transport routing and private enrollment are source checks.
"""
from pathlib import Path
import argparse
import os
import re
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "Telegram/SourceFiles/core/update_checker.cpp"


def masked(text):
    """Keep offsets/braces while excluding comments and quoted literals."""
    return re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
                  lambda match: " " * len(match.group()), text, flags=re.S)


def function(text, marker):
    start = text.index(marker)
    clean = masked(text)
    opening = clean.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        if clean[end] == "{":
            depth += 1
        elif clean[end] == "}":
            depth -= 1
        end += 1
    return text[start:end]


def compact(text):
    return re.sub(r"\s+", "", re.sub(r"//[^\n]*|/\*.*?\*/", "", text, flags=re.S))


def source_contracts(checker):
    unpack = compact(function(checker, "[[nodiscard]] bool UnpackUpdateV2("))
    verify = unpack.index("constautoverified=Updates::VerifyUpdate(")
    reject = unpack.index("if(!verified){", verify)
    lane = unpack.index("if(BuildIsCanary&&verified->envelope.channel!=BuildUpdateChannel){")
    decompress = unpack.index("Updates::DecompressUpdatePayload(")
    assert verify < reject < lane < decompress, "Authenticate and reject foreign canary lane before decompression"
    assert "content,BuildUpdateChannel," in unpack[verify:reject]
    assert "returnfalse;" in unpack[reject:lane]
    assert "returnfalse;" in unpack[lane:unpack.index("if(verified->adoptManifest)", lane)]

    start = compact(function(checker, "void Updater::start(bool forceWait)"))
    assert re.search(
        r"elseif\(sendRequest\)\{ifconstexpr\(BuildIsCanary\)\{"
        r"startImplementation\(&_httpImplementation,std::make_unique<MtpChecker>\(LookupCanarySession\(\),_testing\)\);"
        r"\}else\{startImplementation\(&_httpImplementation,std::make_unique<HttpChecker>\(_testing\)\);\}",
        start), "Actual Updater::start must select MTP for canaries, HTTP otherwise"

    lookup = compact(function(checker, "[[nodiscard]] base::weak_ptr<Main::Session> LookupCanarySession()"))
    assert "if(!IsAppLaunched()||!App().domain().started()){return{};}" in lookup
    assert "ifconstexpr(BuildUpdateChannel==Updates::Channel::CanaryPrivate){" in lookup
    assert "ifconstexpr(CanaryPrivateChannelId!=0){" in lookup
    assert "for(constauto&[index,account]:App().domain().accounts())" in lookup
    assert "account->maybeSession()" in lookup
    assert "session->data().channelLoaded(ChannelId(BareId(CanaryPrivateChannelId)))" in lookup
    assert "if(channel&&channel->amIn()){returnbase::make_weak(session);}" in lookup
    assert "return{};}returnbase::make_weak(App().activeAccount().maybeSession());" in lookup

    canary = compact(function(checker, "void MtpChecker::startCanary()"))
    public, private = canary.split("}elseifconstexpr(CanaryPrivateChannelId!=0){", 1)
    assert "ifconstexpr(BuildUpdateChannel==Updates::Channel::CanaryPublic)" in public
    assert "MTP::ResolveChannel(" in public and "MTP::ResolveChannel(" not in private
    assert "session->data().channelLoaded(ChannelId(BareId(CanaryPrivateChannelId)))" in private
    membership = private.index("if(!channel||!channel->amIn()){")
    request = private.index("requestCanaryMetadata(channel->inputChannel(),")
    assert membership < request and "return;" in private[membership:request]


CPP = r'''
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <utility>
#include "luxury/libs/json.hpp"
using uchar = unsigned char;
#define Unexpected(...) std::abort()
using quint32 = uint32_t;
using quint64 = uint64_t;
using QByteArray = std::string;
#define QByteArrayLiteral(text) QByteArray(text)
using QString = std::u16string;
using Json = nlohmann::json;
QString operator""_q(const char16_t *text, size_t size) { return {text, size}; }
const QByteArray &QLatin1String(const QByteArray &name) { return name; }
struct QJsonObject;
struct QJsonValue {
    Json data;
    bool isDouble() const { return data.is_number(); }
    double toDouble() const { return isDouble() ? data.get<double>() : 0.; }
    QJsonObject toObject() const;
};
struct QJsonObject {
    Json data = Json::object();
    QJsonValue value(const QByteArray &key) const {
        const auto it = data.find(key);
        return {it == data.end() ? Json() : *it};
    }
    QJsonValue value(const QString &key) const {
        return value(QByteArray(key.begin(), key.end()));
    }
    bool isEmpty() const { return data.empty(); }
};
QJsonObject QJsonValue::toObject() const { return {data.is_object() ? data : Json::object()}; }
namespace Updates {
ENUM_HERE
VERSION_HERE
NAME_HERE
}
namespace Platform { QByteArray AutoUpdateKey() { return "win64"; } }
quint64 runningVersion = 0;
quint64 RunningUpdateVersion() { return runningVersion; }
struct Selection { quint64 version = 0; int post = 0; int noUpdate = 0; } selected;
void done(std::nullptr_t) { ++selected.noUpdate; }
template<Updates::Channel BuildUpdateChannel>
void select(const QJsonObject &object) {
SELECTION_HERE
    selected.version = bestVersion;
    selected.post = bestPostId;
}
size_t checks = 0;
template<Updates::Channel Lane>
void expect(const Json &document, quint64 running, quint64 version, int post = 0) {
    ++checks;
    selected = {};
    runningVersion = running;
    // Parse actual JSON text; numbers obey Qt's double conversion in the double.
    select<Lane>({Json::parse(document.dump())});
    assert(selected.version == version);
    assert(selected.post == post);
    assert(selected.noUpdate == (version ? 0 : 1));
}
Json entry(Json base, Json counter, Json post) {
    return {{"base", base}, {"counter", counter}, {"posts", {{"win64", post}}}};
}
template<Updates::Channel Lane>
void suite() {
    constexpr auto other = Lane == Updates::Channel::CanaryPublic
        ? Updates::Channel::CanaryPrivate : Updates::Channel::CanaryPublic;
    const auto own = Updates::ChannelName(Lane);
    const auto foreign = Updates::ChannelName(other);
    const auto current = Updates::MakeUpdateVersion(1000020, 7);
    const auto next = Updates::MakeUpdateVersion(1000020, 8);
    auto document = [&](Json value) { return Json{{"channels", {{own, value}}}}; };
    auto valid = document(entry(1000020, 8, 123));
    expect<Lane>(valid, current, next, 123);
    expect<Lane>(valid, next, 0); // Equal version is a successful no-update.
    expect<Lane>(valid, next + 1, 0);
    expect<Lane>(document(entry(1000020, 6, 123)), current, 0);
    expect<Lane>(document(entry(1000019, 4294967295ULL, 123)), current, 0);
    expect<Lane>(document(entry(1000021, 1, 124)),
        Updates::MakeUpdateVersion(1000020, 4294967295U),
        Updates::MakeUpdateVersion(1000021, 1), 124);
    // Full 64-bit output must not be combined through a JSON/double value.
    expect<Lane>(document(entry(4294967295ULL, 4294967295ULL, 2147483647)),
        0, std::numeric_limits<quint64>::max(), 2147483647);
    expect<Lane>(document(entry(1, 1, 1)), 0, (quint64(1) << 32) | 1, 1);
    auto mixed = valid;
    mixed["channels"][foreign] = entry(4294967295ULL, 4294967295ULL, 2147483647);
    mixed["channels"][Updates::ChannelName(Updates::Channel::Stable)] = entry(4294967295ULL, 4294967295ULL, 123);
    mixed["channels"][Updates::ChannelName(Updates::Channel::Beta)] = entry(4294967295ULL, 4294967295ULL, 123);
    expect<Lane>(mixed, current, next, 123); // Huge foreign lanes are ignored.
    mixed["channels"].erase(own);
    expect<Lane>(mixed, 0, 0);
    mixed["channels"][own] = entry(0, 8, 123);
    expect<Lane>(mixed, 0, 0); // Invalid own lane never falls back to foreign.
    for (const auto *field : {"base", "counter", "post"}) {
        // Zero, fractions, negative, U32 overflow and every JSON wrong type.
        for (const auto &bad : Json::array({0, 1.5, -1, 4294967296ULL,
                1e100, "8", true, false, nullptr, Json::object(), Json::array()})) {
            auto value = entry(1000020, 8, 123);
            if (std::string(field) == "post") value["posts"]["win64"] = bad;
            else value[field] = bad;
            expect<Lane>(document(value), 0, 0);
        }
        auto value = entry(1000020, 8, 123);
        if (std::string(field) == "post") value["posts"].erase("win64");
        else value.erase(field);
        expect<Lane>(document(value), 0, 0);
    }
    for (const auto post : {2147483648ULL, 4294967295ULL}) {
        expect<Lane>(document(entry(1000020, 8, post)), 0, 0);
    }
    expect<Lane>(document(entry(1000020, 8, 2147483647)), current, next, 2147483647);
    for (const auto &bad : Json::array({nullptr, false, 42, "entry", Json::array(), Json::object()})) {
        expect<Lane>(document(bad), 0, 0);
        expect<Lane>(Json{{"channels", bad}}, 0, 0);
        auto value = entry(1000020, 8, 123);
        value["posts"] = bad;
        expect<Lane>(document(value), 0, 0);
    }
    expect<Lane>(Json::object(), 0, 0);
    auto missingPosts = entry(1000020, 8, 123); missingPosts.erase("posts");
    expect<Lane>(document(missingPosts), 0, 0);
    auto wrongPlatform = entry(1000020, 8, 123);
    wrongPlatform["posts"] = {{"linux", 123}};
    expect<Lane>(document(wrongPlatform), 0, 0);
}
int main() {
    suite<Updates::Channel::CanaryPublic>();
    suite<Updates::Channel::CanaryPrivate>();
    std::cout << "PASS: " << checks << " actual canary selection/no-update cases (both lanes, O0, bundled JSON); "
        "source auth-before-decompress, Updater::start MTP/HTTP, private enrollment contracts\n";
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cxx", default=os.environ.get("CXX") or shutil.which("g++") or r"C:\Strawberry\c\bin\g++.exe")
    args = parser.parse_args()
    checker = SOURCE.read_text(encoding="utf-8")
    source_contracts(checker)
    metadata = function(checker, "void MtpChecker::parseCanaryMetadata(")
    first = metadata.index('const auto channels =')
    consider = metadata.index('consider(Updates::ChannelName(BuildUpdateChannel));', first)
    no_update = metadata.index('if (!bestVersion || bestVersion <= RunningUpdateVersion())', consider)
    end = masked(metadata).index('}', no_update) + 1
    selection = metadata[first:end]
    assert 'done(nullptr);' in selection and 'return;' in selection
    header = (ROOT / "Telegram/SourceFiles/core/update_verify.h").read_text(encoding="utf-8")
    implementation = (ROOT / "Telegram/SourceFiles/core/update_verify.cpp").read_text(encoding="utf-8")
    enum = re.search(r'enum class Channel\s*[^\{]*\{[^}]*\};', header)
    assert enum, "Production channel enum missing"
    version = function(header, "[[nodiscard]] constexpr quint64 MakeUpdateVersion(")
    name = function(implementation, "QByteArray ChannelName(")
    cpp = CPP.replace("ENUM_HERE", enum.group()).replace("VERSION_HERE", version)
    cpp = cpp.replace("NAME_HERE", name).replace("SELECTION_HERE", selection)
    with tempfile.TemporaryDirectory(prefix="luxury-canary-discovery-") as directory:
        work = Path(directory)
        source = work / "canary.cpp"
        source.write_text(cpp, encoding="utf-8")
        exe = work / ("canary.exe" if os.name == "nt" else "canary")
        subprocess.run([args.cxx, "-std=c++20", "-O0", "-D_DEBUG", "-I",
                        str(ROOT / "Telegram/SourceFiles"), str(source), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    main()
