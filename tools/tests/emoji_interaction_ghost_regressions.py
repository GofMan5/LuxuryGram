"""Compile the production emoji-interaction send paths with RPC doubles.

Tapping an animated emoji in a private chat sends messages.setTyping with
sendMessageEmojiInteraction, and watching one sent by the peer sends
sendMessageEmojiInteractionSeen. The peer sees both in the typing slot of the
chat, so "Don't Send Typing" in ghost mode has to stop them as well. The local
animation is played from a separate queue and is not under test here.
No Qt, application/profile, real network, or native integration is exercised.
"""
from pathlib import Path
import tempfile
from library_regressions import cpp, source


def cut(text, start, end):
    begin = text.index(start)
    return text[begin:text.index(end, begin)]


PRELUDE = r'''
#include <cassert>
#include <functional>
#include <iostream>
#include <map>
#include <string>
#include <utility>
#include <vector>
using QString = std::string;
using mtpRequestId = int;
template<class T> using not_null = T;
#define Expects(x) assert(x)
namespace crl { using time = long long; inline time clock = 0; time now() { return clock; } }
namespace ranges {
template<class C, class P> auto find_if(C &c, P p) { auto i = c.begin(); while (i != c.end() && !p(*i)) ++i; return i; }
template<class I> struct Sub { I b, e; I begin() const { return b; } I end() const { return e; } };
template<class I> Sub<I> make_subrange(I b, I e) { return {b, e}; }
}
constexpr auto kAccumulateDelay = crl::time(1000);
constexpr auto kAccumulateSeenRequests = kAccumulateDelay;
constexpr auto kAcceptSeenSinceRequest = 3 * crl::time(1000);
struct MTPBool {};
struct Action { bool seen = false; };
Action MTP_sendMessageEmojiInteraction(QString, int, int) { return {}; }
Action MTP_sendMessageEmojiInteractionSeen(QString) { return {true}; }
QString MTP_string(QString value) { return value; }
int MTP_int(int value) { return value; }
int MTP_bytes(int value) { return value; }
int MTP_dataJSON(int value) { return value; }
int MTP_flags(int value) { return value; }
struct MTPint {};
struct MTPmessages_SetTyping { Action action; MTPmessages_SetTyping(int, int, MTPint, Action a) : action(a) {} };
struct Api {
	std::vector<Action> sent;
	struct Builder {
		Api *api;
		template<class F> Builder &done(F) { return *this; }
		int send() { return int(api->sent.size()); }
	};
	Builder request(MTPmessages_SetTyping r) { sent.push_back(r.action); return {this}; }
};
struct Ghost { bool typing = true; bool sendUploadProgress() const { return typing; } };
struct Session { Api transport; Ghost ghost; Api &api() { return transport; } };
namespace LuxurySettings { Ghost &ghost(Session *session) { return session->ghost; } }
struct PeerData { int input() { return 1; } };
struct History { PeerData *peer; };
struct HistoryItem { int id = 5; History *h; History *history() { return h; } };
struct EmojiInteractionsBunch { struct Single { int index = 0; double time = 0.; }; std::vector<Single> interactions; };
int ToJson(const EmojiInteractionsBunch &) { return 0; }
struct Timer { bool active = false; bool isActive() const { return active; } void callOnce(crl::time) { active = true; } };
class EmojiInteractions {
public:
	struct Animation { QString emoticon; int emoji = 1; crl::time startedAt = 0; int index = 0; };
	struct PlaySent { mtpRequestId lastRequestId = 0; crl::time lastDoneReceivedAt = 0; };
	explicit EmojiInteractions(Session *session) : _session(session) {}
	void sendAccumulatedOutgoing(crl::time now, not_null<HistoryItem*> item, std::vector<Animation> &animations);
	void playStarted(not_null<PeerData*> peer, QString emoji);
	Session *const _session;
	Timer _checkTimer;
	std::map<PeerData*, std::map<int, PlaySent>> _playsSent;
	std::map<PeerData*, std::map<QString, crl::time>> _playStarted;
};
'''

MAIN = r'''
int main() {
	PeerData peer;
	History history{&peer};
	HistoryItem item{5, &history};
	for (const auto typing : {true, false}) {
		Session session;
		session.ghost.typing = typing;
		EmojiInteractions interactions(&session);
		crl::clock = 5000;
		std::vector<EmojiInteractions::Animation> animations{
			{"x", 1, 1000, 0},
			{"x", 1, 1500, 1},
		};
		interactions.sendAccumulatedOutgoing(crl::clock, &item, animations);
		assert(animations.empty());
		interactions.playStarted(&peer, "y");
		const auto expected = typing ? 2u : 0u;
		if (session.transport.sent.size() != expected) {
			std::cerr << "typing=" << typing << " sent=" << session.transport.sent.size() << "\n";
			return 1;
		}
		assert(interactions._playsSent.empty() == !typing);
		assert(interactions._playStarted.empty() == !typing);
	}
	std::cout << "emoji interaction ghost PASS\n";
}
'''


def check(root):
    text = source('Telegram/SourceFiles/chat_helpers/emoji_interactions.cpp').replace('\r\n', '\n')
    send = cut(text, 'void EmojiInteractions::sendAccumulatedOutgoing(', 'void EmojiInteractions::clearAccumulatedIncoming(')
    seen = cut(text, 'void EmojiInteractions::playStarted(', 'EmojiInteractionsBunch EmojiInteractions::Parse(')
    cpp(root, 'emoji_interaction_ghost', PRELUDE + send + seen + MAIN)


if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='emoji-interaction-ghost-') as directory:
        check(Path(directory))
