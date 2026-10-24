"""Source-bound G5/G8 regressions; synthetic settings only, never a real profile.

python tools/tests/ghost_settings_regressions.py [--cxx g++]
Compiles extracted production ghost class/decoder, sanitizer, load/write,
account loop and settings header refresh/aggregate/click with bundled JSON and
real rpl. Qt file/logging/application doubles
are in memory; root schema contains routing/ads/account keys only. Exercises actual
startup -ghost helper/resolver and load exits; weak-pointer base remains a double.
Does not verify a full Qt build, native UI, file permissions or network behavior.
"""
from pathlib import Path
import argparse
import os
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SOURCE = (ROOT / 'Telegram/SourceFiles/luxury/luxury_settings.cpp').read_text('utf-8')
HEADER = (ROOT / 'Telegram/SourceFiles/luxury/luxury_settings.h').read_text('utf-8')


def function(signature):
    start = SOURCE.index(signature)
    end = SOURCE.index('\n}', start) + 2
    return SOURCE[start:end]


def between(text, start, end):
    return text[text.index(start):text.index(end, text.index(start))]


ghost_class = between(HEADER, 'class GhostModeAccountSettings', '\nvoid to_json')
enum = re.search(r'enum class SendWithoutSoundOption \{.*?\n\};', HEADER, re.S).group()
enum_json = re.search(r'NLOHMANN_JSON_SERIALIZE_ENUM\(SendWithoutSoundOption, \{.*?\n\}\)', SOURCE, re.S).group()
account_loop = between(function('void from_json(const nlohmann::json &j, LuxurySettings &s)'),
                       '\tif (j.contains("ghostModeSettings")', '\n\ts._useGlobalGhostMode')
account_writer = between(function('void to_json(nlohmann::json &j, const LuxurySettings &s)'),
                         '\tauto ghostAccounts', '\n\tj = ')
setter_names = ('SendReadMessages', 'SendReadStories', 'SendOnlinePackets',
                'SendUploadProgress', 'SendOfflinePacketAfterOnline')
setters = '\n'.join(function(f'void GhostModeAccountSettings::set{name}{suffix}(bool val)')
                    for name in setter_names for suffix in ('', 'Locked'))
UI = (ROOT / 'Telegram/SourceFiles/luxury/ui/settings/settings_luxury_utils.cpp').read_text('utf-8')
refresh = between(UI, '\t\t.refresh = [cState] {', '\n\t\t},').split('{', 1)[1]
counts = between(UI, '\tconst auto countUnlockedChecked', '\n\tfor (const auto &innerCheck')
aggregate = re.search(r'const auto total = countTotal\(\);.*?\n\s*\},', UI, re.S).group().rsplit('\n', 1)[0]
click = re.search(r'const auto checked = !checkView->checked\(\);.*?\n\s*\},', UI, re.S).group().rsplit('\n', 1)[0]

