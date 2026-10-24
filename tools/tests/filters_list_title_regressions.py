"""Execute the per-chat filters list title with long and emoji chat names."""
from pathlib import Path
import tempfile
from library_regressions import cpp, function, source


def title_body():
    text = source('Telegram/SourceFiles/luxury/ui/settings/filters/settings_filters_list.cpp')
    return function(text, 'rpl::producer<QString> LuxuryFiltersList::title()')


def check(root):
    cpp(root, 'filters_list_title', r'''
#include <cassert>
#include <iostream>
#include <optional>
#include <string>

struct QString {
	std::u16string s;
	QString() = default;
	QString(std::u16string v) : s(std::move(v)) {}
	QString(const char16_t *v) : s(v) {}
	QString(const char *v) {
		for (auto p = reinterpret_cast<const unsigned char*>(v); *p;) {
			auto c = char32_t(*p++);
			if (c >= 0xE0) {
				c = ((c & 0x0F) << 12) | ((p[0] & 0x3F) << 6) | (p[1] & 0x3F);
				p += 2;
			} else if (c >= 0xC0) {
				c = ((c & 0x1F) << 6) | (p[0] & 0x3F);
				p += 1;
			}
			s.push_back(char16_t(c));
		}
	}
	int length() const { return int(s.size()); }
	QString left(int n) const { return QString(s.substr(0, n)); }
	static QString number(long long v) {
		const auto a = std::to_string(v);
		return QString(std::u16string(a.begin(), a.end()));
	}
	friend QString operator+(QString a, const QString &b) { a.s += b.s; return a; }
	friend QString operator+(QString a, const char *b) { return a + QString(b); }
	friend QString operator+(const char *a, const QString &b) { return QString(a) + b; }
	bool operator==(const QString &o) const { return s == o.s; }
};

namespace rpl {
template <typename T> struct producer { T value; };
template <typename T> producer<T> single(T v) { return { std::move(v) }; }
} // namespace rpl

namespace tr {
struct now_t {}; inline constexpr now_t now{};
inline rpl::producer<QString> luxury_FiltersShadowBan() { return { u"Shadow ban" }; }
inline rpl::producer<QString> luxury_RegexFiltersShared() { return { u"Shared filters" }; }
inline QString luxury_RegexFiltersHeader(now_t) { return u"Filters"; }
} // namespace tr

struct Peer {
	QString name;
	QString topBarNameText() const { return name; }
};
Peer *Current = nullptr;
long long getBareDialogId(long long id) { return id < 0 ? -id : id; }
Peer *getPeerFromDialogId(long long) { return Current; }

struct LuxuryFiltersList {
	bool shadowBan = false;
	std::optional<long long> dialogId;
	rpl::producer<QString> title();
};

''' + title_body() + r'''

QString TitleFor(const char16_t *name) {
	auto peer = Peer{ name };
	Current = &peer;
	auto list = LuxuryFiltersList{ .dialogId = 42 };
	return list.title().value;
}

int main() {
	const auto longName = u"Рабочий чат по проекту LuxuryGram Desktop";
	assert(TitleFor(longName) == QString(longName));

	// U+1F680 sits on UTF-16 units 16..17, right where a 17-unit cut lands.
	const auto emojiName = u"Команда релизов \U0001F680 и тестеров";
	const auto title = TitleFor(emojiName);
	assert(title == QString(emojiName));
	for (auto i = 0; i != title.length(); ++i) {
		const auto c = title.s[i];
		if (c >= 0xD800 && c <= 0xDBFF) {
			assert(i + 1 < title.length());
			assert(title.s[i + 1] >= 0xDC00 && title.s[i + 1] <= 0xDFFF);
		}
	}

	assert(TitleFor(u"Короткое") == QString(u"Короткое"));

	Current = nullptr;
	auto unknown = LuxuryFiltersList{ .dialogId = -1001234 };
	assert(unknown.title().value == QString(u"Filters (1001234)"));

	std::cout << "LuxuryGram filters list title PASS: long and emoji chat names reach the top bar whole, the label elides by width\n";
}
''')


if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='filters-list-title-') as directory:
        check(Path(directory))
