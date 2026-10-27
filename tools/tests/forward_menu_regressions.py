"""Forward variants and Forward to Saved Messages in the message menu.

Next to Forward / Forward Selected the menu offers a "Forward Options"
submenu (picker preset to hide sender name / caption) and a "Forward to Saved
Messages" submenu that sends straight to the self chat with the same options,
no picker. Gates and labels match the draft panel's forward options:
hide-sender only with HasDropForwardedInfoSetting, hide-caption only with a
caption, singular/plural by sender and caption count (not item count).

The Saved path goes through upstream Window::ForwardToSelf, not the share
box callback, whose hideLayer() would close whatever layer the user opened
meanwhile. The send error is checked up front, and the selection is cleared
only when the forward was issued. A ContextMenuVisibility setting hides both.

Compiles the real AddForwardVariantsActions against doubles; the doubles have
no hideLayer, so a regression to the share box path does not even compile.
"""
from library_regressions import cpp, function, source

MENU = 'Telegram/SourceFiles/luxury/ui/context_menu/context_menu.cpp'
HEADER = 'Telegram/SourceFiles/luxury/ui/context_menu/context_menu.h'
LIST_MENU = 'Telegram/SourceFiles/history/view/history_view_context_menu.cpp'
INNER = 'Telegram/SourceFiles/history/history_inner_widget.cpp'
STRINGS = 'Telegram/Resources/langs/lang.strings'
SETTINGS_H = 'Telegram/SourceFiles/luxury/luxury_settings.h'
SETTINGS = 'Telegram/SourceFiles/luxury/luxury_settings.cpp'
SETTINGS_UI = 'Telegram/SourceFiles/luxury/ui/settings/settings_chats.cpp'

DOUBLES = r'''
#include <cassert>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
template <typename T> using not_null = T;
template <typename T> using Fn = std::function<T>;
using QString = std::string;
struct FullMsgId { int id = 0; int peer = 1; };
using MessageIdsList = std::vector<FullMsgId>;
struct Item { int sender = 0; bool caption = false; bool dropSetting = true; };
using HistoryItemsList = std::vector<Item*>;
enum class ContextMenuVisibility { Hidden, Visible, VisibleWithModifier };
struct LuxurySettings {
	ContextMenuVisibility forward = ContextMenuVisibility::Visible;
	static LuxurySettings &getInstance() { static LuxurySettings s; return s; }
	ContextMenuVisibility showForwardVariantsInContextMenu() const { return forward; }
};
bool needToShowItem(ContextMenuVisibility state) { return state == ContextMenuVisibility::Visible; }
namespace Data {
enum class ForwardOptions { PreserveInfo, NoSenderNames, NoNamesAndCaptions };
struct ForwardDraft { MessageIdsList ids; ForwardOptions options = ForwardOptions::PreserveInfo; };
struct Thread {};
} // namespace Data
struct History : Data::Thread {};
std::vector<Item> world;
bool canHideAuthor = true;
bool sendError = false;
namespace Data {
struct Session {
	History self;
	HistoryItemsList idsToItems(const MessageIdsList &ids) {
		auto r = HistoryItemsList();
		for (const auto &id : ids) if (id.id < int(world.size())) r.push_back(&world[id.id]);
		return r;
	}
	History *history(int peer) { assert(peer == 777); return &self; }
};
} // namespace Data
namespace Main {
struct Session {
	Data::Session owner;
	Data::Session &data() { return owner; }
	int userPeerId() const { return 777; }
};
} // namespace Main
struct Show {}; // No hideLayer on purpose.
struct Box { std::string text; };
namespace Window {
struct SessionController {
	Main::Session s;
	std::vector<std::string> boxes;
	Main::Session &session() { return s; }
	std::shared_ptr<Show> uiShow() { return std::make_shared<Show>(); }
	void show(Box box) { boxes.push_back(box.text); }
};
std::vector<std::pair<std::string, Data::ForwardOptions>> calls;
void ShowForwardMessagesBox(SessionController*, Data::ForwardDraft &&d, Fn<void()> &&) {
	calls.push_back({ "box", d.options });
}
void ForwardToSelf(std::shared_ptr<Show>, const Data::ForwardDraft &d) {
	calls.push_back({ "self", d.options });
}
} // namespace Window
struct SendingErrorRequest { const HistoryItemsList *forward = nullptr; };
struct SendError { bool error = false; };
SendError GetErrorForSending(const std::vector<Data::Thread*> &threads, SendingErrorRequest r) {
	assert(threads.size() == 1 && r.forward && !r.forward->empty());
	return { sendError };
}
Box MakeSendErrorBox(SendError, bool) { return { "error" }; }
int ItemsForwardSendersCount(const HistoryItemsList &list) {
	auto senders = std::vector<int>();
	for (const auto item : list) {
		auto found = false;
		for (const auto s : senders) found = found || (s == item->sender);
		if (!found) senders.push_back(item->sender);
	}
	return int(senders.size());
}
int ItemsForwardCaptionsCount(const HistoryItemsList &list) {
	auto n = 0;
	for (const auto item : list) n += item->caption ? 1 : 0;
	return n;
}
namespace HistoryView::Controls {
bool CanHideForwardAuthor(Main::Session*, const HistoryItemsList &) { return canHideAuthor; }
bool HasDropForwardedInfoSetting(const HistoryItemsList &list) {
	for (const auto item : list) if (item->dropSetting) return true;
	return false;
}
} // namespace HistoryView::Controls
namespace crl { template <typename O, typename F> F guard(O, F f) { return f; } }
namespace tr {
struct now_t {} now;
#define KEY(name) inline QString name(now_t) { return #name; }
KEY(lng_forward_action_hide_sender) KEY(lng_forward_action_hide_senders)
KEY(lng_forward_action_hide_caption) KEY(lng_forward_action_hide_captions)
KEY(lng_context_forward_msg) KEY(luxury_ForwardOptions) KEY(luxury_ForwardToSaved)
#undef KEY
} // namespace tr
struct Icon {};
namespace st {
Icon menuIconForward, menuIconUserHide, menuIconCaptionHide, menuIconSavedMessages;
} // namespace st
namespace Ui {
struct PopupMenu {
	struct Entry { QString text; Fn<void()> handler; std::shared_ptr<PopupMenu> sub; };
	std::vector<Entry> entries;
	void addAction(QString text, Fn<void()> handler, const Icon *) {
		entries.push_back({ text, handler, nullptr });
	}
	std::string dump() const {
		auto r = std::string();
		for (const auto &e : entries) {
			r += e.text;
			if (e.sub) r += "[" + e.sub->dump() + "]";
			r += ";";
		}
		return r;
	}
};
} // namespace Ui
namespace Window::PeerMenuCallback {
struct Args {
	QString text;
	Fn<void()> handler;
	const Icon *icon = nullptr;
	Fn<void(Ui::PopupMenu*)> fillSubmenu;
};
} // namespace Window::PeerMenuCallback
namespace Ui::Menu {
inline auto CreateAddActionCallback(PopupMenu *menu) {
	return [=](Window::PeerMenuCallback::Args &&a) {
		auto sub = std::make_shared<PopupMenu>();
		if (a.fillSubmenu) a.fillSubmenu(sub.get());
		menu->entries.push_back({ a.text, a.handler, sub });
	};
}
} // namespace Ui::Menu
'''