CPP = r'''
#include "luxury/libs/json.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <gsl/util>
#include <string>
#include <utility>
#include <vector>
#include "rpl/variable.h"
#include "rpl/combine.h"
namespace base::assertion { void log(const char*, const char*, int) {} }
namespace base { struct has_weak_ptr {}; }
using json = nlohmann::json;
using uint64 = std::uint64_t;
namespace crl { using time = std::int64_t; }
struct QString {
    std::string text;
    QString() = default;
    QString(const char *s) : text(s) {}
    QString(std::string s) : text(std::move(s)) {}
    static QString fromStdString(const std::string &s) { return s; }
    static QString number(int n) { return std::to_string(n); }
    auto operator<=>(const QString &) const = default;
    QString operator+(const QString &other) const { return text + other.text; }
    QString operator+(char c) const { return text + c; }
};
QString operator""_q(const char16_t *s, std::size_t n) {
    return std::string(s, s + n);
}
struct QByteArray {
    std::string text;
    static QByteArray fromStdString(const std::string &s) { return {s}; }
    int size() const { return int(text.size()); }
    const char *constData() const { return text.data(); }
};
std::vector<std::string> logs;
// Keep logging observable without depending on QString::arg().
#define LOG(value) logs.push_back(#value)
namespace QIODevice { constexpr auto ReadOnly = 1; constexpr auto WriteOnly = 2; }
struct QFile {
    static inline std::map<QString, std::string> files;
    static inline bool cannotOpen = false, cannotRename = false;
    QString path;
    explicit QFile(QString p) : path(std::move(p)) {}
    bool open(int) { return !cannotOpen && files.contains(path); }
    void close() {}
    int size() const { return int(files.at(path).size()); }
    QByteArray read(int n) const { return {files.at(path).substr(0, n)}; }
    static bool exists(const QString &p) { return files.contains(p); }
    static bool rename(const QString &from, const QString &to) {
        if (cannotRename || !files.contains(from) || files.contains(to)) return false;
        files[to] = files.at(from); files.erase(from); return true;
    }
};
struct QSaveFile {
    static inline int writes = 0;
    QString path;
    std::string data;
    explicit QSaveFile(QString p) : path(std::move(p)) { ++writes; }
    bool open(int) { return true; }
    int write(const QByteArray &b) { data = b.text; return b.size(); }
    bool commit() { QFile::files[path] = data; return true; }
};
QString cWorkingDir() { return {}; }
bool cliGhost = false;
bool cGhost() { return cliGhost; }
namespace LuxuryWorker { void wake() {} }
constexpr auto kMaxSettingsBytes = 4 * 1024 * 1024;
constexpr auto kMaxGhostAccounts = std::size_t(64);
@@ENUM@@
@@ENUM_JSON@@
@@GHOST_CLASS@@
void to_json(nlohmann::json &j, const GhostModeAccountSettings &s);
void from_json(const nlohmann::json &j, GhostModeAccountSettings &s);
// Minimal root schema/storage only; account logic and load/write are production.
struct LuxurySettings {
    std::map<uint64, std::unique_ptr<GhostModeAccountSettings>> _ghostAccounts;
    rpl::variable<bool> _useGlobalGhostMode = true, _disableAds = true;
    static LuxurySettings &getInstance() { static LuxurySettings s; return s; }
    static GhostModeAccountSettings &ghost() {
        auto &p = getInstance()._ghostAccounts[0];
        if (!p) p = std::make_unique<GhostModeAccountSettings>();
        return *p;
    }
    static GhostModeAccountSettings &ghostProfile(uint64);
    static void applyStartupGhost(GhostModeAccountSettings &);
    static void load();
    static void save() {}
    void validate() {}
};
void to_json(nlohmann::json &j, const LuxurySettings &s) {
@@ACCOUNT_WRITER@@
    j = {{"ghostModeSettings", ghostAccounts},
         {"useGlobalGhostMode", s._useGlobalGhostMode.current()},
         {"disableAds", s._disableAds.current()}};
}
void from_json(const nlohmann::json &j, LuxurySettings &s) {
@@ACCOUNT_LOOP@@
    s._useGlobalGhostMode = j.value("useGlobalGhostMode", true);
    s._disableAds = j.value("disableAds", true);
}
@@GHOST_CONSTRUCTOR@@
@@SETTERS@@
@@GHOST_WRITER@@
@@GHOST_DECODER@@
@@PATH@@
@@WRITE_FLAG@@
@@WRITE@@
@@SANITIZER@@
@@LOAD@@
@@STARTUP@@
@@RESOLVER@@

namespace anim { enum class type { normal }; }
struct CheckView {
    bool value = false;
    rpl::event_stream<bool> changes;
    bool checked() const { return value; }
    void setChecked(bool b, anim::type) {
        if (value != b) { value = b; changes.fire_copy(b); }
    }
};
struct QGraphicsOpacityEffect {
    explicit QGraphicsOpacityEffect(void *) {}
    void setOpacity(double) {}
};
struct Checkbox {
    CheckView *view = nullptr;
    int lockRefreshes = 0;
    void setChecked(bool value) { assert(view); view->setChecked(value, anim::type::normal); }
    void refreshLockState() { ++lockRefreshes; }
    void setGraphicsEffect(QGraphicsOpacityEffect *p) { delete p; }
};
struct CollapsibleState {
    struct Entry { CheckView *checkView; Checkbox *checkbox; };
    struct Value {
        std::function<bool()> getter, lockGetter;
    };
    std::vector<Entry> entries;
    std::vector<Value> checkboxes;
    rpl::event_stream<> lockChanges;
    std::vector<CheckView *> innerChecks;
    std::vector<std::function<bool()>> lockChecks;
};
void uiRefresh(CollapsibleState *cState) {
@@UI_REFRESH@@
}
bool uiAggregate(CollapsibleState *state) {
@@UI_COUNTS@@
    CheckView output;
    const auto checkView = &output;
    const auto toggledWhenAll = true;
    const auto count = countUnlockedChecked();
@@UI_AGGREGATE@@
    return output.checked();
}
void uiClick(CollapsibleState *state, CheckView *checkView) {
@@UI_CLICK@@
}
int uiCases() {
    bool profileLocked = false;
    bool saved[5] = {true, true, true, true, false};
    CheckView checks[5], header;
    Checkbox widgets[5];
    CollapsibleState state;
    rpl::lifetime lifetime;
    for (auto i = 0; i != 5; ++i) {
        widgets[i].view = &checks[i];
        state.entries.push_back({&checks[i], &widgets[i]});
        state.checkboxes.push_back({[&, i] { return saved[i]; },
                                   [&, i] { return i == 4 && profileLocked; }});
        state.innerChecks.push_back(&checks[i]);
        state.lockChecks.push_back(state.checkboxes.back().lockGetter);
        checks[i].changes.events() | rpl::on_next([&](bool) {
            header.setChecked(uiAggregate(&state), anim::type::normal);
        }, lifetime);
    }
    state.lockChanges.events() | rpl::on_next([&] {
        header.setChecked(uiAggregate(&state), anim::type::normal);
    }, lifetime);
    uiRefresh(&state); assert(!header.checked());
    profileLocked = true; uiRefresh(&state); assert(header.checked());
    // Correct header intent switches unlocked checked options off, not a no-op.
    uiClick(&state, &header);
    for (auto i = 0; i != 5; ++i) assert(!checks[i].checked());
    uiRefresh(&state); assert(header.checked());
    profileLocked = false; uiRefresh(&state); assert(!header.checked());
    for (const auto &widget : widgets) assert(widget.lockRefreshes == 4);
    uiClick(&state, &header);
    for (auto i = 0; i != 5; ++i) assert(checks[i].checked());
    return 3;
}

json strictProfile() {
    return {{"sendReadMessages", false}, {"sendReadStories", false},
            {"sendOnlinePackets", false}, {"sendUploadProgress", false},
            {"sendOfflinePacketAfterOnline", true}};
}
json profileFile() {
    return {{"ghostModeSettings", {{"0", strictProfile()}, {"123", strictProfile()}}},
            {"useGlobalGhostMode", false}, {"disableAds", false}};
}
void fresh(const std::string &data) {
    QFile::files.clear(); QFile::cannotOpen = QFile::cannotRename = false;
    QSaveFile::writes = 0; logs.clear(); SettingsWriteBlocked = false;
    LuxurySettings::getInstance() = LuxurySettings();
    QFile::files[getSettingsPath()] = data;
}
void privacySurvives(const GhostModeAccountSettings &s, const std::string &bad = "") {
    if (bad != "sendReadMessages") assert(!s.sendReadMessages());
    if (bad != "sendReadStories") assert(!s.sendReadStories());
    if (bad != "sendOnlinePackets") assert(!s.sendOnlinePackets());
    if (bad != "sendUploadProgress") assert(!s.sendUploadProgress());
    if (bad != "sendOfflinePacketAfterOnline") assert(s.sendOfflinePacketAfterOnline());
}
void loadedPrivacy(const std::string &bad = "", uint64 id = 0) {
    auto &s = LuxurySettings::getInstance();
    assert(!s._useGlobalGhostMode.current() && !s._disableAds.current());
    assert(s._ghostAccounts.size() == 2);
    privacySurvives(*s._ghostAccounts.at(id), bad);
    privacySurvives(*s._ghostAccounts.at(id ? 0 : 123));
}
int main() {
    int cases = uiCases();
    // Real rpl subscriptions: same aggregate bool must still notify locks-only.
    GhostModeAccountSettings ghost;
    rpl::lifetime lifetime;
    std::vector<bool> events, changes;
    ghost.ghostModeActiveValue() | rpl::on_next([&](bool b) { events.push_back(b); }, lifetime);
    ghost.ghostModeActiveChanges() | rpl::on_next([&](bool b) { changes.push_back(b); }, lifetime);
    assert(events == std::vector<bool>{false} && changes.empty());
    const auto locks = std::vector<void (GhostModeAccountSettings::*)(bool)>{
        &GhostModeAccountSettings::setSendReadMessagesLocked,
        &GhostModeAccountSettings::setSendReadStoriesLocked,
        &GhostModeAccountSettings::setSendOnlinePacketsLocked,
        &GhostModeAccountSettings::setSendUploadProgressLocked,
        &GhostModeAccountSettings::setSendOfflinePacketAfterOnlineLocked};
    for (const auto lock : locks) {
        const auto before = events.size();
        (ghost.*lock)(true);
        assert(events.size() == before + 1 && changes.size() == before);
        (ghost.*lock)(true); // No redundant setter event for an unchanged lock.
        assert(events.size() == before + 1);
        (ghost.*lock)(false);
        assert(events.size() == before + 2 && !events.back()); ++cases;
    }
    from_json(strictProfile(), ghost);
    assert(ghost.isGhostModeActive() && events.back());
    ghost.setSendReadMessagesLocked(true);
    auto count = events.size();
    ghost.setSendReadStoriesLocked(true);
    assert(events.size() == count + 1 && events.back()); ++cases;

    fresh(profileFile().dump()); LuxurySettings::load(); loadedPrivacy();
    assert(!QFile::exists(getSettingsPath() + u".broken"_q));
    writeSettings(); assert(QSaveFile::writes == 1); ++cases;

    const std::vector<std::string> fields = {
        "sendReadMessages", "sendReadStories", "sendOnlinePackets", "sendUploadProgress",
        "sendOfflinePacketAfterOnline", "markReadAfterAction", "useScheduledMessages",
        "suggestGhostModeBeforeViewingStory", "sendReadMessagesLocked", "sendReadStoriesLocked",
        "sendOnlinePacketsLocked", "sendUploadProgressLocked", "sendOfflinePacketAfterOnlineLocked"};
    const std::vector<json> badTypes = {"bad", 7, nullptr, json::array(), json::object()};
    for (uint64 id : {uint64(0), uint64(123)}) {
        for (const auto &field : fields) for (const auto &bad : badTypes) {
            auto data = profileFile(); data["ghostModeSettings"][std::to_string(id)][field] = bad;
            const auto original = data.dump(); fresh(original);
            LuxurySettings::load(); loadedPrivacy(field, id);
            assert(!logs.empty() && !SettingsWriteBlocked);
            assert(QFile::files.at(getSettingsPath() + u".broken"_q) == original);
            writeSettings();
            assert(QFile::files.at(getSettingsPath() + u".broken"_q) == original);
            // Repaired output can be loaded with every unaffected restriction.
            LuxurySettings::getInstance() = LuxurySettings();
            LuxurySettings::load(); loadedPrivacy(field, id); ++cases;
        }
    }
    for (const auto &key : {"", "bad", "123x", "-1", "+1", "18446744073709551616"}) {
        auto data = profileFile(); data["ghostModeSettings"][key] = strictProfile();
        fresh(data.dump()); LuxurySettings::load(); loadedPrivacy();
        assert(QFile::exists(getSettingsPath() + u".broken"_q)); ++cases;
    }
    for (const auto &bad : badTypes) {
        if (bad.is_object()) continue; // Empty object is a valid default profile.
        auto data = profileFile(); data["ghostModeSettings"]["42"] = bad;
        fresh(data.dump()); LuxurySettings::load(); loadedPrivacy(); ++cases;
    }
    // uint64 boundaries and accepted leading-zero IDs retain compatibility.
    auto data = profileFile();
    data["ghostModeSettings"]["18446744073709551615"] = strictProfile();
    data["ghostModeSettings"]["007"] = strictProfile();
    fresh(data.dump()); LuxurySettings::load();
    privacySurvives(*LuxurySettings::getInstance()._ghostAccounts.at(UINT64_MAX));
    privacySurvives(*LuxurySettings::getInstance()._ghostAccounts.at(7)); ++cases;
    data = profileFile(); data["disableAds"] = "bad";
    fresh(data.dump()); LuxurySettings::load();
    assert(LuxurySettings::getInstance()._disableAds.current());
    privacySurvives(*LuxurySettings::getInstance()._ghostAccounts.at(0));
    privacySurvives(*LuxurySettings::getInstance()._ghostAccounts.at(123)); ++cases;
    data = profileFile(); data["futureSetting"] = json::array({1, 2});
    data["ghostModeSettings"]["0"]["futurePrivacyOption"] = "unknown";
    fresh(data.dump()); LuxurySettings::load(); loadedPrivacy();
    assert(!QFile::exists(getSettingsPath() + u".broken"_q)); ++cases;
    data = profileFile();
    for (auto id = 1000; id != 1100; ++id) data["ghostModeSettings"][std::to_string(id)] = strictProfile();
    fresh(data.dump()); LuxurySettings::load();
    auto &capped = LuxurySettings::getInstance()._ghostAccounts;
    assert(capped.size() == kMaxGhostAccounts);
    for (const auto &[id, account] : capped) privacySurvives(*account);
    writeSettings(); assert(QSaveFile::writes == 1); ++cases;

    for (const auto &sound : std::vector<json>{false, true, 0, 1, 2, "unknown"}) {
        auto data = profileFile(); data["ghostModeSettings"]["0"]["sendWithoutSound"] = sound;
        fresh(data.dump()); LuxurySettings::load(); loadedPrivacy();
        const auto actual = LuxurySettings::getInstance()._ghostAccounts.at(0)->sendWithoutSound();
        const auto expected = sound == true ? SendWithoutSoundOption::Always
            : sound == 1 ? SendWithoutSoundOption::InGhostMode
            : sound == 2 ? SendWithoutSoundOption::Always : SendWithoutSoundOption::Never;
        assert(actual == expected); ++cases;
    }
    for (const auto &bad : badTypes) {
        auto data = strictProfile(); data["sendUploadProgress"] = bad;
        data["sendWithoutSound"] = true; data["disableAds"] = false;
        fresh(data.dump()); const auto original = data.dump(); LuxurySettings::load();
        auto &s = LuxurySettings::getInstance();
        assert(s._useGlobalGhostMode.current() && !s._disableAds.current());
        privacySurvives(*s._ghostAccounts.at(0), "sendUploadProgress");
        assert(s._ghostAccounts.at(0)->sendWithoutSound() == SendWithoutSoundOption::Always);
        assert(QFile::files.at(getSettingsPath() + u".broken"_q) == original); ++cases;
    }
    // Preserve previous recovery copies byte-for-byte; choose a vacant suffix.
    data = profileFile(); data["ghostModeSettings"]["123"]["sendUploadProgress"] = "bad";
    auto original = data.dump(); fresh(original);
    QFile::files[getSettingsPath() + u".broken"_q] = "first recovery";
    QFile::files[getSettingsPath() + u".broken.1"_q] = "second recovery";
    LuxurySettings::load(); loadedPrivacy("sendUploadProgress", 123); writeSettings();
    assert(QFile::files.at(getSettingsPath() + u".broken"_q) == "first recovery");
    assert(QFile::files.at(getSettingsPath() + u".broken.1"_q) == "second recovery");
    assert(QFile::files.at(getSettingsPath() + u".broken.2"_q) == original); ++cases;

    fresh(original); QFile::cannotRename = true;
    LuxurySettings::load(); loadedPrivacy("sendUploadProgress", 123); writeSettings();
    assert(SettingsWriteBlocked && QSaveFile::writes == 0);
    assert(QFile::files.at(getSettingsPath()) == original); ++cases;
    fresh(original); QFile::cannotOpen = true; LuxurySettings::load(); writeSettings();
    assert(SettingsWriteBlocked && QSaveFile::writes == 0);
    assert(QFile::files.at(getSettingsPath()) == original); ++cases;
    for (const auto &root : {"{", "null", "[]", "7", "true", "\"bad\""}) {
        fresh(root); LuxurySettings::load(); writeSettings();
        assert(QFile::files.at(getSettingsPath() + u".broken"_q) == root); ++cases;
    }
    fresh(std::string(kMaxSettingsBytes + 1, 'x')); LuxurySettings::load(); writeSettings();
    assert(QFile::files.at(getSettingsPath() + u".broken"_q).size() == kMaxSettingsBytes + 1); ++cases;
    fresh(original); QFile::files.clear(); LuxurySettings::load(); writeSettings();
    assert(!SettingsWriteBlocked && QSaveFile::writes == 1); ++cases;
    cliGhost = true;
    for (const auto &bytes : {std::string(), std::string("broken"), std::string(kMaxSettingsBytes + 1, 'x'), std::string("null"), std::string(R"({"ghostModeSettings":{"0":{"sendReadMessages":true},"42":{"sendReadMessages":true}},"useGlobalGhostMode":false})")}) {
        fresh(bytes); if (bytes.empty()) QFile::files.clear(); LuxurySettings::load();
        auto &g = LuxurySettings::ghostProfile(0); auto &individual = LuxurySettings::ghostProfile(42);
        assert(!g.sendReadMessages() && !individual.sendReadMessages() && !individual.sendReadStories() && !individual.sendOnlinePackets() && !individual.sendUploadProgress() && individual.sendOfflinePacketAfterOnline());
        individual.setSendReadMessages(true); assert(LuxurySettings::ghostProfile(42).sendReadMessages()); ++cases;
    }
    fresh(original); QFile::cannotOpen = true; LuxurySettings::load(); assert(!LuxurySettings::ghostProfile(42).sendReadMessages()); ++cases;
    cliGhost = false;
    std::cout << "PASS: " << cases << " synthetic cases; actual ghost/load/write/sanitizer slices, bundled JSON and real rpl\n";
}
'''

