"""Execute the shown-message refresh that LuxuryGram chat toggles trigger."""
from pathlib import Path
import tempfile
from library_regressions import cpp, function, source


TOGGLES = (
    'showMessageSeconds',
    'replaceBottomInfoWithIcons',
    'deletedMark',
    'editedMark',
    'hideFastShare',
    'semiTransparentDeletedMessages',
)


def refresh_block():
    text = source('Telegram/SourceFiles/data/data_session.cpp')
    start = text.index('\tconst auto &luxury = LuxurySettings::getInstance();')
    end = text.index('\n\t_chatsList.unreadStateChanges(', start)
    block = text[start:end]
    for name in TOGGLES:
        assert f'luxury.{name}Changes()' in block, name
    return block


def check_setters():
    text = source('Telegram/SourceFiles/luxury/luxury_settings.cpp')
    body = function(text, 'void LuxurySettings::setHidePremiumStatuses(bool val)')
    assert body.index('repaintApp();') < body.index('save();'), body
    emoji = source('Telegram/SourceFiles/chat_helpers/emoji_list_widget.cpp')
    start = emoji.index('LuxurySettings::getInstance().showOnlyAddedEmojisAndStickersChanges(')
    tail = emoji[start:emoji.index('}, lifetime());', start)]
    assert 'refreshCustom();' in tail and 'resizeToWidth(width());' in tail, tail


def check(root):
    check_setters()
    producers = ''.join(
        f'producer {name}Changes()const{{return {{&subs[{index}]}};}}'
        for index, name in enumerate(TOGGLES))
    cpp(root, 'luxury_live_toggles', r'''
#include <cassert>
#include <functional>
#include <iostream>
#include <set>
#include <unordered_map>
#include <vector>
template<class T> using not_null = T;
namespace rpl {
struct lifetime {};
using Subs = std::vector<std::function<void()>>;
struct producer { Subs *subs; };
struct to_empty_t {}; inline constexpr to_empty_t to_empty{};
inline producer operator|(producer p, to_empty_t) { return p; }
struct merged { std::vector<producer> list; };
template<class ...P> merged merge(P ...p) { return { { p... } }; }
struct on_next_t { std::function<void()> f; };
template<class F> on_next_t on_next(F f, lifetime &) { return { f }; }
inline void operator|(merged m, on_next_t n) { for (auto p : m.list) p.subs->push_back(n.f); }
} // namespace rpl
using producer = rpl::producer;
struct LuxurySettings {
	mutable rpl::Subs subs[''' + str(len(TOGGLES)) + r'''];
	''' + producers + r'''
	static LuxurySettings &getInstance() { static LuxurySettings s; return s; }
	void fire(int i) { for (auto &f : subs[i]) f(); }
};
struct HistoryItem { int id = 0; };
struct ViewElement { HistoryItem *item; HistoryItem *data() const { return item; } };
struct Session {
	std::unordered_map<not_null<const HistoryItem*>, std::vector<not_null<ViewElement*>>> _views;
	rpl::lifetime _lifetime;
	std::vector<int> notified, resized;
	std::function<void(HistoryItem*)> onNotify;
	void notifyItemDataChange(not_null<HistoryItem*> item) {
		notified.push_back(item->id);
		if (onNotify) onNotify(item);
	}
	void requestItemResize(not_null<const HistoryItem*> item) { resized.push_back(item->id); }
	void setup() {
''' + refresh_block() + r'''
	}
};
int main() {
	HistoryItem a{ 1 }, b{ 2 }, c{ 3 };
	ViewElement va{ &a }, vb1{ &b }, vb2{ &b }, vc{ &c };
	Session s;
	s._views[&a] = { &va };
	s._views[&b] = { &vb1, &vb2 };
	s._views[&c] = { &vc };
	s.setup();
	auto &settings = LuxurySettings::getInstance();
	for (auto i = 0; i != ''' + str(len(TOGGLES)) + r'''; ++i) {
		s.notified.clear();
		s.resized.clear();
		settings.fire(i);
		assert(s.notified.size() == 3 && s.resized.size() == 3);
		assert((std::set<int>(s.notified.begin(), s.notified.end()) == std::set<int>{ 1, 2, 3 }));
		assert(s.notified == s.resized);
	}
	// A refresh that destroys the other shown messages must not touch them.
	s.notified.clear();
	s.resized.clear();
	s.onNotify = [&](HistoryItem *item) {
		for (auto i = s._views.begin(); i != s._views.end();) {
			i = (i->first == item) ? std::next(i) : s._views.erase(i);
		}
		s.onNotify = nullptr;
	};
	settings.fire(0);
	assert(s.notified.size() == 1 && s.resized == s.notified);
	std::cout << "LuxuryGram live chat toggles PASS: 6 toggles refresh every shown message once, destroyed views skipped; premium statuses repaint, emoji panel rebuilds\n";
}
''')


if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='luxury-live-toggles-') as directory:
        check(Path(directory))
