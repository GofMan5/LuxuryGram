"""Compile the production channel direct-messages read request with RPC doubles.

Reading a sublist of a channel's direct messages (monoforum) sends
messages.readSavedHistory, and the other side sees its messages as read.
Ghost mode's "Don't Read Messages" has to keep that request local, the same
way chats and forum topics do.
No Qt, application/profile, real network, or native integration is exercised.
"""
from pathlib import Path
import tempfile
from library_regressions import cpp, source


PRELUDE = r'''
#include <cassert>
#include <iostream>
#include <utility>
#include <vector>
using mtpRequestId = int;
struct MsgId { long long bare = 0; };
namespace base { template<class T> T take(T &value) { return std::exchange(value, T{}); } }
namespace crl { template<class O, class F> F guard(O *, F f) { return f; } }
struct Input {};
int MTP_int(long long value) { return int(value); }
struct MTPmessages_ReadSavedHistory { int till; MTPmessages_ReadSavedHistory(Input, Input, int t) : till(t) {} };
struct Api {
	std::vector<int> sent;
	int cancelled = 0;
	struct Builder {
		Api *api;
		int till = 0;
		template<class F> Builder &done(F) { return *this; }
		int send() { api->sent.push_back(till); return int(api->sent.size()); }
		void cancel() { ++api->cancelled; }
	};
	Builder request(MTPmessages_ReadSavedHistory r) { return {this, r.till}; }
	Builder request(int) { return {this}; }
};
struct Ghost { bool read = true; bool sendReadMessages() const { return read; } };
struct Session { Api transport; Ghost ghost; Api &api() { return transport; } };
namespace LuxurySettings { Ghost &ghost(Session *session) { return session->ghost; } }
struct Peer { Input input() { return {}; } };
struct Parent {
	Session *s; Peer *chat;
	Peer *parentChat() { return chat; }
	Session &session() { return *s; }
};
struct Timer { bool active = false; bool isActive() const { return active; } void cancel() { active = false; } };
namespace Data {
class SavedSublist {
public:
	void sendReadTillRequest();
	MsgId computeInboxReadTillFull() const { return _inboxReadTillId; }
	Peer *sublistPeer() { return &_peer; }
	void reloadUnreadCountIfNeeded() {}
	Parent *_parent = nullptr;
	Peer _peer;
	Timer _readRequestTimer;
	mtpRequestId _readRequestId = 0;
	MsgId _sentReadTill;
	MsgId _inboxReadTillId;
};
'''

MAIN = r'''
} // namespace Data

int main() {
	for (const auto read : {true, false}) {
		Session session;
		session.ghost.read = read;
		Peer chat;
		Parent parent{&session, &chat};
		Data::SavedSublist sublist;
		sublist._parent = &parent;
		sublist._inboxReadTillId = {42};
		sublist._readRequestTimer.active = true;
		sublist.sendReadTillRequest();
		assert(!sublist._readRequestTimer.isActive());
		const auto expected = read ? 1u : 0u;
		if (session.transport.sent.size() != expected) {
			std::cerr << "read=" << read << " sent=" << session.transport.sent.size() << "\n";
			return 1;
		}
		if (read) {
			assert(session.transport.sent.front() == 42);
			assert(sublist._sentReadTill.bare == 42);
		} else {
			assert(sublist._readRequestId == 0);
			assert(sublist._sentReadTill.bare == 0);
		}
	}
	std::cout << "saved sublist ghost PASS\n";
}
'''


def cut(text, start, end):
    begin = text.index(start)
    return text[begin:text.index(end, begin)]


def check(root):
    text = source('Telegram/SourceFiles/data/data_saved_sublist.cpp').replace('\r\n', '\n')
    send = cut(text, 'void SavedSublist::sendReadTillRequest(', 'void SavedSublist::reloadUnreadCountIfNeeded(')
    cpp(root, 'saved_sublist_ghost', PRELUDE + send + MAIN)


if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='saved-sublist-ghost-') as directory:
        check(Path(directory))
