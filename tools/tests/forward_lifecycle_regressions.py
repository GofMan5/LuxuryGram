"""Compile actual forward wait/gate, rich media remaps and rich RPC, Debug -O0.

Only steady_clock::now is replaced by a virtual clock; latch, main scheduler,
application/session/data/UI, rich schema and RPC transport are deterministic doubles.
Bundled base weak_ptr (Qt basic_types include shim only), rpl and flat containers are real.
Upload/download/remaining API hooks are source contracts, not native execution.
No network, application launch, account, profile or external test dependency.
"""
from pathlib import Path
import argparse
import os
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "Telegram/SourceFiles"


def read(path):
    return (SOURCE / path).read_text(encoding="utf-8")


def function(text, marker):
    start = text.index(marker)
    opening = text.index("{", start)
    depth = 0
    for end in range(opening, len(text)):
        depth += (text[end] == "{") - (text[end] == "}")
        if not depth:
            return text[start:end + 1]
    raise AssertionError(f"Unclosed source function: {marker}")


def source_contracts(sync, rich):
    header = read("luxury/features/forward/luxury_sync.h")
    assert "using WeakSession = base::weak_ptr<Main::Session>;" in header
    for name in ("sendMessageSync", "sendDocumentSync", "sendStickerSync",
                 "sendVoiceSync", "uploadFileSync", "loadFullRichPageSync",
                 "sendRichMessageSync", "loadPhotoSync", "loadDocumentSync"):
        declarations = re.findall(r"\b" + name + r"\s*\(([^;]+)\);", header)
        assert declarations and all("WeakSession session" in d and "Cancelled" in d for d in declarations), name
    for name in ("sendMessageSync", "sendDocumentSync", "sendStickerSync", "sendVoiceSync"):
        body = function(sync, "bool " + name + "(")
        assert "SendAndWait(session, action," in body and ".localIdAllocated = allocated;" in body, name
        assert ".localSendFinished = finished;" in body, name
    for marker in ("UploadedFile uploadFileSync(",
                   "std::shared_ptr<const Iv::RichPage> loadFullRichPageSync(",
                   "bool sendRichMessageSync("):
        body = function(sync, marker)
        compact = re.sub(r"\s+", "", body)
        assert "WeakSessionsession" in compact and "constCancelled&cancelled" in compact
        assert "!active->load()" in body and "!session.get()" in body
        assert "crl::on_main(session," in body and "if (stopped())" in body
        assert "strong->api().request(*request).cancel();" in body
        cleanup = body.rindex("crl::on_main_sync(")
        assert body.rindex("active->store(false);") < cleanup
        done = body[body.index(".done("):body.index(".fail(")]
        assert "if (stopped())" in done or "if (mediaStopped())" in done
        assert body.index("WaitUntil(") < cleanup
    upload = function(sync, "UploadedFile uploadFileSync(")
    assert "strong->uploader().cancel(*uploadId);" in upload
    assert "lifetime->destroy();" in upload and "data.fullId == *uploadId" in upload
    for name, identifier in (("loadPhotoSync", "PhotoId"), ("loadDocumentSync", "DocumentId")):
        body = function(sync, "QString " + name + "(")
        assert identifier in body and "WeakSession session" in body and "stopped()" in body
        assert "sessionDataAboutToBeCleared()" in body and "lifetime->destroy();" in body
        assert "crl::on_main_sync(" in body
    maps = rich[rich.index("struct RichMedia"):rich.index("[[nodiscard]] PhotoData")]
    assert "base::flat_map<PhotoId, PhotoId>" in maps
    assert "base::flat_map<DocumentId, DocumentId>" in maps and "*" not in maps
    uploaded = header[header.index("struct UploadedFile"):header.index("QString pathForSave")]
    assert "*" not in uploaded and "PhotoId photo" in uploaded and "DocumentId document" in uploaded
    forward = function(rich, "bool forwardRichMessage(")
    assert "base::weak_ptr<Main::Session> session" in forward
    assert "sendRichMessageSync(session, **serialized, action, stopped)" in forward
    for call in ("loadPhotoSync(session, photoId, origin, cancelled)",
                 "loadDocumentSync(session, documentId, origin, cancelled)"):
        assert call in rich
    assert rich.count("session, peerId, path,") == 2
    common = read("api/api_common.h")
    assert "std::shared_ptr<Fn<bool(FullMsgId)>> localIdAllocated;" in common
    assert "std::shared_ptr<Fn<void(FullMsgId, bool)>> localSendFinished;" in common
    # Narrow audit: the veto must precede the side effect in each real allocation path.
    sending = read("api/api_sending.cpp")
    wrap = read("apiwrap.cpp")
    hooks = (
        (wrap, "&& !(*action.options.localIdAllocated)(newId)) return;", "registerMessageRandomId("),
        (sending, "&& !(*message.action.options.localIdAllocated)(newId)) return;", "auto flags = NewMessageFlags("),
        (sending, "&& !(*action.options.localIdAllocated)(newId)) return true;", "auto &histories ="),
        (sending, "&& !(*file->to.options.localIdAllocated)(local.newId)) return true;", "locals.push_back("),
        (sending, "&& !(*file->to.options.localIdAllocated)(local.newId)) return;", "session->uploader().upload("),
    )
    for text, gate, side_effect in hooks:
        position = text.index(gate)
        start = max(text.rfind(marker, 0, position) for marker in (
            "auto newId = FullMsgId(", "auto local = PrepareConfirmedLocalFile(",
            "const auto local = PrepareConfirmedLocalFile("))
        assert start >= 0 and text.index(side_effect, start) > position
    for marker in ("void collectMedia(", "[[nodiscard]] bool prepareBlock("):
        body = function(rich, marker)
        assert re.search(r"case BlockKind::File:\s*(?:addDocument|if \(!remapDocument)\(", body), marker
    watcher = function(sync, "std::shared_ptr<rpl::lifetime> WatchSessionDataClear(")
    assert "crl::on_main_sync(" in watcher and "sessionDataAboutToBeCleared()" in watcher
    assert "active->store(false);" in watcher
    for marker in ("bool forwardMessagesSync(", "UploadedFile uploadFileSync(",
                   "std::shared_ptr<const Iv::RichPage> loadFullRichPageSync(",
                   "bool sendRichMessageSync("):
        body = function(sync, marker)
        assert "WatchSessionDataClear(session, dataActive)" in body
        assert "!dataActive->load()" in body and "sessionLifetime->destroy();" in body
    rich_send = function(sync, "bool sendRichMessageSync(")
    # A forward is not a suggested post: the rich path drops it like the text path.
    assert "sending.options.suggest = {};" in rich_send and "f_suggested_post" not in rich_send
    # These native callbacks cannot be linked without the app; bind their routing explicitly.
    for text, marker, terminal in (
            (wrap, "void ApiWrap::forwardMessages(", "(*action.options.localSendFinished)(FullMsgId(), false);"),
            (wrap, "void ApiWrap::sendMessage(", "(*action.options.localSendFinished)(newId, false);"),
            (sending, "void SendExistingMedia(", "(*action.options.localSendFinished)(newId, false);"),
            (sending, "bool SendDice(", "(*action.options.localSendFinished)(newId, false);"),
            (sending, "void SendMusicSelectionBatch(", "(*action.options.localSendFinished)(item.localId, false);"),
            (wrap, "void ApiWrap::uploadAlbumMedia(", "(*finished)(localId, false);"),
            (wrap, "void ApiWrap::sendMediaWithRandomId(", "(*options.localSendFinished)(itemId, false);"),
            (wrap, "void ApiWrap::sendAlbumIfReady(", "(*finished)(item.msgId, false);")):
        assert terminal in function(text, marker), marker
    existing = function(sending, "void SendExistingMedia(")
    refreshed = existing[existing.index("api->refreshFileReference("):]
    assert refreshed.index("repeatRequest(repeatRequest);") < refreshed.index("localSendFinished")
    assert refreshed.index("} else {") < refreshed.index("localSendFinished")
    paid = function(wrap, "void ApiWrap::sendMultiPaidMedia(")
    assert "const auto parts = album->items;" in paid
    assert "for (const auto &part : parts) (*finished)(part.msgId, true);" in paid
    assert "(*finished)(itemId, false);" in paid
    ephemeral = function(read("data/components/ephemeral_messages.cpp"), "bool EphemeralMessages::trySend(")
    assert "(*finished)(FullMsgId(), false);" in ephemeral and ephemeral.count("dropped();") >= 3
    loader = function(read("storage/localimageloader.cpp"), "void FileLoadTask::finish(")
    assert "(*finished)(FullMsgId(), false);" in loader and loader.count("failed();") == 2
    return "rich data-clear watcher + File collect/remap + rich send drops suggestion + native terminal/preallocation routing + five allocation veto hooks"


