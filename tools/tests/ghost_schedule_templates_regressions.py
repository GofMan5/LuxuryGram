"""Compile ghost mode's "Schedule Messages" production slices with UI doubles.

Ghost scheduling turns an ordinary send into one scheduled a few seconds
ahead. It must not touch what is not an ordinary send:
- quick replies and welcome messages are stored templates, and a schedule
  date files them as scheduled messages of the editor's chat instead;
- self-destructing media cannot be scheduled, and the upload path re-applies
  ghost scheduling with the timer already in the options;
- the files box of the "Scheduled messages" section must still ask for the
  date the user wants instead of sending at now + a few seconds.
No Qt, application/profile, real network, or native integration is exercised.
"""
from pathlib import Path
import tempfile
from library_regressions import cpp, function, source


PRELUDE = r'''
#include <cassert>
#include <iostream>
#include <optional>
#include <string>
#include <vector>
template<class T> using not_null = T;
using TimeId = long long;
using int64 = long long;
struct PeerData {};
struct History {};
struct FullReplyTo { int id = 0; };
namespace Api {
struct SendOptions {
	PeerData *sendAs = nullptr;
	TimeId scheduled = 0;
	int shortcutId = 0;
	bool welcomeTemplate = false;
	long long ttlSeconds = 0;
};
struct SendAction {
	SendAction(History *history, SendOptions options)
	: history(history), options(options) {}
	History *history;
	SendOptions options;
	FullReplyTo replyTo;
};
enum class SendType { Normal, Scheduled, ScheduledToUser };
} // namespace Api
struct Ghost { bool schedule = true; bool isUseScheduledMessages() const { return schedule; } };
namespace Main { struct Session { Ghost ghost; }; }
namespace LuxurySettings { const Ghost &ghost(Main::Session *s) { return s->ghost; } }
namespace Core {
struct Proxy { bool enabled = false; bool isEnabled() const { return enabled; } };
struct Settings { Proxy p; Proxy &proxy() { return p; } };
struct Application { Settings s; Settings &settings() { return s; } };
Application app;
Application &App() { return app; }
} // namespace Core
namespace base::unixtime { TimeId now() { return 1000; } }
namespace Ui { struct PreparedFile { int ttlSeconds = 0; int64 size = 0; }; }
namespace ranges {
template<class C, class M> bool any_of(const C &c, M m) { for (const auto &x : c) if (x.*m) return true; return false; }
template<class C, class T, class F> T accumulate(const C &c, T init, F f) { for (const auto &x : c) init = f(init, x); return init; }
} // namespace ranges
namespace tr { struct Now {}; inline Now now; inline std::string lng_ttl_no_schedule(Now) { return "ttl"; } }
namespace SendMenu {
enum class SpoilerState { None };
enum class CaptionState { None };
enum class PhotoQualityState { None };
enum class ActionType { Schedule };
struct Action { ActionType type; };
struct Details {
	SpoilerState spoiler{};
	CaptionState caption{};
	PhotoQualityState photoQuality{};
	std::optional<unsigned long long> price;
};
int prompts = 0;
template<class S, class C> auto DefaultCallback(S, C) { return [](Action, Details) { ++prompts; }; }
} // namespace SendMenu
struct Show { Main::Session *s; Main::Session &session() { return *s; } };
struct List { std::vector<Ui::PreparedFile> files; };
class SendFilesBox {
public:
	void send(Api::SendOptions options, bool ctrlShiftEnter = false);
	List _list;
	Show *_show = nullptr;
	Api::SendType _sendType = Api::SendType::Normal;
	std::vector<std::string> toasts;
	std::vector<Api::SendOptions> sent;
	void showToast(std::string text) { toasts.push_back(text); }
	int getScheduleTime(int64) const { return 20; }
	SendMenu::Details _sendMenuDetails() const { return {}; }
	int sendCallback() { return 0; }
};
template<class T> struct Variable { T v; T current() const { return v; } };
struct Compose { PeerData *sendAsPeer() const { return nullptr; } };
class ShortcutMessages {
public:
	Api::SendAction prepareSendAction(Api::SendOptions options) const;
	History *_history = nullptr;
	Variable<int> _shortcutId{ 5 };
	Compose *_composeControls = nullptr;
	FullReplyTo replyTo() const { return {}; }
};
class WelcomeMessagesWidget {
public:
	Api::SendAction prepareSendAction(Api::SendOptions options) const;
	History *_history = nullptr;
};
'''

