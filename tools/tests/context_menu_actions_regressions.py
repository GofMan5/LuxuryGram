"""Context menu actions do what they say.

A filter made from selected text matches that text literally:
The "Add filter" context action stored the selection as an ICU pattern
verbatim, so "why?" or "(lol" became a different pattern or failed to
compile and was silently dropped by the filters cache.
Compiles the real LiteralPattern on QString doubles.

Repeat of a rich page with a reply falls back to a plain forward when the
rich copy cannot be rebuilt, instead of silently sending nothing.
"""
from library_regressions import cpp, function, source

MENU = 'Telegram/SourceFiles/luxury/ui/context_menu/context_menu.cpp'


def check(root, read=source):
    text = read(MENU)
    assert 'filter.text = LiteralPattern(selectedText).toStdString();' in text
    literal = function(text, 'QString LiteralPattern(')
    cpp(root, 'quick_filter_literal', r'''
#include <cassert>
#include <cwctype>
#include <iostream>
#include <string>
struct QChar {
	char32_t c;
	char32_t unicode() const { return c; }
	bool isLetterOrNumber() const { return std::iswalnum(c) || c > 127; }
	bool isSpace() const { return c == ' ' || c == '\t' || c == '\n'; }
	bool operator!=(char o) const { return c != char32_t(o); }
};
struct QString : std::u32string {
	using std::u32string::u32string;
	QString() = default;
	void append(QChar ch) { push_back(ch.c); }
	void append(char ch) { push_back(char32_t(ch)); }
	struct It { const char32_t *p; QChar operator*() const { return { *p }; } It &operator++() { ++p; return *this; } bool operator!=(It o) const { return p != o.p; } };
	It begin() const { return { data() }; }
	It end() const { return { data() + size() }; }
};
''' + literal + r'''
int main() {
	assert(LiteralPattern(U"why?") == U"why\\?");
	assert(LiteralPattern(U"(lol) a.b*c") == U"\\(lol\\) a\\.b\\*c");
	assert(LiteralPattern(U"[x]{2}^$|\\+") == U"\\[x\\]\\{2\\}\\^\\$\\|\\\\\\+");
	assert(LiteralPattern(U"привет_мир 42") == U"привет_мир 42");
	assert(LiteralPattern(U"") == U"");
	std::cout << "ok";
}
''')
    repeat = function(text, 'void AddRepeatMessageAction(')
    assert 'if (!LuxuryForward::forwardRichMessage(weakSession, itemId, action)) {' in repeat
    assert 'crl::on_main(weakSession, forwardPlain);' in repeat
    assert 'if (!resolvedDraft.items.empty()) {' in repeat
    print('Context menu actions PASS: quick filters match the selection literally; a failed rich repeat falls back to a forward')


if __name__ == '__main__':
    import tempfile
    from pathlib import Path
    with tempfile.TemporaryDirectory() as d:
        check(Path(d))
