"""Times with seconds follow the system 12/24-hour clock.

Message Details, the exact last-seen status and the watcher timeline printed
a fixed "HH:mm:ss", so a 12-hour system showed 21:05:09 next to message
bubbles reading 9:05 PM. Compiles the real seconds format against a QString
stub and checks every caller goes through it.
"""
from pathlib import Path
import tempfile
from library_regressions import cpp, function, source

HELPERS = 'Telegram/SourceFiles/luxury/utils/telegram_helpers.cpp'
CALLERS = {
    'message details': ('Telegram/SourceFiles/luxury/utils/telegram_helpers.cpp', 'QString formatDateTime('),
    'exact last seen': ('Telegram/SourceFiles/data/data_peer_values.cpp', 'QString ExactLastSeenText('),
    'watcher timeline': ('Telegram/SourceFiles/luxury/ui/watcher/watcher_box.cpp', 'TimestampLabel CompactTimestamp('),
}


def check_sources():
    for name, (path, signature) in CALLERS.items():
        body = function(source(path), signature)
        if 'HH:mm:ss' in body:
            raise AssertionError('RED: ' + name + ' ignores the 12-hour clock')
        assert 'formatTimeWithSeconds(' in body, name
    text = source(HELPERS)
    assert 'secondsTimeFormat()' in function(text, 'QString formatTimeWithSeconds(')
    bubble = function(text, 'QString formatMessageTime(')
    assert 'secondsTimeFormat()' in bubble and 'shortTimeFormat()' in bubble


def check(root):
    check_sources()
    text = source(HELPERS)
    body = function(text, 'const QString &secondsTimeFormat(')
    body = body.replace('const QString &secondsTimeFormat()', 'QString SecondsFormat(const QString &shortFormat)')
    body = body.replace('const auto &shortFormat = shortTimeFormat();', '')
    body = body.replace('static const auto result = [] {', 'const auto result = [&] {')
    cpp(root, 'clock_format', r'''
#include <cassert>
#include <cwctype>
#include <iostream>
#include <string>

struct QChar {
	char16_t c = 0;
};

namespace Qt { enum CaseSensitivity { CaseInsensitive }; }

struct QString {
	std::u16string s;
	QString() = default;
	QString(std::u16string v) : s(std::move(v)) {}
	QString(const char16_t *v) : s(v) {}
	int indexOf(const QString &what) const {
		const auto i = s.find(what.s);
		return (i == std::u16string::npos) ? -1 : int(i);
	}
	bool contains(const QString &what, Qt::CaseSensitivity) const {
		for (auto i = size_t(0); i + what.s.size() <= s.size(); ++i) {
			auto same = true;
			for (auto j = size_t(0); j != what.s.size(); ++j) {
				if (std::towlower(s[i + j]) != std::towlower(what.s[j])) {
					same = false;
					break;
				}
			}
			if (same) {
				return true;
			}
		}
		return false;
	}
	QChar at(int i) const { return { s[i] }; }
	QString arg(QChar c) const {
		auto r = s;
		r.replace(r.find(u"%1"), 2, std::u16string(1, c.c));
		return r;
	}
	QString &insert(int at, const QString &what) {
		s.insert(size_t(at), what.s);
		return *this;
	}
	bool operator==(const QString &o) const { return s == o.s; }
};

QString operator""_q(const char16_t *v, size_t n) {
	return QString(std::u16string(v, n));
}

''' + body + r'''

int main() {
	assert(SecondsFormat(u"h:mm AP") == QString(u"h:mm:ss AP"));
	assert(SecondsFormat(u"HH:mm") == QString(u"HH:mm:ss"));
	assert(SecondsFormat(u"H.mm") == QString(u"H.mm.ss"));
	assert(SecondsFormat(u"mm") == QString(u"HH:mm:ss"));
	assert(SecondsFormat(u"ap h:mm") == QString(u"ap h:mm:ss"));
	std::cout << "LuxuryGram clock format PASS: details, exact last seen and watcher use the system clock with seconds\n";
}
''')


if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='clock-format-') as directory:
        check(Path(directory))