parts = {
    'ENUM': enum, 'ENUM_JSON': enum_json, 'GHOST_CLASS': ghost_class,
    'ACCOUNT_WRITER': account_writer, 'ACCOUNT_LOOP': account_loop,
    'GHOST_CONSTRUCTOR': function('GhostModeAccountSettings::GhostModeAccountSettings()'),
    'SETTERS': setters,
    'GHOST_WRITER': function('void to_json(nlohmann::json &j, const GhostModeAccountSettings &s)'),
    'GHOST_DECODER': function('void from_json(const nlohmann::json &j, GhostModeAccountSettings &s)'),
    'PATH': function('QString getSettingsPath()'),
    'WRITE_FLAG': 'bool SettingsWriteBlocked = false;',
    'STARTUP': function('void LuxurySettings::applyStartupGhost('),
    'RESOLVER': function('GhostModeAccountSettings &LuxurySettings::ghostProfile('),
    'WRITE': function('void writeSettings()'),
    'SANITIZER': function('bool dropMismatchedKeys('),
    'LOAD': function('void LuxurySettings::load()'),
    'UI_REFRESH': refresh, 'UI_COUNTS': counts,
    'UI_AGGREGATE': aggregate, 'UI_CLICK': click,
}
assert 'bool SettingsWriteBlocked = false;' in SOURCE
for key, value in parts.items():
    CPP = CPP.replace(f'@@{key}@@', value)
assert '@@' not in CPP

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--cxx', default=os.environ.get('CXX') or shutil.which('g++') or r'C:\Strawberry\c\bin\g++.exe')
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix='luxury-ghost-settings-') as directory:
    work = Path(directory)
    cpp = work / 'ghost_settings.cpp'
    cpp.write_text(CPP, encoding='utf-8')
    exe = work / ('ghost_settings.exe' if os.name == 'nt' else 'ghost_settings')
    includes = ['Telegram/SourceFiles', 'Telegram/lib_rpl', 'Telegram/lib_base', 'Telegram/ThirdParty/GSL/include']
    command = [args.cxx, '-std=c++20', '-O0', '-g', '-D_DEBUG', '-Wall', '-Wextra']
    for include in includes:
        command += ['-I', str(ROOT / include)]
    command += [str(cpp), '-o', str(exe)]
    print('Compile:', subprocess.list2cmdline(command), flush=True)
    subprocess.run(command, check=True)
    subprocess.run([str(exe)], check=True)
