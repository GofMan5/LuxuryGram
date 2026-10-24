"""Execute the batch-translation failure toast snippet and check its toast."""
from pathlib import Path
import re
import tempfile
from library_regressions import cpp, function, source

TRACKER = 'Telegram/SourceFiles/history/view/history_view_translate_tracker.cpp'


def snippet_body(text):
    return function(text, 'QString TranslateFailSnippet(')


def snippet_limit(text):
    return int(re.search(r'kTranslateFailSnippetLength = (\d+);', text).group(1))


def check_toast(text):
    start = text.index('tr::luxury_TranslateBatchFailed(')
    call = text[start:text.index('requestSome();', start)]
    if 'Ui::Toast::Show(' in call:
        raise AssertionError('RED: the failure toast goes to the primary window')
    assert 'window->showToast(' in call
    head = text[text.rindex('_requested.clear();', 0, start):start]
    assert 'peer->session().tryResolveWindow(peer)' in head
    duration = re.search(r'\.duration = (\w+)', call)
    assert duration, 'the failure toast keeps the 1.5 s default'
    value = re.search(
        duration.group(1) + r' = (\d+) \* crl::time\(1000\);',
        text)
    assert value and int(value.group(1)) >= 4, 'the failure toast is too short to read'


def check(root):
    text = source(TRACKER)
    check_toast(text)
    cpp(root, 'translate_fail_toast', r'''
#include <cassert>
#include <iostream>
#include <string>
#include <vector>

struct QChar {
	char16_t c = 0;
	QChar(char16_t v = 0) : c(v) {}
	bool isHighSurrogate() const { return c >= 0xD800 && c <= 0xDBFF; }
	bool isLowSurrogate() const { return c >= 0xDC00 && c <= 0xDFFF; }
};

struct QString {
	std::u16string s;
	QString() = default;
	QString(std::u16string v) : s(std::move(v)) {}
	QString(const char16_t *v) : s(v) {}
	QString(int n, QChar c) : s(size_t(n), c.c) {}
	int size() const { return int(s.size()); }
	QChar at(int i) const { return s[i]; }
	QString left(int n) const { return QString(s.substr(0, n)); }
	QString &replace(QChar from, QChar to) {
		for (auto &c : s) {
			if (c == from.c) {
				c = to.c;
			}
		}
		return *this;
	}
	QString &replace(int from, int n, const QString &with) {
		s.replace(from, n, with.s);
		return *this;
	}
	bool contains(const char16_t *what) const {
		return s.find(what) != std::u16string::npos;
	}
	friend QString operator+(QString a, const QString &b) { a.s += b.s; return a; }
	bool operator==(const QString &o) const { return s == o.s; }
};

QString operator""_q(const char16_t *v, size_t n) {
	return QString(std::u16string(v, n));
}

enum class EntityType { Bold, Spoiler };

struct EntityInText {
	EntityType t;
	int o = 0;
	int l = 0;
	EntityType type() const { return t; }
	int offset() const { return o; }
	int length() const { return l; }
};

struct TextWithEntities {
	QString text;
	std::vector<EntityInText> entities;
	operator QString() const { return text; }
};

constexpr auto kTranslateFailSnippetLength = ''' + str(snippet_limit(text)) + r''';

''' + snippet_body(text) + r'''

bool WholeSurrogates(const QString &value) {
	for (auto i = 0; i != value.size(); ++i) {
		if (value.at(i).isHighSurrogate()
			&& (i + 1 == value.size() || !value.at(i + 1).isLowSurrogate())) {
			return false;
		}
	}
	return true;
}

int main() {
	const auto shortText = TextWithEntities{ u"Hello\nworld" };
	assert(TranslateFailSnippet(shortText) == QString(u"Hello world"));

	auto secret = TextWithEntities{ u"Code: hunter2 for you" };
	secret.entities.push_back({ EntityType::Bold, 0, 4 });
	secret.entities.push_back({ EntityType::Spoiler, 6, 7 });
	const auto hidden = TranslateFailSnippet(secret);
	assert(!hidden.contains(u"hunter2"));
	assert(hidden == QString(u"Code: \u259A\u259A\u259A\u259A\u259A\u259A\u259A for you"));

	auto padding = std::u16string(kTranslateFailSnippetLength - 1, u'a');
	const auto emoji = TextWithEntities{
		QString(padding + u"\U0001F680 and the rest of a long message"),
	};
	const auto cut = TranslateFailSnippet(emoji);
	assert(WholeSurrogates(cut));
	assert(cut.s.back() == u'\u2026');

	std::cout << "LuxuryGram translate fail toast PASS: spoilers stay hidden, emoji are not split, the toast stays long enough to read\n";
}
''')


if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='translate-fail-toast-') as directory:
        check(Path(directory))
