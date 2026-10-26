"""The plugin details box lists the dependencies a plugin declares.

exteraGram documents `__requirements__` as a Python list of PEP 508 strings
(`__requirements__ = ["mpmath", "tinydb"]`). The parser only read the older
comma-separated string, so a plugin written to the documentation showed no
dependency chips at all: 5 of the 16 public plugin files on GitHub that set
`__requirements__` use the list.
Each chip opened `pypi.org/project/<whole requirement>/`, so a pinned
`kangelpluginsmanager==1.4.4` led to a missing project
(`pypi.org/pypi/kangelpluginsmanager==1.4.4/json` answers 404, the bare name
200).

Round 94 harness against the real `Ui::ParsePluginMetadata`: every list form
gave 0 chips and `">=2, ok"` gave a `>=2` chip; 6 failures.

Runs the real parser functions over a std::regex Qt shim; not a native
acceptance.
"""
from pathlib import Path
import re
import tempfile
from library_regressions import cpp, function, source


PLUGIN = 'Telegram/SourceFiles/luxury/ui/boxes/plugin_info_box.cpp'
HELPERS = 'Telegram/SourceFiles/luxury/utils/telegram_helpers.cpp'

SHIM = r'''
#include <cstdio>
#include <optional>
#include <regex>
#include <string>
#include <vector>

struct QChar {
    char value = 0;
    bool isHighSurrogate() const { return false; }
    bool isLowSurrogate() const { return false; }
};
struct QString {
    std::optional<std::string> value;
    QString() = default;
    QString(const char *text) : value(text) {}
    QString(std::string text) : value(std::move(text)) {}
    static QString fromUtf8(const std::string &data) { return QString(data); }
    const std::string &str() const { static const std::string empty; return value ? *value : empty; }
    bool isNull() const { return !value; }
    bool isEmpty() const { return str().empty(); }
    int size() const { return int(str().size()); }
    QChar operator[](int i) const { return { str()[i] }; }
    QString left(int n) const { return str().substr(0, n < 0 ? 0 : n); }
    QString &replace(const QString &from, const QString &to) {
        auto text = str();
        for (auto i = text.find(from.str()); i != std::string::npos; i = text.find(from.str(), i + to.size())) {
            text.replace(i, from.size(), to.str());
        }
        value = text;
        return *this;
    }
    QString arg(const QString &a, const QString &b) const {
        auto text = str();
        for (const auto &[key, with] : { std::pair{ std::string("%1"), a.str() }, std::pair{ std::string("%2"), b.str() } }) {
            for (auto i = text.find(key); i != std::string::npos; i = text.find(key, i + with.size())) {
                text.replace(i, key.size(), with);
            }
        }
        return text;
    }
    QString trimmed() const {
        const auto &text = str();
        const auto from = text.find_first_not_of(" \t\r\n");
        return (from == std::string::npos) ? std::string() : text.substr(from, text.find_last_not_of(" \t\r\n") - from + 1);
    }
    std::vector<QString> split(char separator) const;
    QString &operator+=(const QString &other) { value = str() + other.str(); return *this; }
    bool operator==(const QString &other) const { return str() == other.str(); }
};
QString operator""_q(const char16_t *text, size_t size) {
    auto result = std::string();
    for (auto i = size_t(); i != size; ++i) {
        result.push_back(char(text[i]));
    }
    return result;
}
struct QStringList : std::vector<QString> {
    using std::vector<QString>::vector;
    QStringList(std::vector<QString> list) : std::vector<QString>(std::move(list)) {}
    bool isEmpty() const { return empty(); }
    void append(const QString &value) { push_back(value); }
    QString join(const QString &with) const {
        auto result = std::string();
        for (auto i = size_t(); i != size(); ++i) {
            result += (i ? with.str() : std::string()) + (*this)[i].str();
        }
        return result;
    }
};
std::vector<QString> QString::split(char separator) const {
    auto result = std::vector<QString>();
    auto from = size_t();
    for (auto to = str().find(separator); ; to = str().find(separator, from)) {
        result.push_back(str().substr(from, to - from));
        if (to == std::string::npos) {
            return result;
        }
        from = to + 1;
    }
}
using QByteArray = std::string;

struct QRegularExpressionMatch {
    std::vector<QString> groups;
    bool hasMatch() const { return !groups.empty(); }
    QString captured(int i = 0) const {
        return (i < int(groups.size())) ? groups[i] : QString();
    }
    static QRegularExpressionMatch From(const std::smatch &match) {
        auto result = QRegularExpressionMatch();
        for (const auto &group : match) {
            result.groups.push_back(group.matched ? QString(group.str()) : QString());
        }
        return result;
    }
};
struct QRegularExpressionMatchIterator {
    std::vector<QRegularExpressionMatch> matches;
    size_t index = 0;
    bool hasNext() const { return index < matches.size(); }
    QRegularExpressionMatch next() { return matches[index++]; }
};
struct QRegularExpression {
    std::string source;
    std::regex compiled;
    QRegularExpression(const QString &pattern) : source(pattern.str()), compiled(source) {}
    QString pattern() const { return source; }
    int captureCount() const { return int(compiled.mark_count()); }
    QRegularExpressionMatch match(const QString &text) const {
        const auto &subject = text.str();
        auto match = std::smatch();
        return std::regex_search(subject, match, compiled)
            ? QRegularExpressionMatch::From(match)
            : QRegularExpressionMatch();
    }
    QRegularExpressionMatchIterator globalMatch(const QString &text) const {
        auto result = QRegularExpressionMatchIterator();
        const auto &subject = text.str();
        for (auto i = std::sregex_iterator(subject.begin(), subject.end(), compiled); i != std::sregex_iterator(); ++i) {
            result.matches.push_back(QRegularExpressionMatch::From(*i));
        }
        return result;
    }
};

struct PluginMetadata {
    QString id;
    QString name;
    QString description;
    QString author;
    QString version;
    QString icon;
    QString minVersion;
    QStringList requirements;
};

CONSTANTS
HELPER
TRUNCATED
QUOTED_VALUE
QUOTED_PARTS
EXTRACT_FIELD
EXTRACT_LIST
REQUIREMENT_NAME
PARSE

int failures = 0;
void Expect(const char *name, const char *requirements, QStringList wanted) {
    const auto metadata = ParsePluginMetadata(
        std::string("__id__ = \"probe\"\n__name__ = \"Probe\"\n") + requirements + "\n");
    if (metadata.id.str() != "probe" || !(metadata.requirements == wanted)) {
        ++failures;
        std::printf("FAIL [%s]: chips [%s], wanted [%s]\n", name,
            metadata.requirements.join(" | ").str().c_str(),
            wanted.join(" | ").str().c_str());
    }
}

int main() {
    Expect("documented list", "__requirements__ = [\"mpmath\", \"tinydb\"]", { "mpmath", "tinydb" });
    Expect("single list", "__requirements__ = [\"yt-dlp\"]", { "yt-dlp" });
    Expect("multiline list", "__requirements__ = [\n    'mutagen',\n    \"requests\",\n]", { "mutagen", "requests" });
    Expect("pep508 list", "__requirements__ = [\"requests[socks]>=2.0,<3\", 'yt-dlp']", { "requests[socks]>=2.0,<3", "yt-dlp" });
    Expect("empty list", "__requirements__ = []", {});
    Expect("string", "__requirements__ = \"mpmath, tinydb\"", { "mpmath", "tinydb" });
    Expect("concatenated string", "__requirements__ = (\"a, \" \"b\")", { "a", "b" });
    Expect("pinned string", "__requirements__ = \"kangelpluginsmanager==1.4.4\"", { "kangelpluginsmanager==1.4.4" });
    Expect("nameless string", "__requirements__ = \">=2, ok\"", { "ok" });
    Expect("none", "", {});
    auto many = std::string();
    auto wanted = QStringList();
    for (auto i = 0; i != 70; ++i) {
        many += (i ? ", \"p" : "\"p") + std::to_string(i) + "\"";
        if (i < kMaxRequirements) {
            wanted.push_back("p" + std::to_string(i));
        }
    }
    Expect("long list", ("__requirements__ = [" + many + "]").c_str(), wanted);

    const auto links = std::vector<std::pair<const char*, const char*>>{
        { "kangelpluginsmanager==1.4.4", "kangelpluginsmanager" },
        { "requests[socks]>=2.0,<3", "requests" },
        { "zope.interface ; python_version<'3.8'", "zope.interface" },
        { "yt-dlp", "yt-dlp" },
        { "name-", "name" },
        { ">=2", "" },
    };
    for (const auto &[requirement, name] : links) {
        if (RequirementName(requirement).str() != name) {
            ++failures;
            std::printf("FAIL [link %s]: project [%s], wanted [%s]\n", requirement,
                RequirementName(requirement).str().c_str(), name);
        }
    }
    if (failures) {
        return 1;
    }
    std::printf("plugin requirements: 11 metadata forms and 6 PyPI project names\n");
}
'''