MAIN = r'''
using Options = Data::ForwardOptions;
std::string run(std::vector<Item> items, int *cleared = nullptr, int peer = 1) {
	world = items;
	auto ids = MessageIdsList();
	for (auto i = 0; i != int(items.size()); ++i) ids.push_back({ i, peer });
	static Window::SessionController controller;
	auto menu = Ui::PopupMenu();
	AddForwardVariantsActions(&menu, &controller, ids, [=] { if (cleared) ++*cleared; });
	return menu.dump();
}
Fn<void()> savedEntry(std::vector<Item> items, int index, int *cleared, Window::SessionController &c) {
	world = items;
	auto menu = Ui::PopupMenu();
	AddForwardVariantsActions(&menu, &c, MessageIdsList{ { 0 } }, [=] { ++*cleared; });
	const auto &saved = menu.entries.back();
	return saved.sub ? saved.sub->entries[index].handler : saved.handler;
}
int main() {
	const auto opts = std::string("luxury_ForwardOptions");
	// One message with a caption: both submenus, singular labels.
	assert(run({ { 1, true } }) ==
		"luxury_ForwardOptions[lng_forward_action_hide_sender;lng_forward_action_hide_caption;];"
		"luxury_ForwardToSaved[lng_context_forward_msg;lng_forward_action_hide_sender;lng_forward_action_hide_caption;];");
	// Album from one sender with two captions: sender stays singular.
	assert(run({ { 1, true }, { 1, true } }) ==
		"luxury_ForwardOptions[lng_forward_action_hide_sender;lng_forward_action_hide_captions;];"
		"luxury_ForwardToSaved[lng_context_forward_msg;lng_forward_action_hide_sender;lng_forward_action_hide_captions;];");
	// Two senders, one caption.
	assert(run({ { 1, false }, { 2, true } }) ==
		"luxury_ForwardOptions[lng_forward_action_hide_senders;lng_forward_action_hide_caption;];"
		"luxury_ForwardToSaved[lng_context_forward_msg;lng_forward_action_hide_senders;lng_forward_action_hide_caption;];");
	// No caption: no caption variant.
	assert(run({ { 1, false } }) ==
		"luxury_ForwardOptions[lng_forward_action_hide_sender;];"
		"luxury_ForwardToSaved[lng_context_forward_msg;lng_forward_action_hide_sender;];");
	// Names forced (no drop setting) but a caption: only the caption variant.
	assert(run({ { 1, true, false } }) ==
		"luxury_ForwardOptions[lng_forward_action_hide_caption;];"
		"luxury_ForwardToSaved[lng_context_forward_msg;lng_forward_action_hide_caption;];");
	// Nothing to hide: one plain item.
	assert(run({ { 1, false, false } }) == "luxury_ForwardToSaved;");
	canHideAuthor = false;
	assert(run({ { 1, true } }) == "luxury_ForwardToSaved;");
	canHideAuthor = true;
	// From Saved Messages itself: picker options only, never a one-click self forward.
	assert(run({ { 1, true } }, nullptr, 777) ==
		"luxury_ForwardOptions[lng_forward_action_hide_sender;lng_forward_action_hide_caption;];");
	assert(run({ { 1, false, false } }, nullptr, 777) == "");
	// Setting hides everything.
	LuxurySettings::getInstance().forward = ContextMenuVisibility::Hidden;
	assert(run({ { 1, true } }) == "");
	LuxurySettings::getInstance().forward = ContextMenuVisibility::Visible;

	// Options submenu opens the preset box and does not send.
	Window::calls.clear();
	{
		world = { { 1, true } };
		Window::SessionController c;
		auto menu = Ui::PopupMenu();
		AddForwardVariantsActions(&menu, &c, MessageIdsList{ { 0 } }, nullptr);
		menu.entries[0].sub->entries[1].handler();
		assert(Window::calls.size() == 1 && Window::calls[0].first == "box"
			&& Window::calls[0].second == Options::NoNamesAndCaptions);
	}

	// Saved: a send error shows the box, sends nothing, keeps the selection.
	auto cleared = 0;
	Window::calls.clear();
	sendError = true;
	{
		Window::SessionController c;
		savedEntry({ { 1, true } }, 1, &cleared, c)();
		assert(Window::calls.empty() && cleared == 0);
		assert(c.boxes == std::vector<std::string>{ "error" });
	}
	sendError = false;
	// Saved: success forwards to self with the option, then clears.
	{
		Window::SessionController c;
		savedEntry({ { 1, true } }, 1, &cleared, c)();
		assert(Window::calls.size() == 1 && Window::calls[0].first == "self"
			&& Window::calls[0].second == Options::NoSenderNames);
		assert(cleared == 1 && c.boxes.empty());
	}
	// Saved: items gone by click time, nothing sent, nothing cleared.
	{
		Window::SessionController c;
		auto handler = savedEntry({ { 1, true } }, 0, &cleared, c);
		world.clear();
		Window::calls.clear();
		handler();
		assert(Window::calls.empty() && cleared == 1);
	}
	std::cout << "forward variants fixture ok";
}
'''