CPP = r'''
#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <compare>
#include <cstdlib>
#include <deque>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <gsl/gsl>
using gsl::not_null;
template <typename... Args> constexpr bool false_t(Args&&...) { return false; }
#include <rpl/event_stream.h>
#include <base/flat_map.h>
#include <base/flat_set.h>
WEAK_HEADER
namespace base::assertion { void log(const char*, const char*, int) { std::abort(); } }
template<class T> using Fn = std::function<T>;
using PeerId = long long;
using MsgId = long long;
using uint64 = unsigned long long;
using mtpRequestId = int;
struct FullMsgId { PeerId peer = 0; MsgId msg = 0; explicit operator bool() const { return peer && msg; } auto operator<=>(const FullMsgId&) const = default; };
namespace Test {
struct Clock {
 static inline std::chrono::steady_clock::time_point time;
 static auto now() { return time; }
};
struct Scheduler {
 std::deque<Fn<void()>> main, steps;
 Fn<void()> beforeWait, beforeSync;
 bool autoMain = true, onMain = false;
 int syncCalls = 0;
 void invoke(Fn<void()> call) { const auto previous = onMain; onMain = true; call(); onMain = previous; }
 void drain() { while (!main.empty()) { auto f = std::move(main.front()); main.pop_front(); invoke(std::move(f)); } }
 void tick(std::chrono::milliseconds slice) {
  if (beforeWait) { auto f = std::exchange(beforeWait, {}); f(); }
  if (autoMain && !main.empty()) { auto f = std::move(main.front()); main.pop_front(); invoke(std::move(f)); }
  if (!steps.empty()) { auto f = std::move(steps.front()); steps.pop_front(); invoke(std::move(f)); }
  Clock::time += slice;
 }
 void reset() { assert(main.empty()); steps.clear(); beforeWait = {}; beforeSync = {}; autoMain = true; syncCalls = 0; }
} scheduler;
struct UI { int sends = 0, prepares = 0; void send() { assert(scheduler.onMain); ++sends; } } ui;
}
struct TimedCountDownLatch {
 static inline int live = 0, signals = 0;
 static inline TimedCountDownLatch *current = nullptr;
 int count;
 explicit TimedCountDownLatch(int n) : count(n) { ++live; current = this; }
 ~TimedCountDownLatch() { --live; if (current == this) current = nullptr; }
 void countDown() { ++signals; if (count) --count; }
 bool await(std::chrono::milliseconds slice) { if (count) Test::scheduler.tick(slice); return !count; }
};
bool ready() { return TimedCountDownLatch::current && !TimedCountDownLatch::current->count; }
using PhotoId = uint64;
using DocumentId = uint64;
struct PhotoData { PhotoId id = 0; };
struct DocumentData { DocumentId id = 0; };
struct QString {
 std::string value;
 QString() = default;
 QString(const char *text) : value(text) {}
 bool isEmpty() const { return value.empty(); }
};
enum class EntityType { Mention, Hashtag, BotCommand, Cashtag, Url, Email, Phone, BankCard, Bold, CustomUrl };
struct EntityInText { EntityType kind; EntityType type() const { return kind; } };
struct HistoryMessageMarkupButton {
 enum class Type { Default, Url, Callback, CopyText, Disabled, UserProfile };
 struct Bytes { unsigned long long id = 0; unsigned long long toULongLong() const { return id; } };
 Type type = Type::Disabled; Bytes data;
};
namespace Iv {
struct RichPage {
 BLOCK_KIND
 struct RichText { struct { std::vector<EntityInText> entities; } text; QString anchorId; std::vector<QString> anchorIds; };
 struct Block;
 struct ListItem { RichText text; QString anchorId; std::vector<Block> blocks; };
 struct MediaItem { BlockKind kind = BlockKind::Unsupported; PhotoId photoId = 0; DocumentId documentId = 0; PhotoData *photo = nullptr; DocumentData *document = nullptr; };
 struct TableCell { RichText text; };
 struct TableRow { std::vector<TableCell> cells; };
 struct Button { RichText text; HistoryMessageMarkupButton button; };
 struct Block {
  BlockKind kind = BlockKind::Unsupported;
  PhotoId photoId = 0; DocumentId documentId = 0;
  PhotoData *photo = nullptr; DocumentData *document = nullptr;
  QString anchorId; RichText text, caption;
  bool pullquote = false; int zoom = 0;
  std::vector<Block> blocks; std::vector<ListItem> listItems;
  std::vector<MediaItem> mediaItems; std::vector<TableRow> tableRows;
  std::vector<Button> buttons;
 };
};
}
using HistoryItemsList = std::vector<FullMsgId>;
namespace Data {
enum class ForwardOptions { PreserveInfo };
struct ResolvedForwardDraft { ResolvedForwardDraft(const HistoryItemsList&, ForwardOptions) {} };
struct Session {
 bool resolveMessages = true;
 FullMsgId message(FullMsgId id) { return resolveMessages ? id : FullMsgId{}; }
 struct IdChange { MsgId oldId; FullMsgId newId; };
 rpl::event_stream<IdChange> changed;
 rpl::event_stream<> cleared;
 base::flat_map<PhotoId, std::shared_ptr<PhotoData>> photos;
 base::flat_map<DocumentId, std::shared_ptr<DocumentData>> documents;
 auto photo(PhotoId id) { auto [i, added] = photos.emplace(id, std::make_shared<PhotoData>(PhotoData{id})); return i->second; }
 auto document(DocumentId id) { auto [i, added] = documents.emplace(id, std::make_shared<DocumentData>(DocumentData{id})); return i->second; }
 int acknowledgments = 0;
 auto itemIdChanged() { return changed.events(); }
 auto sessionDataAboutToBeCleared() { return cleared.events(); }
 void ack(FullMsgId old, MsgId next) { ++acknowledgments; changed.fire(IdChange{old.msg, {old.peer, next}}); }
};
}
struct Peer { PeerId id = 7; int stars = 0; int input() const { return 7; } int starsPerMessageChecked() const { return stars; } } peer;
struct History { Peer *peer = &::peer; } history;
namespace Api {
struct SendOptions { std::shared_ptr<Fn<bool(FullMsgId)>> localIdAllocated; std::shared_ptr<Fn<void(FullMsgId, bool)>> localSendFinished; int scheduled = 0, scheduleRepeatPeriod = 0, effectId = 0, suggest = 0, starsApproved = 0; Peer *sendAs = nullptr; };
struct SendAction { History *history = &::history; SendOptions options; int replyTo = 0; int mtpReplyTo() const { return 0; } };
struct MessageToSend { SendAction action; };
int SuggestToMTP(int n) { return n; }
}
struct ApiWrap { using SendAction = Api::SendAction; };
struct MTPInputRichMessage {};
struct MTPUpdates {};
struct MTPMessageEntity {};
struct MTPReplyMarkup {};
struct MTPInputQuickReplyShortcut {};
template<class T> using MTPVector = std::vector<T>;
int MTP_flags(int n) { return n; } int MTP_long(uint64 n) { return int(n); }
int MTP_int(int n) { return n; } int MTP_string(QString) { return 0; } int MTP_inputPeerEmpty() { return 0; }
namespace base { template<class T> T RandomValue() { return T(1); } }
namespace MTP { struct Error {}; }
#define LOG(...) do {} while (0)
bool ShouldSendSilent(Peer*, const Api::SendOptions&) { return false; }
struct MTPmessages_SendMessage {
 enum Flag { f_rich_message = 1, f_reply_to = 2, f_silent = 4, f_schedule_date = 8, f_schedule_repeat_period = 16, f_send_as = 32, f_effect = 64, f_suggested_post = 128, f_allow_paid_stars = 256 };
 using Flags = int;
 int flags = 0, effect = 0, stars = 0, suggested = 0;
 MTPmessages_SendMessage(int flags, int, int, int, int, MTPReplyMarkup,
   MTPVector<MTPMessageEntity>, int, int, int, MTPInputQuickReplyShortcut,
   int effect, int stars, int suggested, const MTPInputRichMessage&) : flags(flags), effect(effect), stars(stars), suggested(suggested) {
  assert(flags & f_rich_message);
  assert(bool(flags & f_suggested_post) == bool(suggested));
  assert(bool(flags & f_effect) == bool(effect) && bool(flags & f_allow_paid_stars) == bool(stars));
 }
};
struct ApiDouble {
 struct Rpc { Fn<void(const MTPUpdates&)> done; Fn<void(const MTP::Error&)> fail; bool cancelled = false; };
 std::vector<Rpc> rpc;
 int cancellations = 0, updates = 0, lastFlags = 0, lastSuggested = 0, lastEffect = 0, lastStars = 0;
 std::vector<FullMsgId> outgoing = {{7, -1}};
 std::shared_ptr<Fn<bool(FullMsgId)>> gate;
 std::shared_ptr<Fn<void(FullMsgId, bool)>> finished;
 Fn<void(FullMsgId)> onAllocated;
 Fn<void()> forwardDone;
 void forwardMessages(Data::ResolvedForwardDraft, Api::SendAction action, Fn<void()> done) {
  Test::ui.send(); finished = action.options.localSendFinished; forwardDone = std::move(done);
 }
 void sendMessage(Api::MessageToSend message) {
  gate = message.action.options.localIdAllocated;
  finished = message.action.options.localSendFinished;
  for (auto id : outgoing) { if (gate && !(*gate)(id)) return; Test::ui.send(); if (onAllocated) onAllocated(id); }
 }
 bool late(FullMsgId id) { if (gate && !(*gate)(id)) return false; Test::ui.send(); return true; }
 struct Builder {
  ApiDouble *owner; Rpc callbacks;
  template<class F> Builder &done(F f) { callbacks.done = std::move(f); return *this; }
  template<class F> Builder &fail(F f) { callbacks.fail = std::move(f); return *this; }
  int send() { Test::ui.send(); owner->rpc.push_back(std::move(callbacks)); return int(owner->rpc.size()); }
 };
 struct Cancellation { ApiDouble *owner; int id; void cancel() { assert(Test::scheduler.onMain); ++owner->cancellations; owner->rpc.at(id - 1).cancelled = true; } };
 Builder request(MTPmessages_SendMessage message) { lastFlags = message.flags; lastSuggested = message.suggested; lastEffect = message.effect; lastStars = message.stars; return {this, {}}; }
 Cancellation request(int id) { return {this, id}; }
 void applyUpdates(const MTPUpdates&) { assert(Test::scheduler.onMain); ++updates; }
 void complete(int id = 1) { auto callback = rpc.at(id - 1).done; callback(MTPUpdates{}); }
 void fail(int id = 1) { auto callback = rpc.at(id - 1).fail; callback(MTP::Error{}); }
};
namespace Main {
struct Session : base::has_weak_ptr {
 Data::Session dataDouble; ApiDouble apiDouble;
 rpl::event_stream<FullMsgId> photoFailures, documentFailures;
 auto &uploader() { return *this; }
 auto photoFailed() { return photoFailures.events(); } auto documentFailed() { return documentFailures.events(); }
 Data::Session &data() { return dataDouble; } ApiDouble &api() { return apiDouble; }
};
}
namespace Core {
struct Application {
 std::unique_ptr<Main::Session> session = std::make_unique<Main::Session>();
 void logout() { session->data().cleared.fire({}); session.reset(); }
 void replace() { logout(); session = std::make_unique<Main::Session>(); }
};
}
namespace crl {
template<class T, class F> void on_main(base::weak_ptr<T> owner, F f) {
 Test::scheduler.main.push_back([owner, f = std::move(f)]() mutable { if (owner.get()) f(); });
}
template<class F> void on_main_sync(F f) {
 ++Test::scheduler.syncCalls;
 if (Test::scheduler.beforeSync) { auto hook = std::exchange(Test::scheduler.beforeSync, {}); hook(); }
 Test::scheduler.invoke(std::move(f));
}
}
// Doubles of the shared send helpers: a reply the server cannot take is
// marked negative, ghost mode schedules what has no date yet.
int ghostDate = 0;
void StripUnsendableReply(not_null<Main::Session*>, int &replyTo) { if (replyTo < 0) replyTo = 0; }
void applyGhostScheduling(not_null<Main::Session*>, Api::SendOptions &options) { if (ghostDate && !options.scheduled) options.scheduled = ghostDate; }
namespace LuxurySync {
using WeakSession = base::weak_ptr<Main::Session>;
using Cancelled = Fn<bool()>;
WATCH_DATA_CLEAR
WAIT_AND_SEND
SEND_MESSAGE
FORWARD_MESSAGES
SEND_RICH
}
namespace LuxuryForward {
using Block = Iv::RichPage::Block;
using BlockKind = Iv::RichPage::BlockKind;
RICH_MEDIA
RUN_ON_MAIN
}
void cleanup(Main::Session &session) {
 const auto signals = TimedCountDownLatch::signals;
 session.data().ack({7, -999}, 9); session.data().cleared.fire({});
 session.photoFailures.fire(FullMsgId{7, -1}); session.documentFailures.fire(FullMsgId{7, -1});
 assert(TimedCountDownLatch::signals == signals); // Real rpl consumers were detached.
 session.api().gate.reset(); session.api().finished.reset(); session.api().onAllocated = {}; session.api().forwardDone = {};
 Test::scheduler.drain(); assert(!TimedCountDownLatch::live);
}
int main() {
 using namespace LuxurySync;
 const auto noCancel = Cancelled([] { return false; });
 Api::SendAction action;
 const auto trackedSend = [&](auto strong, const auto &gate, const auto &finished) {
  auto m = Api::MessageToSend{action}; m.action.options.localIdAllocated = gate; m.action.options.localSendFinished = finished;
  strong->api().sendMessage(std::move(m)); return true;
 };
 { // Actual collect/remap logic: File IDs and pointers, nested blocks/list items, pruning and deduplication.
  using namespace LuxuryForward;
  Main::Session s;
  const auto file = [](DocumentId id, DocumentData *document = nullptr) {
   Block b; b.kind = BlockKind::File; b.documentId = id; b.document = document; return b;
  };
  auto blocks = std::vector<Block>{file(10), file(999, s.data().document(11).get()), file(10), file(0)};
  Block nested; nested.kind = BlockKind::Paragraph; nested.blocks = {file(12)};
  nested.listItems.resize(1); nested.listItems[0].blocks = {file(13)};
  blocks.push_back(nested);
  Block unsupported; unsupported.kind = BlockKind::Embed; unsupported.blocks = {file(14)};
  blocks.push_back(unsupported);
  RichMedia collected; collectMedia(&s, blocks, collected);
  assert(collected.photos.empty() && collected.documents.size() == 4);
  for (auto id : {10, 11, 12, 13}) { const auto i = collected.documents.find(id); assert(i != collected.documents.end() && i->second == DocumentId(id)); }
  assert(collected.documents.find(0) == collected.documents.end() && collected.documents.find(14) == collected.documents.end());
  RichMedia remap; remap.documents.emplace(10, 110); remap.documents.emplace(11, 111); remap.documents.emplace(13, 113);
  blocks[0].text.anchorId = "old"; blocks[0].text.anchorIds = {QString("old")};
  blocks[0].text.text.entities = {{EntityType::Url}, {EntityType::Bold}, {EntityType::CustomUrl}};
  assert(prepareBlock(&s, blocks[0], remap));
  assert(blocks[0].documentId == 110 && blocks[0].document == s.data().document(110).get());
  assert(blocks[0].text.anchorId.isEmpty() && blocks[0].text.anchorIds.empty());
  assert(blocks[0].text.text.entities.size() == 2 && blocks[0].text.text.entities[0].type() == EntityType::Bold);
  assert(prepareBlock(&s, blocks[1], remap) && blocks[1].documentId == 111);
  assert(blocks[1].document == s.data().document(111).get()); // Pointer wins over stale documentId.
  assert(!prepareBlock(&s, blocks[3], remap));
  auto missing = file(12); assert(!prepareBlock(&s, missing, remap) && missing.documentId == 12 && !missing.document);
  assert(!prepareBlock(&s, blocks[5], remap));
  pruneBlocks(&s, nested.blocks, remap); assert(nested.blocks.empty());
  assert(prepareBlock(&s, nested, remap));
  assert(nested.listItems[0].blocks[0].documentId == 113);
  assert(nested.listItems[0].blocks[0].document == s.data().document(113).get());
  using ButtonType = HistoryMessageMarkupButton::Type;
  Block row; row.kind = BlockKind::ButtonRow; row.buttons.resize(5);
  row.buttons[0].button.type = ButtonType::Url; row.buttons[0].text.anchorId = "old";
  row.buttons[1].button.type = ButtonType::Callback;
  row.buttons[2].button.type = ButtonType::UserProfile;
  row.buttons[3].button.type = ButtonType::UserProfile; row.buttons[3].button.data.id = 5;
  row.buttons[4].button.type = ButtonType::CopyText;
  assert(prepareBlock(&s, row, remap) && row.buttons.size() == 3); // Unwritable buttons go, the page stays.
  assert(row.buttons[0].button.type == ButtonType::Url && row.buttons[0].text.anchorId.isEmpty());
  assert(row.buttons[1].button.data.id == 5 && row.buttons[2].button.type == ButtonType::CopyText);
  Block dead; dead.kind = BlockKind::ButtonRow; dead.buttons.resize(1); dead.buttons[0].button.type = ButtonType::Callback;
  assert(!prepareBlock(&s, dead, remap));
 }
 { // Same peer is insufficient; match old identity, including scheduled nonserver new ID.
  Core::Application app; auto &s = *app.session;
  Test::scheduler.steps = {
   [&] { s.data().ack({7, -88}, 42); assert(!ready()); },
   [&] { s.data().ack({8, -1}, 43); assert(!ready()); },
   [&] { s.data().ack({7, 99}, -1); assert(!ready()); },
   [&] { s.data().ack({7, -1}, (1LL << 56) + 11); }
  };
  assert(sendMessageSync(&s, Api::MessageToSend{action}, noCancel));
  assert(!(*s.api().gate)({7, -2})); cleanup(s);
 }
 Test::scheduler.reset();
 { // A split message allocates more than expected: every pending ID must finish.
  Main::Session s; s.api().outgoing = {{7, -1}, {7, -2}, {7, -3}};
  Test::scheduler.steps = {
   [&] { s.data().ack({7, -1}, 1); assert(!ready()); },
   [&] { s.data().ack({7, -1}, 1); s.data().ack({7, -2}, 2); assert(!ready()); },
   [&] { s.data().ack({7, -3}, (1LL << 56) + 12); }
  };
  assert(sendMessageSync(&s, Api::MessageToSend{action}, noCancel)); cleanup(s);
 }
 Test::scheduler.reset();
 { // Pending empty alone cannot finish before the expected number has allocated.
  Main::Session s;
  Test::scheduler.steps = {
   [&] { s.data().ack({7, -1}, 1); assert(!ready()); },
   [&] { assert(s.api().late({7, -2})); assert(!ready()); s.data().ack({7, -2}, 2); }
  };
  assert(SendAndWait(&s, action, 2, noCancel, [&](auto strong, const auto &gate, const auto &finished) {
   auto message = Api::MessageToSend{action}; message.action.options.localIdAllocated = gate; message.action.options.localSendFinished = finished;
   strong->api().sendMessage(std::move(message)); return true;
  })); cleanup(s);
 }
 Test::scheduler.reset();
 { // Synchronous ID changes during dispatch must not release the wait early.
  Main::Session s; s.api().outgoing = {{7, -1}, {7, -2}};
  s.api().onAllocated = [&](FullMsgId id) { s.data().ack(id, 5); assert(!ready()); };
  assert(sendMessageSync(&s, Api::MessageToSend{action}, noCancel)); cleanup(s);
 }
 Test::scheduler.reset();
 { // Duplicate allocations do not satisfy expected count.
  Main::Session s; s.api().outgoing = {{7, -1}, {7, -1}};
  Test::scheduler.steps = {[&] { s.data().ack({7, -1}, 1); assert(!ready()); }};
  assert(!SendAndWait(&s, action, 2, noCancel, [&](auto strong, const auto &gate, const auto &finished) {
   auto m = Api::MessageToSend{action}; m.action.options.localIdAllocated = gate; m.action.options.localSendFinished = finished;
   strong->api().sendMessage(std::move(m)); return true;
  })); cleanup(s);
 }
 Test::scheduler.reset();
 { // The actual terminal hook also resolves own IDs and reports RPC failure.
  Main::Session s; s.api().outgoing = {{7, -1}, {7, -2}};
  Test::scheduler.steps = {
   [&] { (*s.api().finished)({7, -88}, true); (*s.api().finished)({7, -88}, false); assert(!ready()); (*s.api().finished)({7, -1}, true); assert(!ready()); },
   [&] { (*s.api().finished)({7, -2}, true); }
  };
  assert(sendMessageSync(&s, Api::MessageToSend{action}, noCancel)); cleanup(s);
 }
 Test::scheduler.reset();
 { Main::Session s;
  Test::scheduler.steps = {[&] { (*s.api().finished)({7, -1}, false); }};
  assert(!sendMessageSync(&s, Api::MessageToSend{action}, noCancel));
  const auto signals = TimedCountDownLatch::signals;
  (*s.api().finished)({7, -1}, true); assert(TimedCountDownLatch::signals == signals); cleanup(s);
 }
 for (bool photo : {false, true}) { // Actual rpl uploader subscriptions: exact pending identity only.
  Test::scheduler.reset(); Main::Session s;
  auto &failures = photo ? s.photoFailures : s.documentFailures;
  Test::scheduler.steps = {
   [&] { failures.fire(FullMsgId{7, -88}); failures.fire(FullMsgId{8, -1}); assert(!ready()); },
   [&] { failures.fire(FullMsgId{7, -1}); assert(ready()); assert(!(*s.api().gate)({7, -2})); }
  };
  assert(!sendMessageSync(&s, Api::MessageToSend{action}, noCancel)); cleanup(s);
 }
 Test::scheduler.reset();
 { // A synchronous own failure must veto later allocations in the same dispatch.
  Main::Session s; s.api().outgoing = {{7, -1}, {7, -2}, {7, -3}};
  s.api().onAllocated = [&](FullMsgId id) { assert((id == FullMsgId{7, -1})); (*s.api().finished)(id, false); };
  const auto sends = Test::ui.sends;
  assert(!sendMessageSync(&s, Api::MessageToSend{action}, noCancel));
  assert(Test::ui.sends == sends + 1 && !(*s.api().gate)({7, -4})); cleanup(s);
 }
 Test::scheduler.reset();
 { // Null failure before any ID exists is terminal (ephemeral drop/file-loader error).
  Main::Session s; const auto sends = Test::ui.sends;
  assert(!SendAndWait(&s, action, 3, noCancel, [&](auto, const auto &gate, const auto &finished) {
   (*finished)(FullMsgId{}, true); assert(!ready()); // Null success is not completion.
   (*finished)(FullMsgId{}, false); assert(ready()); assert(!(*gate)({7, -1})); return true;
  }));
  assert(Test::ui.sends == sends); cleanup(s);
 }
 Test::scheduler.reset();
 { // Paid-like albums can complete all local parts without any IdChange event.
  Main::Session s; s.api().outgoing = {{7, -1}, {7, -2}, {7, -3}};
  Test::scheduler.steps = {
   [&] { (*s.api().finished)({7, -3}, true); (*s.api().finished)({7, -3}, true); assert(!ready()); },
   [&] { (*s.api().finished)({7, -1}, true); s.photoFailures.fire(FullMsgId{7, -1}); assert(!ready()); },
   [&] { (*s.api().finished)({7, -2}, true); assert(ready()); }
  };
  assert(SendAndWait(&s, action, 3, noCancel, trackedSend));
  assert(!s.data().acknowledgments); cleanup(s);
 }
 for (bool cancel : {false, true}) { // Timeout/cancel reject delayed file-task allocations.
  Test::scheduler.reset(); Main::Session s; bool cancelled = false;
  if (cancel) Test::scheduler.steps = {[&] { cancelled = true; }};
  assert(!sendMessageSync(&s, Api::MessageToSend{action}, [&] { return cancelled; }));
  const auto sends = Test::ui.sends;
  Test::scheduler.invoke([&] { assert(!s.api().late({7, -2})); });
  assert(Test::ui.sends == sends); cleanup(s);
 }
 for (bool cancel : {false, true}) { // Work queued until after return must never dispatch.
  Test::scheduler.reset(); Test::scheduler.autoMain = false; Main::Session s; bool cancelled = false;
  if (cancel) Test::scheduler.beforeWait = [&] { cancelled = true; };
  const auto sends = Test::ui.sends;
  assert(!sendMessageSync(&s, Api::MessageToSend{action}, [&] { return cancelled; }));
  Test::scheduler.drain(); assert(Test::ui.sends == sends); cleanup(s);
 }
 Test::scheduler.reset();
 { // Logout before main dispatch must not send through a replacement account.
  Core::Application app; auto weak = WeakSession(app.session.get());
  Test::scheduler.autoMain = false; Test::scheduler.beforeWait = [&] { app.replace(); };
  const auto sends = Test::ui.sends;
  assert(!sendMessageSync(weak, Api::MessageToSend{action}, noCancel));
  Test::scheduler.drain(); assert(Test::ui.sends == sends && !weak.get()); cleanup(*app.session);
 }
 Test::scheduler.reset();
 { // Clearing live session state wakes the waiter and destroys real rpl subscriptions.
  Core::Application app; Test::scheduler.steps = {[&] { app.logout(); }};
  assert(!sendMessageSync(app.session.get(), Api::MessageToSend{action}, noCancel));
  assert(!TimedCountDownLatch::live);
 }
 Test::scheduler.reset();
 { Main::Session s;
  assert(!SendAndWait(&s, action, 1, noCancel, [](auto, const auto&, const auto&) { return false; })); cleanup(s);
  const auto sends = Test::ui.sends;
  assert(!sendMessageSync(&s, Api::MessageToSend{action}, [] { return true; }));
  assert(Test::ui.sends == sends && Test::scheduler.main.empty());
 }
 Test::scheduler.reset();
 { Core::Application app; auto weak = WeakSession(app.session.get()); bool cancelled = false;
  auto callback = [](not_null<Main::Session*>) { assert(Test::scheduler.onMain); ++Test::ui.prepares; };
  assert(LuxuryForward::runOnMainSync(weak, noCancel, callback));
  const auto prepares = Test::ui.prepares;
  Test::scheduler.beforeSync = [&] { cancelled = true; };
  assert(!LuxuryForward::runOnMainSync(weak, [&] { return cancelled; }, callback));
  Test::scheduler.beforeSync = [&] { app.replace(); };
  assert(!LuxuryForward::runOnMainSync(weak, noCancel, callback));
  assert(Test::ui.prepares == prepares);
 }
 for (int mode : {0, 1, 2, 3, 4, 5, 6}) { // Ordinary forward completion and clear.
   Test::scheduler.reset(); Main::Session s; bool cancelled = false;
   const auto sends = Test::ui.sends;
   if (mode == 0) Test::scheduler.steps = {[&] { s.api().forwardDone(); }};
   if (mode == 1) Test::scheduler.steps = {[&] { (*s.api().finished)(FullMsgId{}, false); }};
   if (mode == 3) Test::scheduler.steps = {[&] { cancelled = true; }};
   if (mode == 4) Test::scheduler.steps = {[&] { s.data().cleared.fire({}); s.api().forwardDone(); }};
   if (mode == 5) s.data().resolveMessages = false;
   if (mode == 6) { Test::scheduler.autoMain = false; Test::scheduler.beforeWait = [&] { s.data().cleared.fire({}); }; }
   assert(forwardMessagesSync(&s, {{7, 42}}, action, Data::ForwardOptions::PreserveInfo, [&] { return cancelled; }) == (mode == 0));
   Test::scheduler.drain();
   assert(Test::ui.sends == sends + (mode == 5 || mode == 6 ? 0 : 1));
   cleanup(s);
  }
  for (int suggested : {0, 73}) { // A forwarded rich message never carries the composer's suggestion.
  Test::scheduler.reset(); Main::Session s; auto richAction = action; richAction.options.suggest = suggested;
  Test::scheduler.steps = {[&] { s.api().complete(); }};
  assert(sendRichMessageSync(&s, MTPInputRichMessage{}, richAction, noCancel));
  assert(s.api().lastSuggested == 0);
  assert(!(s.api().lastFlags & MTPmessages_SendMessage::f_suggested_post));
  assert(s.api().lastFlags & MTPmessages_SendMessage::f_rich_message);
  s.api().rpc.clear(); assert(!TimedCountDownLatch::live);
 }
 for (bool ghost : {false, true}) { // Same rules as the text of a forward: no effect, ghost date, sendable reply, capped price.
  using F = MTPmessages_SendMessage;
  Test::scheduler.reset(); Main::Session s; auto richAction = action;
  richAction.options.effectId = 5; richAction.replyTo = -3; richAction.options.starsApproved = 10; peer.stars = 4; ghostDate = ghost ? 777 : 0;
  Test::scheduler.steps = {[&] { s.api().complete(); }};
  assert(sendRichMessageSync(&s, MTPInputRichMessage{}, richAction, noCancel));
  assert(!s.api().lastEffect && !(s.api().lastFlags & F::f_effect) && !(s.api().lastFlags & F::f_reply_to));
  assert(s.api().lastStars == 4 && (s.api().lastFlags & F::f_allow_paid_stars));
  assert(bool(s.api().lastFlags & F::f_schedule_date) == ghost);
  peer.stars = 0; ghostDate = 0; s.api().rpc.clear(); assert(!TimedCountDownLatch::live);
 }
 for (int mode : {0, 1, 2}) { // Live-session data clear: queued veto, in-flight cancel, stale-done veto.
  Test::scheduler.reset(); Main::Session s; const auto sends = Test::ui.sends;
  if (mode == 0) { Test::scheduler.autoMain = false; Test::scheduler.beforeWait = [&] { s.data().cleared.fire({}); }; }
  else Test::scheduler.steps = {[&] { s.data().cleared.fire({}); if (mode == 2) s.api().complete(); assert(!s.api().updates); }};
  assert(!sendRichMessageSync(&s, MTPInputRichMessage{}, action, noCancel));
  Test::scheduler.drain(); assert(Test::ui.sends == sends + (mode == 0 ? 0 : 1));
  assert(!s.api().updates); if (mode != 0) assert(s.api().rpc[0].cancelled == (mode == 1));
  if (mode == 1) { Test::scheduler.invoke([&] { s.api().complete(); }); assert(!s.api().updates); }
  s.api().rpc.clear(); assert(!TimedCountDownLatch::live);
 }
 for (int mode : {0, 1, 2, 3}) { // Real rich RPC body: success/fail/timeout/cancel.
  Test::scheduler.reset(); Main::Session s; bool cancelled = false;
  if (mode == 0) Test::scheduler.steps = {[&] { s.api().complete(); }};
  if (mode == 1) Test::scheduler.steps = {[&] { s.api().fail(); }};
  if (mode == 3) Test::scheduler.steps = {[&] { cancelled = true; }};
  assert(sendRichMessageSync(&s, MTPInputRichMessage{}, action, [&] { return cancelled; }) == (mode == 0));
  assert(s.api().updates == (mode == 0));
  assert(s.api().cancellations == (mode >= 2));
  if (mode >= 2) { assert(s.api().rpc[0].cancelled); Test::scheduler.invoke([&] { s.api().complete(); }); assert(!s.api().updates); }
  s.api().rpc.clear(); assert(!TimedCountDownLatch::live);
 }
 Test::scheduler.reset();
 { Core::Application app; auto weak = WeakSession(app.session.get()); Test::scheduler.autoMain = false;
  Test::scheduler.beforeWait = [&] { app.replace(); }; const auto sends = Test::ui.sends;
  assert(!sendRichMessageSync(weak, MTPInputRichMessage{}, action, noCancel));
  Test::scheduler.drain(); assert(Test::ui.sends == sends && !TimedCountDownLatch::live);
 }
 std::cout << "PASS: extracted SendAndWait/WaitUntil/sendMessage/forwardMessages/runOnMainSync/sendRich/data-clear watcher + collectMedia/prepareBlock/prune/sanitize; real weak/rpl/flat containers; File remaps, exact uploader failures, synchronous/null prealloc veto, paid-like terminal completion, rich send without suggestion/effect with ghost date/sendable reply/capped stars, button-row pruning, cancellation/logout/RPC cleanup\n";
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cxx", default=os.environ.get("CXX") or shutil.which("g++") or r"C:\Strawberry\c\bin\g++.exe")
    args = parser.parse_args()
    sync = read("luxury/features/forward/luxury_sync.cpp")
    rich = read("luxury/features/forward/luxury_forward_rich.cpp")
    contracts = source_contracts(sync, rich)
    wait = function(sync, "bool WaitUntil(")
    send = function(sync, "template <typename Callback>\nbool SendAndWait(")
    # Only the platform clock changes: all polling, dispatch and result logic is source-extracted.
    wait = wait.replace("std::chrono::steady_clock::now()", "Test::Clock::now()")
    weak = (ROOT / "Telegram/lib_base/base/weak_ptr.h").read_text(encoding="utf-8")
    weak = weak.replace('#include "base/basic_types.h"', '#include <gsl/gsl>').replace("#pragma once", "")
    cpp = CPP
    for marker, value in {
        "WEAK_HEADER": weak,
        "WATCH_DATA_CLEAR": function(sync, "std::shared_ptr<rpl::lifetime> WatchSessionDataClear("),
        "WAIT_AND_SEND": wait + "\n" + send,
        "BLOCK_KIND": function(read("iv/iv_rich_page.h"), "enum class BlockKind : uchar").replace("uchar", "unsigned char") + ";",
        "RICH_MEDIA": "\n".join([
            function(rich, "struct RichMedia") + ";",
            function(rich, "[[nodiscard]] PhotoData *resolvePhoto("),
            function(rich, "[[nodiscard]] DocumentData *resolveDocument("),
            function(rich, "[[nodiscard]] bool isSerializableKind("),
            function(rich, "void collectMedia("),
            function(rich, "void sanitizeRichText("),
            function(rich, "[[nodiscard]] bool isSerializableButton("),
            "void pruneBlocks(not_null<Main::Session*> session, std::vector<Block> &blocks, const RichMedia &remap);",
            function(rich, "[[nodiscard]] bool prepareBlock("),
            function(rich, "void pruneBlocks(not_null<Main::Session*> session, std::vector<Block> &blocks, const RichMedia &remap) {"),
        ]),
        "SEND_MESSAGE": function(sync, "bool sendMessageSync("),
        "FORWARD_MESSAGES": function(sync, "bool forwardMessagesSync("),
        "SEND_RICH": function(sync, "bool sendRichMessageSync("),
        "RUN_ON_MAIN": function(rich, "[[nodiscard]] bool runOnMainSync("),
    }.items():
        cpp = cpp.replace(marker, value)
    with tempfile.TemporaryDirectory(prefix="luxury-forward-lifecycle-") as directory:
        work = Path(directory)
        test = work / "forward.cpp"
        test.write_text(cpp, encoding="utf-8")
        exe = work / ("forward.exe" if os.name == "nt" else "forward")
        command = [args.cxx, "-std=c++20", "-O0", "-g", "-D_DEBUG"]
        for include in ("Telegram/lib_rpl", "Telegram/lib_base", "Telegram/ThirdParty/GSL/include"):
            command += ["-I", str(ROOT / include)]
        subprocess.run(command + [str(test), "-o", str(exe)], check=True, timeout=90)
        subprocess.run([str(exe)], check=True, timeout=15)
    print("PASS: source contracts:", contracts)
    print("GAPS: no native Qt/app/UI/RPC/upload/download execution; native send/paid-album/prealloc routing is source-bound only; latch/scheduler/clock, rich schema/Data and MTP values are doubles.")


if __name__ == "__main__":
    main()
