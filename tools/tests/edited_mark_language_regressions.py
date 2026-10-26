"""The "edited" mark on message bubbles follows the interface language.

The default mark was the text of `lng_edited` taken when the settings object
was built. That happens while the settings file is read, before the language
pack is loaded, so it was always English "edited". The first change of any
LuxuryGram setting wrote that word into `luxury_settings.json` as the mark,
and from then on bubbles and the "Edited mark" row said "edited" in every
interface language. "Reset" in the mark box wrote the current translation,
which then stayed after the next language switch.

Round 95 harness, English then a pack translating `lng_edited`, restarted:
bubble and row "edited" with `lng_edited` = "изменено" (3 failures);
after the fix "изменено", and a file from an earlier build with
`"editedMark": "edited"` reads as the language default.

Runs the real getter, value producer, setter and JSON load/save fragments
over the real rpl and nlohmann::json with a QString/tr shim; not a native
acceptance.
"""
from pathlib import Path
import re
import tempfile
from library_regressions import CXX, ROOT, function, run, source


SETTINGS = 'Telegram/SourceFiles/luxury/luxury_settings.cpp'
HEADER = 'Telegram/SourceFiles/luxury/luxury_settings.h'
HELPERS = 'Telegram/SourceFiles/luxury/utils/telegram_helpers.cpp'
CHATS = 'Telegram/SourceFiles/luxury/ui/settings/settings_chats.cpp'

PROGRAM = r'''
#include "luxury/libs/json.hpp"
#include <compare>
#include <cstdio>
#include <string>
#include "rpl/variable.h"
#include "rpl/combine.h"
#include "rpl/map.h"
namespace base::assertion { void log(const char*, const char*, int) {} }
using ushort = unsigned short;
struct QChar {
    char value = 0;
    bool isHighSurrogate() const { return false; }
    bool isLowSurrogate() const { return false; }
};
struct QString {
    std::string text;
    QString() = default;
    QString(const char *s) : text(s) {}
    QString(std::string s) : text(std::move(s)) {}
    static QString fromStdString(const std::string &s) { return s; }
    std::string toStdString() const { return text; }
    bool isEmpty() const { return text.empty(); }
    int size() const { return int(text.size()); }
    QChar operator[](int i) const { return { text[i] }; }
    QString left(int n) const { return text.substr(0, n < 0 ? 0 : n); }
    auto operator<=>(const QString &) const = default;
};
inline void to_json(nlohmann::json &j, const QString &q) { j = q.text; }
inline void from_json(const nlohmann::json &j, QString &q) { q = j.get<std::string>(); }
using json = nlohmann::json;

namespace tr {
struct now_t {};
inline constexpr now_t now;
rpl::variable<QString> language = QString("edited");
struct phrase {
    ushort base;
    QString operator()(now_t) const { return language.current(); }
    rpl::producer<QString> operator()() const { return language.value(); }
};
inline constexpr phrase lng_edited{ 3706 };
} // namespace tr
namespace Lang {
QString GetOriginalValue(ushort key) { return (key == 3706) ? "edited" : ""; }
} // namespace Lang

constexpr auto kMaxMarkLength = MARK;
HELPER
struct LuxurySettings {
    FIELD
    int saves = 0;
    void save() { ++saves; }
    QString editedMark() const;
    rpl::producer<QString> editedMarkValue() const;
    void setEditedMark(const QString &val);
};
GETTER
VALUE
SETTER
LuxurySettings Load(const std::string &text) {
    const auto j = json::parse(text);
    LuxurySettings defaults;
    auto loaded = LuxurySettings();
    auto &s = loaded;
    READ
    return loaded;
}
json Save(const LuxurySettings &s) {
    return json{
        WRITE
    };
}

int failures = 0;
void Expect(bool ok, const char *what, const QString &got) {
    if (!ok) {
        std::printf("FAIL %s: '%s'\n", what, got.text.c_str());
        ++failures;
    }
}
int main() {
    const auto russian = QString("\xD0\xB8\xD0\xB7\xD0\xBC\xD0\xB5\xD0\xBD\xD0\xB5\xD0\xBD\xD0\xBE");
    auto settings = LuxurySettings();
    auto row = QString();
    auto lifetime = rpl::lifetime();
    settings.editedMarkValue() | rpl::on_next([&](QString v) {
        row = v;
    }, lifetime);
    Expect(settings.editedMark() == "edited" && row == "edited", "English default", settings.editedMark());

    const auto saved = Save(settings).dump();
    tr::language = russian;
    Expect(settings.editedMark() == russian, "bubble follows a language switch", settings.editedMark());
    Expect(row == russian, "settings row follows a language switch", row);
    const auto restarted = Load(saved);
    Expect(restarted.editedMark() == russian, "a file saved in English reads in the new language", restarted.editedMark());

    settings.setEditedMark(russian);
    Expect(Save(settings).at("customEditedMark").get<std::string>().empty(), "reset to the language word keeps following it", settings.editedMark());
    tr::language = QString("edited");
    Expect(settings.editedMark() == "edited", "after reset, the next language switch applies", settings.editedMark());

    settings.setEditedMark("upd.");
    tr::language = russian;
    Expect(settings.editedMark() == "upd." && row == "upd.", "a custom mark stays in every language", row);
    Expect(Load(Save(settings).dump()).editedMark() == "upd.", "a custom mark survives a restart", settings.editedMark());

    Expect(Load(R"({"editedMark":"edited"})").editedMark() == russian, "an earlier build's saved default reads as the language", Load(R"({"editedMark":"edited"})").editedMark());
    Expect(Load(R"({"editedMark":"red."})").editedMark() == "red.", "an earlier build's custom mark is kept", Load(R"({"editedMark":"red."})").editedMark());
    Expect(Load(R"({"editedMark":"red.","customEditedMark":""})").editedMark() == russian, "the current key wins over the earlier one", Load(R"({"editedMark":"red.","customEditedMark":""})").editedMark());
    Expect(Load("{}").editedMark() == russian, "no key reads as the language", Load("{}").editedMark());
    if (failures) {
        return 1;
    }
    std::printf("Edited mark language PASS: the default follows the interface language in bubbles, the settings row, reset and saved files\n");
}
'''