def check(root, read=source):
    assert 'void AddForwardVariantsActions(' in read(HEADER)
    body = function(read(MENU), 'void AddForwardVariantsActions(')
    # The share box callback hides the open layer on completion.
    assert 'DefaultForwardCallback' not in body and 'hideLayer' not in body
    assert 'Window::ForwardToSelf(' in body
    cpp(root, 'forward_variants_menu', DOUBLES + body + MAIN)

    single = function(read(LIST_MENU), 'bool AddForwardMessageAction(')
    assert 'LuxuryUi::AddForwardVariantsActions(' in single
    selected = function(read(LIST_MENU), 'bool AddForwardSelectedAction(')
    assert 'LuxuryUi::AddForwardVariantsActions(' in selected
    assert 'strong->cancelSelection();' in selected

    inner = function(read(INNER), 'void HistoryInner::showContextMenu(')
    assert inner.count('LuxuryUi::AddForwardVariantsActions(') == 4
    assert inner.count('getSelectedForwardItems(),') == 2
    assert inner.count('_widget->clearSelected(); }));') >= 2
    assert 'session->data().itemOrItsGroup(item));' in inner

    header = read(SETTINGS_H)
    assert 'rpl::variable<ContextMenuVisibility> _showForwardVariantsInContextMenu = ContextMenuVisibility::Visible;' in header
    assert 'void setShowForwardVariantsInContextMenu(ContextMenuVisibility val);' in header
    settings = read(SETTINGS)
    assert 'validateEnum(_showForwardVariantsInContextMenu, defaults._showForwardVariantsInContextMenu);' in settings
    assert '{"showForwardVariantsInContextMenu", s._showForwardVariantsInContextMenu.current()},' in settings
    assert 's._showForwardVariantsInContextMenu = j.value("showForwardVariantsInContextMenu", defaults._showForwardVariantsInContextMenu.current());' in settings
    assert 'void LuxurySettings::setShowForwardVariantsInContextMenu(' in settings
    ui = read(SETTINGS_UI)
    assert 'u"luxury/showForwardVariantsInContextMenu"_q' in ui
    assert 'setShowForwardVariantsInContextMenu(static_cast<ContextMenuVisibility>(i))' in ui

    strings = read(STRINGS)
    for key in ('luxury_ForwardOptions', 'luxury_ForwardToSaved', 'luxury_ForwardVariantsMenuText'):
        assert f'"{key}" = "' in strings, key
        assert f'{{ "{key}", "' in settings, key
    print('Forward menu PASS: variants gated and labelled like the draft panel; Saved forward keeps open layers and clears the selection only when sent; menu visibility setting')


if __name__ == '__main__':
    import tempfile
    from pathlib import Path
    with tempfile.TemporaryDirectory() as d:
        check(Path(d))