def check(root, read=source):
    plugin = read(PLUGIN).replace('\r\n', '\n')
    if 'QStringList ExtractList(' not in plugin:
        raise AssertionError('RED: plugin metadata reads only the comma-separated '
            '__requirements__ string, so the documented list form shows no '
            'dependency chips')
    if 'QString RequirementName(' not in plugin:
        raise AssertionError('RED: a requirement chip opens pypi.org/project/ with '
            'the whole requirement, so a pinned version leads to a missing project')
    box = function(plugin, 'void FillPluginInfoBox(')
    chips = box[box.index('if (!metadata.requirements.isEmpty())'):]
    assert 'RequirementName(dep)' in chips and '+ dep +' not in chips, (
        'a requirement chip must open the PyPI page of its project name')

    constants = '\n'.join(re.findall(r'constexpr auto kMaxRequirement\w* = \d+;', plugin))
    cpp(root, 'plugin_requirements', SHIM
        .replace('CONSTANTS', constants)
        .replace('HELPER', function(read(HELPERS), 'int safeCutPosition('))
        .replace('TRUNCATED', function(plugin, 'QString Truncated('))
        .replace('QUOTED_VALUE', function(plugin, 'const QRegularExpression &QuotedValue('))
        .replace('QUOTED_PARTS', function(plugin, 'QStringList QuotedParts('))
        .replace('EXTRACT_FIELD', function(plugin, 'QString ExtractField('))
        .replace('EXTRACT_LIST', function(plugin, 'QStringList ExtractList('))
        .replace('REQUIREMENT_NAME', function(plugin, 'QString RequirementName('))
        .replace('PARSE', function(plugin, 'PluginMetadata ParsePluginMetadata(')))
    print('Plugin requirements PASS: list and string forms give dependency chips; '
        'each chip opens its PyPI project page')


if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='luxury-plugin-requirements-') as directory:
        check(Path(directory))