def check(root):
    settings = source(SETTINGS)
    header = source(HEADER)
    constructor = function(settings, 'LuxurySettings::LuxurySettings()')
    assert 'lng_edited' not in constructor, 'the default mark is still frozen at construction'
    field = re.search(r'\trpl::variable<QString> _editedMark[^\n]*\n', header).group()
    read_start = settings.index('\ts._editedMark = j.value(')
    read = settings[read_start:settings.index('\n\ts._unlimitedRecentStickers', read_start)]
    write = re.search(r'\t\t\{"\w*[eE]ditedMark", s\._editedMark\.current\(\)\},\n', settings).group()
    reset = function(source(CHATS), 'void BuildMarks(')
    assert 'settings->editedMark(),\n\t\t\t\t\ttr::lng_edited(tr::now),' in reset, 'the mark box no longer resets to the language word'
    path = root / 'edited_mark_language.cpp'
    path.write_text(PROGRAM
        .replace('MARK', re.search(r'kMaxMarkLength = (\d+);', settings).group(1))
        .replace('HELPER', function(source(HELPERS), 'int safeCutPosition('))
        .replace('FIELD', field)
        .replace('GETTER', function(settings, 'QString LuxurySettings::editedMark() const'))
        .replace('VALUE', function(settings, 'rpl::producer<QString> LuxurySettings::editedMarkValue() const'))
        .replace('SETTER', function(settings, 'void LuxurySettings::setEditedMark('))
        .replace('READ', read)
        .replace('WRITE', write), encoding='utf-8')
    exe = root / 'edited_mark_language.exe'
    includes = ['Telegram/SourceFiles', 'Telegram/lib_rpl', 'Telegram/lib_base', 'Telegram/ThirdParty/GSL/include']
    command = [CXX, '-std=c++20', '-O0', '-g', '-D_DEBUG']
    for include in includes:
        command += ['-I', str(ROOT / include)]
    run([*command, str(path), '-o', str(exe)], root)
    print(run([str(exe)], root).strip())


if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='luxury-edited-mark-') as directory:
        check(Path(directory))
