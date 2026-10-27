"""Execute actual M1-M5 production slices with explicit peer/UI/send doubles.
python tools/tests/messaging_intent.py [--cxx g++]
No Telegram RPC, real allocation at INT_MAX, profile, native Qt menu or delivery.
"""
from pathlib import Path
import argparse
import os
import shutil
import subprocess
import tempfile
root = Path(__file__).resolve().parents[2]
def read(path): return (root / 'Telegram/SourceFiles' / path).read_text('utf-8')
common = read('api/api_common.h')
options = common[common.index('struct SendOptions {'):common.index('[[nodiscard]] SendOptions DefaultSendWhenOnlineOptions()')]
silent = read('history/history_item_helpers.cpp')
silent = silent[silent.index('bool ShouldSendSilent('):silent.index('HistoryItem *LookupReplyTo(')]
menu = read('menu/menu_send.cpp')
menu_block = menu[menu.index('\t\tconst auto peer = maybeShow && details.barePeerId'):menu.index('\n\t}', menu.index('\t\tconst auto peer = maybeShow && details.barePeerId'))]
assert '.silent = true' in menu[menu.index('request->check(Command::SendSilentMessage)'):]
forward = read('luxury/features/forward/luxury_forward.cpp')
prep = forward[forward.index('\t\tauto message = Api::MessageToSend(job.action);'):forward.index('\n\t\tif (!item.downloadable) {', forward.index('\t\tauto message = Api::MessageToSend(job.action);'))]
assert 'message.action.options.scheduled = 0;' not in prep
assert '.action = action,' in forward
helper = read('luxury/utils/telegram_helpers.cpp')
schedule = helper[helper.index('void applyGhostScheduling('):helper.index('\n}\n', helper.index('void applyGhostScheduling(')) + 3]
collect = read('history/view/history_view_list_widget.cpp')
collect = collect[collect.index('std::vector<not_null<HistoryItem*>> ListWidget::collectBetween('):collect.index('std::vector<not_null<HistoryItem*>> ListWidget::selectionUpTo(')]
assert 'result.reserve(max);' not in collect
assert 'std::min<size_t>(max, toIt - fromIt + 1)' in collect
inner = read('history/history_inner_widget.cpp')
inner = inner[inner.index('\tconst auto collectBetween = [=]('):inner.index('\tconst auto addSelectMessageAction')]
# max is INT_MAX with the selection limit off; reserving it is a 16 GB allocation.
assert 'reserve(' not in inner and 'collected.size() > max' in inner
repeat = read('luxury/ui/context_menu/context_menu.cpp')
repeat = repeat[repeat.index('\t\t\t\t\tif (!media || media->webpage())'):repeat.index('\n\t\t\t\t}', repeat.index('\t\t\t\t\tif (!media || media->webpage())'))]
gifs = read('chat_helpers/gifs_list_widget.cpp')
confirm = gifs[gifs.index('\tconst auto confirmSend ='):gifs.index('\n\tif (const auto photo', gifs.index('\tconst auto confirmSend ='))]
saved = gifs[gifs.index('\t\t\tauto from = messageSendingFrom();'):gifs.index('\n\t\t} else if (!preview.usingThumbnail())')]
inline = gifs[gifs.index('\t\tif (inlineResult->onChoose(item))'):gifs.index('\n\t}\n}', gifs.index('\t\tif (inlineResult->onChoose(item))'))]
assert gifs.count('settings.gifConfirmation()') == 1 and gifs.count('confirmSend(') == 2
assert 'getResult() const' in read('inline_bots/inline_bot_layout_item.h') and 'std::shared_ptr<Result> getResult()' in read('inline_bots/inline_bot_layout_item.h')
music = read('boxes/music_attach_box.cpp')
merge = music[music.index('\tif (options.silent != empty.silent'):music.index('\n\tif (options.handleSupportSwitch', music.index('\tif (options.silent != empty.silent'))]
scheduled = read('history/view/history_view_schedule_box.cpp')
schedule_merge = scheduled[scheduled.index('\t\tif (action.options.silent'):scheduled.index('\n\t\tif (action.options.effectId', scheduled.index('\t\tif (action.options.silent'))]
assert '.barePeerId = details.barePeerId,' in scheduled
cpp = r'''
#include <algorithm>
#include <cassert>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <set>
#include <vector>
#include <gsl/gsl>
using gsl::not_null;
using uint64 = uint64_t; using TimeId = int64_t; using BusinessShortcutId = int; using EffectId = uint64_t; using QByteArray = std::string; using int64 = int64_t;
namespace crl { using time = int64_t; }
struct SuggestOptions { bool active = false; friend bool operator==(const SuggestOptions&, const SuggestOptions&) = default; };
struct PeerData;
struct FullMsgId { int value = 0; friend bool operator==(FullMsgId, FullMsgId) = default; };
template<class T> using Fn = std::function<T>;
namespace Api { OPTIONS_HERE }
Api::SendOptions mergeMusic(Api::SendOptions base, const Api::SendOptions &options) { const auto empty = Api::SendOptions(); MUSIC_MERGE_HERE return base; }
Api::SendOptions mergeSchedule(Api::SendOptions options, const Api::SendOptions &input) { struct Action { Api::SendOptions options; } action{input}; SCHEDULE_MERGE_HERE return options; }
struct Settings { bool supportSilent = false; bool supportAllSilent() const { return supportSilent; } struct Proxy { bool enabled = false; bool isEnabled() const { return enabled; } } proxyValue; auto &proxy() { return proxyValue; } };
struct Ghost { bool silent = false, schedule = false; bool shouldSendWithoutSound() const { return silent; } bool isUseScheduledMessages() const { return schedule; } };
namespace Main { struct Session { Ghost ghost; Settings setting; bool support = false; bool supportMode() const { return support; } Settings &settings() { return setting; } }; }
struct Owner { bool silent = false; Owner &notifySettings() { return *this; } bool silentPosts(PeerData*) const { return silent; } };
struct PeerData { Main::Session *s; Owner owned; bool broadcast = false; Main::Session &session() { return *s; } Owner &owner() { return owned; } bool isBroadcast() const { return broadcast; } };
namespace LuxurySettings { Ghost &ghost(Main::Session *s) { return s->ghost; } }
SILENT_HERE
namespace Core { struct Application { Settings setting; Settings &settings() { return setting; } }; Application app; Application &App() { return app; } }
namespace base::unixtime { int64_t now() { return 1000; } }
SCHEDULE_HERE
struct Action { Api::SendOptions options; int replaceMediaOf = 9; };
namespace Api { struct MessageToSend { Action action; int textWithTags = 0; explicit MessageToSend(Action a) : action(a) {} }; }
namespace Data { enum class ForwardOptions { NoNamesAndCaptions, PreserveInfo }; }
Api::MessageToSend prepare(Action action, bool downloadable, Data::ForwardOptions forwardOptions) {
 struct Job { Action action; Data::ForwardOptions options; } job{action, forwardOptions};
 struct Item { bool invertCaption = true, downloadable; int text = 42; } item{true, downloadable};
PREP_HERE
 return message;
}
struct HistoryItem { int id; bool selectable = true; int fullId() const { return id; } };
struct View { HistoryItem *item; HistoryItem *data() { return item; } };
struct Delegate { bool listIsItemGoodForSelection(HistoryItem *item) { return item->selectable; } };
namespace ranges { template <class C, class V> auto find(const C &c, const V &v) { return std::find(c.begin(), c.end(), v); } }
struct ListWidget {
 std::vector<not_null<View*>> _items; Delegate *_delegate; std::set<int> _selected;
 View *viewForItem(HistoryItem *item) const { for (auto view : _items) if(view->item == item) return view; return nullptr; }
 std::vector<not_null<HistoryItem*>> collectBetween(not_null<HistoryItem*>, not_null<HistoryItem*>, int) const;
};
COLLECT_HERE
using PeerId = uint64_t;
struct MenuSession { PeerData *peer; struct Data { PeerData *peer; PeerData *peerLoaded(PeerId) { return peer; } }; Data data() { return {peer}; } };
struct Show { MenuSession owner; MenuSession &session() { return owner; } };
struct Details { uint64_t barePeerId = 1; };
struct Icons { int menuUnmute = 1, menuMute = 2; };
namespace tr { inline int now = 0; int luxury_SendWithSound(int) { return 1; } int lng_send_silent_message(int) { return 2; } int luxury_ConfirmationGIF() { return 3; } int lng_send_button() { return 4; } }
struct Menu { std::function<void()> click; int label = 0, icon = 0; void addAction(int text, std::function<void()> fn, const int *i) { label = text; click = std::move(fn); icon = *i; } };
struct MenuAction { Api::SendOptions options; };
Api::SendOptions menuOptions(PeerData *p, bool haveShow, bool havePeerId, int &label) {
 Show storage{{p}}; auto maybeShow = haveShow ? &storage : nullptr; Details details{havePeerId ? 1u : 0u}; Icons icons; Menu storageMenu; auto menu = &storageMenu;
 const auto &ghost = p->s->ghost; Api::SendOptions result;
 auto action = [&](MenuAction a, Details) { result = a.options; };
MENU_HERE
 label = menu->label; menu->click(); return result;
}
struct Media { int kind = 0; bool webpage() const { return kind == 1 || kind == 2; } int photo() const { return kind == 1 || kind == 3 ? 1 : 0; } int document() const { return kind == 4 ? 1 : 0; } };
int dispatched = 0;
namespace Api { void SendExistingPhoto(int, int) { dispatched = 2; } void SendExistingDocument(int, int) { dispatched = 3; } }
struct RepeatSession { RepeatSession &api() { return *this; } void sendMessage(int) { dispatched = 1; } };
void repeat(Media *media) { int message = 1; RepeatSession strong; auto session = &strong;
REPEAT_HERE
}
struct Result { bool ready = true; bool onChoose(void*) { return ready; } };
struct Event { int document = 0; std::shared_ptr<Result> result; int bot = 0; Api::SendOptions options; int messageSendingFrom = 0; bool needsCaption = false; };
struct Stream { std::vector<Event> sent; void fire(Event event) { sent.push_back(std::move(event)); } };
template <class T> using Fn = std::function<T>;
struct Confirm { int text; Fn<void()> confirmed; int confirmText; };
namespace Ui { Confirm MakeConfirmBox(Confirm box) { return box; } }
struct GifShow { std::vector<Fn<void()>> pending; void showBox(Confirm box) { pending.push_back(std::move(box.confirmed)); } };
struct GifSettings { bool enabled = true; bool gifConfirmation() const { return enabled; } };
namespace LuxurySettings { GifSettings gifs; GifSettings &getInstance() { return gifs; } }
namespace crl { template <class T, class F> auto guard(T *widget, F callback) { return [weak = std::weak_ptr<int>(widget->alive), callback = std::move(callback)]() mutable { if(!weak.expired()) callback(); }; } }
struct Gifs {
 std::shared_ptr<int> alive = std::make_shared<int>(1); GifShow show; GifShow *_show = &show; bool _requireConfirmation = true; int _searchBot = 7; Stream _fileChosen, _inlineResultChosen;
 void choose(bool saved, std::shared_ptr<Result> inlineResult, Api::SendOptions options, bool needsCaption) {
  int document = 1, storageItem = 0; auto item = &storageItem; auto messageSendingFrom = [] { return 123; };
CONFIRM_HERE
  if(saved) {
SAVED_HERE
  } else {
INLINE_HERE
  }
 }
};
int main() {
 Main::Session session; PeerData peer{&session};
 for(bool ghost : {false, true}) for(bool explicitSilent : {false, true}) for(bool ignore : {false, true}) for(bool broadcast : {false, true}) for(bool support : {false, true}) {
  session.ghost.silent = ghost; session.support = support; session.setting.supportSilent = support; peer.broadcast = broadcast; peer.owned.silent = broadcast;
  Api::SendOptions o{ .silent = explicitSilent, .ignoreGhostSilent = ignore };
  assert(ShouldSendSilent(&peer, o) == (explicitSilent || (ghost && !ignore) || broadcast || support));
 }
 session = {}; session.ghost.silent = true; peer.broadcast = false; peer.owned.silent = false;
 int label = 0; auto audible = menuOptions(&peer, true, true, label); assert(label == 1 && !audible.silent && audible.ignoreGhostSilent && !ShouldSendSilent(&peer, audible));
 peer.broadcast = true; peer.owned.silent = true; auto restricted = menuOptions(&peer, true, true, label); assert(label == 2 && restricted.silent && !restricted.ignoreGhostSilent);
 peer.broadcast = false; peer.owned.silent = false; session.support = true; session.setting.supportSilent = true; menuOptions(&peer, true, true, label); assert(label == 2);
 session.support = false; menuOptions(&peer, false, false, label); assert(label == 2);
 auto baseSilent = Api::SendOptions{ .silent = true }; auto mergedMusic = mergeMusic(baseSilent, audible); auto mergedSchedule = mergeSchedule(baseSilent, audible);
 assert(!mergedMusic.silent && mergedMusic.ignoreGhostSilent && !mergedSchedule.silent && mergedSchedule.ignoreGhostSilent);
 auto explicitSilence = Api::SendOptions{ .silent = true }; assert(mergeMusic(audible, explicitSilence).silent && !mergeMusic(audible, explicitSilence).ignoreGhostSilent);
 assert(mergeSchedule(audible, explicitSilence).silent && !mergeSchedule(audible, explicitSilence).ignoreGhostSilent);
 session.ghost.schedule = true;
 for(bool proxy : {false, true}) for(auto date : {0ll, 9000ll, 0x7ffffffeLL}) for(bool media : {false, true}) {
  Core::app.setting.proxyValue.enabled = proxy;
  Action action; action.options.scheduled = date; action.options.scheduleRepeatPeriod = 3600;
  auto message = prepare(action, media, Data::ForwardOptions::PreserveInfo); applyGhostScheduling(&session, message.action.options, 10);
  assert(message.action.options.scheduled == (date ? date : proxy ? 1012 : 1010));
  assert(message.action.options.scheduleRepeatPeriod == 3600 && message.action.replaceMediaOf == 0);
 }
 HistoryItem a{1}, b{2}, c{3}, missing{4}; View va{&a}, vb{&b}, vc{&c}; Delegate delegate; ListWidget list{{&va, &vb, &vc}, &delegate};
 assert(list.collectBetween(&a, &c, INT_MAX).size() == 3); assert(list.collectBetween(&a, &c, 2).empty()); assert(list.collectBetween(&a, &b, 2).size() == 2);
 assert(list.collectBetween(&a, &b, -1).empty() && list.collectBetween(&c, &a, INT_MAX).empty() && list.collectBetween(&a, &missing, INT_MAX).empty());
 b.selectable = false; assert(list.collectBetween(&a, &c, 2).size() == 2);
 for(int kind = 0; kind < 5; ++kind) { Media media{kind}; dispatched = 0; repeat(kind ? &media : nullptr); assert(dispatched == (kind <= 2 ? 1 : kind == 3 ? 2 : 3)); }
 for(bool saved : {false, true}) for(bool enabled : {false, true}) for(bool caption : {false, true}) for(bool picker : {false, true}) {
  LuxurySettings::gifs.enabled = enabled; Gifs gifs; gifs._requireConfirmation = picker; auto result = std::make_shared<Result>(); auto weak = std::weak_ptr<Result>(result); Api::SendOptions o; o.scheduled = 9000; o.silent = true;
  gifs.choose(saved, result, o, caption); result.reset(); auto &stream = saved ? gifs._fileChosen : gifs._inlineResultChosen;
  if(enabled && !caption && picker) { assert(stream.sent.empty() && gifs.show.pending.size() == 1); gifs._searchBot = 99; gifs.show.pending.back()(); }
  assert(stream.sent.size() == 1 && gifs.show.pending.size() == size_t(enabled && !caption && picker) && stream.sent[0].options.scheduled == 9000 && stream.sent[0].options.silent);
  if(!saved) assert(stream.sent[0].bot == 7 && stream.sent[0].options.hideViaBot && !weak.expired());
 }
 LuxurySettings::gifs.enabled = true; Gifs destroyed; destroyed.choose(false, std::make_shared<Result>(), {}, false); destroyed.alive.reset(); destroyed.show.pending.back()(); assert(destroyed._inlineResultChosen.sent.empty());
 Gifs notReady; auto result = std::make_shared<Result>(); result->ready = false; notReady.choose(false, result, {}, false); assert(notReady.show.pending.empty());
 std::cout << "PASS: M1-M5 actual options/helper/menu/selection/confirmation/repeat slices, synthetic peer/UI/send\n";
}
'''
for marker, value in {'OPTIONS_HERE': options, 'MUSIC_MERGE_HERE': merge, 'SCHEDULE_MERGE_HERE': schedule_merge, 'SILENT_HERE': silent, 'SCHEDULE_HERE': schedule, 'PREP_HERE': prep, 'COLLECT_HERE': collect, 'MENU_HERE': menu_block, 'REPEAT_HERE': repeat, 'CONFIRM_HERE': confirm, 'SAVED_HERE': saved, 'INLINE_HERE': inline}.items(): cpp = cpp.replace(marker, value)
parser = argparse.ArgumentParser()
parser.add_argument('--cxx', default=os.environ.get('CXX') or shutil.which('g++') or r'C:\Strawberry\c\bin\g++.exe')
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix='luxury-intent-') as directory:
    work = Path(directory); test = work / 'intent.cpp'; test.write_text(cpp, encoding='utf-8')
    exe = work / ('intent.exe' if os.name == 'nt' else 'intent')
    subprocess.run([args.cxx, '-std=c++20', '-O0', '-D_DEBUG', '-I', str(root / 'Telegram/ThirdParty/GSL/include'), str(test), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
