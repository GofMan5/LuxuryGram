"""Per-chat filter rows and titles resolve the chat a signed dialog id names.

Compiles the real getBareDialogId/getPeerFromDialogId with g++ against a
two-account session double where one user and one channel share the bare id 500,
then checks the per-chat filter callers pass the signed id. Source-extracted
fixture; not a native settings render.
"""
from pathlib import Path
import tempfile

from library_regressions import cpp, function, source

HELPERS = 'Telegram/SourceFiles/luxury/utils/telegram_helpers.cpp'
ROWS = 'Telegram/SourceFiles/luxury/ui/settings/filters/per_dialog_filter.cpp'
TITLE = 'Telegram/SourceFiles/luxury/ui/settings/filters/settings_filters_list.cpp'


def lookup(text):
    template = 'template<typename T>\nPeerData *getPeerFromDialogId('
    start = text.find(template)
    if start < 0:
        start = text.index('PeerData *getPeerFromDialogId(')
    return text[start:text.index('QString filterZalgo(')]


def fixture(text):
    return r'''#include <cassert>
#include <cstdio>
#include <map>
#include <memory>
#include <utility>
#include <vector>
using ID = long long;
using uint64 = unsigned long long;
struct PeerId { static constexpr uint64 kChatTypeMask = 0xFFFFFFFFFFFFULL; };
struct PeerData { const char *kind; uint64 bare; };
struct UserData : PeerData {};
struct ChatData : PeerData {};
struct ChannelData : PeerData {};
struct UserId { uint64 bare; UserId(uint64 v) : bare(v) {} };
struct ChatId { uint64 bare; ChatId(uint64 v) : bare(v) {} };
struct ChannelId { uint64 bare; ChannelId(uint64 v) : bare(v) {} };
namespace Data {
struct Session {
	std::map<uint64, UserData*> users;
	std::map<uint64, ChatData*> chats;
	std::map<uint64, ChannelData*> channels;
	template <typename Map, typename Key>
	static auto find(const Map &map, Key key) {
		const auto i = map.find(key.bare);
		return (i != map.end()) ? i->second : nullptr;
	}
	UserData *userLoaded(UserId id) const { return find(users, id); }
	ChatData *chatLoaded(ChatId id) const { return find(chats, id); }
	ChannelData *channelLoaded(ChannelId id) const { return find(channels, id); }
};
} // namespace Data
namespace Main {
struct Session { Data::Session d; Data::Session &data() { return d; } };
struct Account { Session *s = nullptr; Session *maybeSession() const { return s; } };
} // namespace Main
struct Domain {
	std::vector<std::pair<int, std::unique_ptr<Main::Account>>> list;
	const auto &accounts() const { return list; }
};
struct Application { Domain d; Domain &domain() { return d; } };
Application app;
namespace Core { Application &App() { return app; } }
''' + function(text, 'ID getBareDialogId(ID dialogId)') + '\n' + lookup(text) + r'''
int main() {
	UserData user{ { "user", 500 } };
	ChannelData channel{ { "channel", 500 } };
	ChatData chat{ { "chat", 777 } };
	Main::Session empty;
	Main::Session session;
	session.d.users[500] = &user;
	session.d.channels[500] = &channel;
	session.d.chats[777] = &chat;
	app.d.list.emplace_back(0, std::make_unique<Main::Account>());
	app.d.list.emplace_back(1, std::make_unique<Main::Account>(Main::Account{ &empty }));
	app.d.list.emplace_back(2, std::make_unique<Main::Account>(Main::Account{ &session }));
	assert(getPeerFromDialogId(ID(500)) == &user);
	assert(getPeerFromDialogId(ID(-500)) == &channel);
	assert(getPeerFromDialogId(ID(-777)) == &chat);
	assert(getPeerFromDialogId(ID(777)) == nullptr);
	assert(getPeerFromDialogId(ID(-900)) == nullptr);
	assert(getPeerFromDialogId(ID(0)) == nullptr);
	std::puts("dialog peer lookup PASS: sign picks user vs channel/chat across accounts; g++ fixture of real helpers, not a native settings render");
}
'''


def check(helpers=None, rows=None, title=None):
    helpers = helpers if helpers is not None else source(HELPERS)
    rows = rows if rows is not None else source(ROWS)
    title = title if title is not None else source(TITLE)
    with tempfile.TemporaryDirectory() as root:
        cpp(Path(root), 'dialog_peer', fixture(helpers))
    for name in ('QString PerDialogFiltersListRow::generateName()', 'PaintRoundImageCallback PerDialogFiltersListRow::generatePaintUserpicCallback('):
        body = function(rows, name)
        assert 'getPeerFromDialogId(_dialogId)' in body, name
    header = function(title, 'rpl::producer<QString> LuxuryFiltersList::title()')
    assert 'getPeerFromDialogId(*dialogId)' in header
    print('per-chat filter callers pass the signed dialog id PASS')


if __name__ == '__main__':
    check()
