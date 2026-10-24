"""Compile production ghost read queue/intent slices with explicit history/RPC doubles.
No Qt, application/profile, real network, or native integration is exercised.
"""
from pathlib import Path
import os, shutil, subprocess, tempfile
root = Path(__file__).resolve().parents[2]
def read(p): return (root / 'Telegram/SourceFiles' / p).read_text('utf-8')
hist = read('data/data_histories.cpp')
replies = read('data/data_replies_list.cpp')
helpers = read('luxury/utils/telegram_helpers.cpp')
read_inbox = hist[hist.index('void Histories::readInbox('):hist.index('void Histories::readInboxTill(not_null<HistoryItem*>')]
read_till = hist[hist.index('void Histories::readInboxTill(\n'):hist.index('void Histories::readInboxOnNewMessage(')]
queue = hist[hist.index('void Histories::sendReadRequests()'):hist.index('void Histories::sendReadRequest(')]
reply_read = replies[replies.index('void RepliesList::readTill(\n'):replies.index('void RepliesList::reloadUnreadCountIfNeeded()')]
read_rpc = helpers[helpers.index('void readHistory('):helpers.index('void markReadAfterAction(')]
assert '_states.clear()' not in queue
assert 'std::optional<bool> sendReadReceipt' in read_inbox and 'false, sendReadReceipt' in read_inbox
commands = read('window/window_main_menu.cpp')
commands = commands[commands.index('if (settings.showLReadToggleInDrawer())'):commands.index('if (settings.showLReadToggleInDrawer())') + 2600]
assert 'MarkAsReadChatList(chats, false)' in commands and 'MarkAsReadChatList(chats, true)' in commands
assert 'ghost.setSendReadMessages(' not in commands
assert 'topic->readTillEnd(sendReadReceipt)' in helpers
assert 'else history->clearUnreadMentionsFor(rootId)' in helpers and 'else history->clearUnreadReactionsFor(rootId, thread->asSublist())' in helpers
cpp = r'''
#include <algorithm>
#include <cassert>
#include <functional>
#include <iostream>
#include <map>
#include <optional>
#include <utility>
#include <vector>
using MsgId = int; using mtpRequestId = int;
template<class T> using not_null = T;
template<class T> using Fn = std::function<T>;
#define DEBUG_LOG(x) do {} while(0)
#define LOG(x) do {} while(0)
#define Expects(x) assert(x)
namespace crl { using time = long long; inline time clock = 0; time now() { return clock; } template<class T,class F> F guard(T*,F f) { return f; } }
namespace base { template<class T> T take(T &v) { return std::exchange(v,T{}); } struct Timer { bool active = false; int delay = 0; bool isActive() const { return active; } void callOnce(int t) { active = true; delay = t; } void cancel() { active = false; } int remainingTime() const { return delay; } }; }
namespace gsl { template<class F> struct Finally { F f; ~Finally() { f(); } }; template<class F> Finally<F> finally(F f) { return {f}; } }
bool IsServerMsgId(MsgId id) { return id > 0; }
constexpr int kReadRequestTimeout = 3000;
struct Ghost { bool send = true; bool sendReadMessages() const { return send; } } ghost;
namespace LuxurySettings { template<class T> Ghost &ghost(T*) { return ::ghost; } }
struct MTPmessages_AffectedMessages {};
struct Channel { int inputChannel() { return 1; } } channel;
struct Peer { int id = 1; bool isChannel = false; Channel *asChannel() { return isChannel ? &channel : nullptr; } int input() { return 1; } void *forumTopicFor(int) { return nullptr; } } peer;
struct MTPchannels_ReadHistory { MTPchannels_ReadHistory(int,int) {} };
struct MTPmessages_ReadHistory { MTPmessages_ReadHistory(int,int) {} };
struct MTPmessages_ReadDiscussion { int till; MTPmessages_ReadDiscussion(int,int,int value) : till(value) {} };
int MTP_int(int value) { return value; }
struct FullMsgId { FullMsgId(int,int) {} };
struct Session;
struct HistoryItem;
namespace Data { class Histories; }
struct DataOwner { Session *s = nullptr; Data::Histories *h = nullptr; Session &session(); Data::Histories &histories() { return *h; } template<class T> HistoryItem *message(T) { return nullptr; } };
struct Api {
 std::vector<int> discussion; Fn<void()> success, failure; int finishes = 0, applied = 0;
 struct Builder {
  Api *a;
  template<class F> Builder &done(F f) { a->success = [f] { if constexpr(std::is_invocable_v<F,MTPmessages_AffectedMessages>) f(MTPmessages_AffectedMessages{}); else f(); }; return *this; }
  template<class F> Builder &fail(F f) { a->failure = f; return *this; }
  int send() { return 7; } void cancel() {}
 };
 Builder request(MTPmessages_ReadDiscussion r) { discussion.push_back(r.till); return {this}; }
 template<class T> Builder request(T) { return {this}; }
 void applyAffectedMessages(Peer*,const MTPmessages_AffectedMessages&) { ++applied; }
};
struct Session { Api transport; DataOwner owner{this}; Api &api() { return transport; } DataOwner &data() { return owner; } } session;
Session &DataOwner::session() { return *s; }
struct History;
struct HistoryItem { MsgId id = 0; History *h = nullptr; bool outgoing = false; bool out() const { return outgoing; } History *history() { return h; } void setCommentsInboxReadTill(int) {} HistoryItem *lookupDiscussionPostOriginal() { return nullptr; } };
struct History {
 Peer *peer = &::peer; bool known = true, bottom = false, top = false, marked = true; int local = 0, unread = 10, networkMarks = 0, clientReads = 0; HistoryItem last{10,this};
 Session &session() { return ::session; } DataOwner &owner() { return ::session.owner; }
 bool lastServerMessageKnown() { return known; } HistoryItem *lastServerMessage() { return &last; } bool loadedAtBottom() { return bottom; } bool loadedAtTop() { return top; } int maxMsgId() { return last.id; }
 int unreadCount() { return unread; } void setUnreadCount(int count) { unread = count; } void updateChatListEntry() {} bool unreadCountKnown() { return true; }
 bool readInboxTillNeedsRequest(int till) { ++networkMarks; marked = false; return local < till; } bool trackUnreadMessages() { return true; }
 std::optional<int> countStillUnreadLocal(int till) { return std::max(0, unread - std::max(0, till - local)); } void setInboxReadTill(int value) { local = value; } std::vector<HistoryItem*> clientSideMessages() { return {}; } void setUnreadMark(bool value) { marked = value; }
 struct Badge { bool has() const { return false; } }; Badge unreadMentions() { return {}; } Badge unreadReactions() { return {}; } History *asThread() { return this; }
};
namespace Core { struct Notifications { template<class T> void clearIncomingFromHistory(T) {} template<class T> void clearIncomingFromTopic(T) {} }; struct AppType { Notifications n; Notifications &notifications() { return n; } } app; AppType &App() { return app; } }
namespace LuxuryWorker { void markAsOnline(Session*) {} }
void readMentions(History*) {} void readReactions(History*) {}
namespace Data {
class Histories {
public:
 struct State { std::map<int,int> sent{{1,77}}, postponed{{2,88}}; int willReadTill = 0, sentReadTill = 0; crl::time willReadWhen = 0; bool sentReadDone = false; int explicitReadTill = 0; bool postponedRequestEntry = true; };
 std::map<History*,State> _states; DataOwner *_owner = &::session.owner; base::Timer _readRequestsTimer; std::vector<int> reads; Fn<void()> entry;
 enum class RequestType { ReadInbox };
 Session &session() { return ::session; }
 State *lookup(History *h) { auto i = _states.find(h); return i == _states.end() ? nullptr : &i->second; }
 void readClientSideMessage(HistoryItem*) {}
 void readInbox(History*,std::optional<bool> = std::nullopt);
 void readInboxTill(History*,int,bool,std::optional<bool> = std::nullopt);
 void sendReadRequests(); void sendReadRequest(History*,State &s) { reads.push_back(base::take(s.willReadTill)); s.willReadWhen = 0; s.explicitReadTill = 0; }
 void sendPendingReadInbox(History *h) { auto s = lookup(h); if(s && s->willReadTill) { s->willReadWhen = 0; sendReadRequests(); } }
 void requestDialogEntry(History*,Fn<void()> f) { entry = f; }
 template<class F> void sendRequest(History*,RequestType,F generate) { generate([] { ++::session.transport.finishes; }); }
};
READ_INBOX_HERE
READ_TILL_HERE
QUEUE_HERE
class RepliesList {
public:
 History *_history; int _rootId = 1, local = 0, _readRequestId = 0, _readRequestTill = 0, _explicitReadRequestTill = 0; base::Timer _readRequestTimer;
 explicit RepliesList(History *h) : _history(h) {}
 int computeInboxReadTillFull() { return local; } std::optional<int> computeUnreadCountLocally(int) { return 0; } void setInboxReadTill(int now,std::optional<int>) { local = now; }
 void readTill(int,HistoryItem*,std::optional<bool> = std::nullopt); void sendReadTillRequest(); void reloadUnreadCountIfNeeded() {}
};
REPLY_READ_HERE
}
READ_RPC_HERE
int main() {
 Data::Histories histories; session.owner.h = &histories; History h; auto &state = histories._states[&h]; state.willReadTill = 10; state.willReadWhen = 5;
 ghost.send = false; histories.sendReadRequests(); assert(state.sent.size() == 1 && state.postponed.size() == 1 && state.postponedRequestEntry && !state.willReadTill && !histories._readRequestsTimer.isActive());
 ghost.send = true; histories.readInbox(&h,false); assert(h.local == 10 && h.unread == 0 && !h.marked && !h.networkMarks && histories.reads.empty());
 ghost.send = false; histories.readInbox(&h,true); assert(histories.reads == std::vector<int>{10});
 History delayed; delayed.known = false; ghost.send = true; histories.readInbox(&delayed,false); assert(histories.entry); ghost.send = false; delayed.known = true; histories.entry(); assert(delayed.local == 10 && !delayed.networkMarks && histories.reads.size() == 1);
 History delayedServer; delayedServer.known = false; histories.readInbox(&delayedServer,true); delayedServer.known = true; histories.entry(); assert(histories.reads.size() == 2);
 state.willReadTill = 70; state.explicitReadTill = 60; state.willReadWhen = 5; ghost.send = false; histories.sendReadRequests(); assert(histories.reads.back() == 60);
 History failedEntry; failedEntry.known = false; histories.readInbox(&failedEntry,true); histories.entry(); assert(failedEntry.local == 0 && histories.reads.size() == 3);
 History partial; partial.local = 100; partial.last.id = 110; partial.unread = 10; histories.readInboxTill(&partial,105,false,false); assert(partial.local == 105 && partial.unread == 5 && !partial.networkMarks);
 Data::RepliesList topic(&h); HistoryItem incoming{20,&h}; ghost.send = true; topic.readTill(20,&incoming,false); assert(topic.local == 20 && !topic._readRequestTimer.isActive()); ghost.send = false; topic.sendReadTillRequest(); assert(session.transport.discussion.empty());
 topic.readTill(20,&incoming,true); assert(topic._readRequestTimer.isActive()); crl::clock = 4000; topic.sendReadTillRequest(); assert(session.transport.discussion == std::vector<int>{20});
 ghost.send = true; incoming.id = 30; topic.readTill(30,&incoming); topic.readTill(40,&incoming,false); topic.sendReadTillRequest(); assert(session.transport.discussion.back() == 30 && topic.local == 40);
 ghost.send = false; incoming.id = 50; topic.readTill(50,&incoming); ghost.send = true; topic.sendReadTillRequest(); assert(session.transport.discussion.back() == 30);
 ghost.send = false; incoming.id = 60; topic.readTill(60,&incoming,true); ghost.send = true; topic.readTill(70,&incoming); ghost.send = false; topic.sendReadTillRequest(); assert(session.transport.discussion.back() == 60);
 h.peer->isChannel = true; readHistory(&h.last); session.transport.success(); assert(session.transport.finishes == 1); readHistory(&h.last); session.transport.failure(); assert(session.transport.finishes == 2);
 h.peer->isChannel = false; readHistory(&h.last); session.transport.success(); assert(session.transport.finishes == 3 && session.transport.applied == 1); readHistory(&h.last); session.transport.failure(); assert(session.transport.finishes == 4);
 std::cout << "PASS: actual ghost read intent/history queue/forum delayed dispatch and explicit read finish; RPC/history/timer doubles\n";
}
'''
for marker, value in {'READ_INBOX_HERE':read_inbox,'READ_TILL_HERE':read_till,'QUEUE_HERE':queue,'REPLY_READ_HERE':reply_read,'READ_RPC_HERE':read_rpc}.items(): cpp = cpp.replace(marker,value)
with tempfile.TemporaryDirectory(prefix='luxury-ghost-read-') as directory:
    work = Path(directory); source = work / 'ghost.cpp'; source.write_text(cpp, encoding='utf-8'); exe = work / ('ghost.exe' if os.name == 'nt' else 'ghost')
    subprocess.run([os.environ.get('CXX') or shutil.which('g++') or r'C:\Strawberry\c\bin\g++.exe','-std=c++20','-O0','-D_DEBUG',str(source),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