MAIN = r'''
int failures = 0;
void expect(bool ok, const char *what) {
	if (!ok) {
		std::cerr << "FAIL: " << what << "\n";
		++failures;
	}
}

int main() {
	Main::Session session;
	auto plain = Api::SendOptions();
	applyGhostScheduling(&session, plain, 12);
	expect(plain.scheduled == 1012, "ghost schedules an ordinary send");
	auto shortcut = Api::SendOptions{ .shortcutId = 5 };
	applyGhostScheduling(&session, shortcut, 12);
	expect(!shortcut.scheduled, "quick reply template stays unscheduled");
	auto welcome = Api::SendOptions{ .welcomeTemplate = true };
	applyGhostScheduling(&session, welcome, 12);
	expect(!welcome.scheduled, "welcome template stays unscheduled");
	auto viewOnce = Api::SendOptions{ .ttlSeconds = 2147483647 };
	applyGhostScheduling(&session, viewOnce, 17);
	expect(!viewOnce.scheduled, "view-once voice and timer media stay unscheduled");

	Show show{ &session };
	const auto box = [&](Api::SendType type, int ttl) {
		auto result = SendFilesBox();
		result._show = &show;
		result._sendType = type;
		result._list.files.push_back({ .ttlSeconds = ttl, .size = 7 });
		SendMenu::prompts = 0;
		return result;
	};
	for (const auto type : { Api::SendType::Scheduled, Api::SendType::ScheduledToUser }) {
		auto scheduledBox = box(type, 0);
		scheduledBox.send({});
		expect(SendMenu::prompts == 1 && scheduledBox.sent.empty(), "scheduled box asks for a date");
		scheduledBox.send({ .scheduled = 5000 });
		expect(scheduledBox.sent.size() == 1 && scheduledBox.sent[0].scheduled == 5000, "chosen date is kept");
	}
	auto normal = box(Api::SendType::Normal, 0);
	normal.send({});
	expect(!SendMenu::prompts && normal.sent.size() == 1 && normal.sent[0].scheduled == 1020, "normal box applies ghost");
	auto ttl = box(Api::SendType::Normal, 10);
	ttl.send({});
	expect(ttl.toasts.empty() && ttl.sent.size() == 1 && !ttl.sent[0].scheduled, "self-destructing media skips ghost");
	session.ghost.schedule = false;
	auto off = box(Api::SendType::Normal, 0);
	off.send({});
	expect(off.sent.size() == 1 && !off.sent[0].scheduled, "ghost off sends now");

	const auto stray = Api::SendOptions{ .scheduled = 1020 };
	const auto quick = ShortcutMessages().prepareSendAction(stray);
	expect(quick.options.shortcutId == 5 && !quick.options.scheduled, "quick reply editor drops a schedule date");
	const auto greeting = WelcomeMessagesWidget().prepareSendAction(stray);
	expect(greeting.options.welcomeTemplate && !greeting.options.scheduled, "welcome editor drops a schedule date");

	if (failures) {
		return 1;
	}
	std::cout << "ghost schedule templates PASS\n";
}
'''


def check(root):
    helpers = source('Telegram/SourceFiles/luxury/utils/telegram_helpers.cpp').replace('\r\n', '\n')
    schedule = function(helpers, 'void applyGhostScheduling(')
    box = source('Telegram/SourceFiles/boxes/send_files_box.cpp').replace('\r\n', '\n')
    begin = box.index('void SendFilesBox::send(\n')
    send = box[begin:box.index('\tif (_preparing) {', begin)] + '\tsent.push_back(options);\n}\n'
    shortcut = source('Telegram/SourceFiles/settings/business/settings_shortcut_messages.cpp').replace('\r\n', '\n')
    quick = function(shortcut, 'Api::SendAction ShortcutMessages::prepareSendAction(')
    welcome = source('Telegram/SourceFiles/history/view/history_view_welcome_messages_section.cpp').replace('\r\n', '\n')
    greeting = function(welcome, 'Api::SendAction WelcomeMessagesWidget::prepareSendAction(')
    cpp(root, 'ghost_schedule_templates', '\n'.join((PRELUDE, schedule, send, quick, greeting, MAIN)))


if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='ghost-schedule-templates-') as directory:
        check(Path(directory))
