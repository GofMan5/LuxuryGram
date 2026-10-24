"""Run actual ShotState + production subscriptions with real rpl/GSL on doubles.
python tools/tests/message_shot_lifetime.py [--cxx g++]
HistoryItem/Element/Qt painting are synthetic; not native ASan/UI verification.
"""
from pathlib import Path
import argparse
import os
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'Telegram/SourceFiles/luxury/features/message_shot/message_shot.cpp').read_text('utf-8')
state = source[source.index('\tstruct ShotState {'):source.index('\n\tconst auto state = std::make_shared<ShotState>();')]
subscriptions = source[source.index('\towner.itemRemoved()'):source.index('\n\n\tif (!preload->documents.empty()', source.index('\towner.itemRemoved()'))]
restore = source[source.index('\t\t\tconst auto takingShotGuard'):source.index('\n\n\t\t\t// calculate the size')]
assert source.count('callback(result, final);') == 1
assert '\n\t\t}\n\t\tcallback(result, final);' in source
assert 'auto preview = base::take(state->render);' in source
assert 'state->render = std::move(preview);' in source
assert 'preview(false);' in source and 'if (state->finished)' in source
assert 'QObject::connect(box, &QObject::destroyed, cancel);' in source
assert 'base::call_delayed(' in source and '[state] { state->finish(); }' in source
assert source.index('base::call_delayed(', source.index('state->render = std::move(preview);')) < source.index('rpl::single()', source.index('state->render = std::move(preview);'))
preview = source[source.index('\t\tauto preview = base::take(state->render);'):source.index('\n\t\tbase::call_delayed(', source.index('\t\tauto preview = base::take(state->render);'))]
assert source.index('owner.itemRemoved()') < source.index('preview(false);')
session = (root / 'Telegram/SourceFiles/data/data_session.cpp').read_text('utf-8')
history = (root / 'Telegram/SourceFiles/history/history.cpp').read_text('utf-8')
clear = session[session.index('void Session::clear()'):session.index('void Session::clear()') + 6000]
assert clear.index('_sessionDataAboutToBeCleared.fire({});') < clear.index('_histories')
destroy = history[history.index('void History::destroyMessage('):history.index('void History::destroyMessage(') + 4000]
assert destroy.index('owner().unregisterMessage(item);') < destroy.index('_items.erase(i);')
unregister = session[session.index('void Session::unregisterMessage('):session.index('\nMsgId Session::nextLocalMessageId(')]
assert unregister.index('_itemRemoved.fire_copy(item);') < unregister.index('messagesListForInsert(peerId)->erase(itemId);')
for marker in ('void History::destroyMessagesByDates(', 'void History::destroyMessagesByTopic(', 'void History::destroyMessagesBySublist('):
    batch = history[history.index(marker):history.index(marker) + 1200]
    assert batch.index('owner().notifyItemsAboutToBeDestroyed(toDestroy);') < batch.index('item->destroy();')
cpp = r'''
#include <algorithm>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>
#include <gsl/gsl>
template <typename... Args> constexpr bool false_t(Args&&...) { return false; }
#include <rpl/event_stream.h>
#include <rpl/producer.h>
namespace base::assertion { void log(const char*, const char*, int) { std::abort(); } }
namespace base { template <typename Type> constexpr Type take(Type &value) noexcept { return std::exchange(value, Type{}); } }
namespace ranges { template<class C, class V> bool contains(const C &c, const V &v) { return std::find(c.begin(), c.end(), v) != c.end(); } }
using gsl::not_null;
template<class T> using Fn = std::function<T>;
struct Delegate { bool alive = true; };
struct HistoryMessageVia {};
struct HistoryMessageForwarded { bool real = false; void create(HistoryMessageVia*, void*) { real = true; } };
struct HistoryItem {
 int id = 0; bool alive = true; HistoryMessageForwarded forwarded;
 int fullId() const { return id; }
 template<class T> T *Get() { if constexpr (std::is_same_v<T, HistoryMessageForwarded>) return &forwarded; else return nullptr; }
};
int destroyed = 0;
namespace HistoryView { struct Element {
 HistoryItem *item; std::shared_ptr<Delegate> delegate;
 Element(HistoryItem *i, std::shared_ptr<Delegate> d) : item(i), delegate(d) {}
 ~Element() { if (!item->alive || !delegate->alive) std::abort(); ++destroyed; }
}; }
using Views = std::unordered_map<not_null<HistoryItem*>, std::shared_ptr<HistoryView::Element>>;
STATE_HERE
struct Owner {
 rpl::event_stream<not_null<const HistoryItem*>> removed;
 rpl::event_stream<std::vector<not_null<HistoryItem*>>> bulk;
 rpl::event_stream<> clear;
 auto itemRemoved() { return removed.events(); }
 auto itemsAboutToBeDestroyed() { return bulk.events(); }
 auto sessionDataAboutToBeCleared() { return clear.events(); }
};
void require(bool value, const char *label) { if (!value) throw std::runtime_error(label); }
void wire(Owner &owner, const std::shared_ptr<ShotState> &state, std::vector<int> messageIds) {
 const auto weakState = std::weak_ptr<ShotState>(state);
 const auto cancel = [weakState] { if (const auto state = weakState.lock()) state->cancel(); };
SUBSCRIPTIONS_HERE
}
void restoreBeforeCallback(HistoryItem *item, Fn<void()> callback) {
 bool takingShot = true;
 bool anonymous = true;
 auto messages = std::vector<not_null<HistoryItem*>>{item};
 {
RESTORE_HERE
 }
 require(!takingShot && item->forwarded.real, "anonymous state restored before external callback");
 callback();
}
void previewNow(const std::shared_ptr<ShotState> &state) {
PREVIEW_HERE
}
int main() {
 for (int scenario = 0; scenario != 9; ++scenario) {
  Owner owner;
  HistoryItem item{1}, other{2};
  auto delegate = std::make_shared<Delegate>();
  auto state = std::make_shared<ShotState>();
  state->views = std::make_shared<Views>();
  state->views->emplace(&item, std::make_shared<HistoryView::Element>(&item, delegate));
  const auto before = destroyed;
  int calls = 0;
  state->render = [views = state->views, delegate, &calls](bool) { ++calls; };
  wire(owner, state, {1});
  if (scenario == 0) owner.removed.fire_copy(&item);
  if (scenario == 1) owner.bulk.fire_copy({&item, &other});
  if (scenario == 2) owner.clear.fire({});
  if (scenario == 3) state->cancel(); // Qt box destruction invokes same cancel.
  if (scenario == 4) {
   owner.removed.fire_copy(&other);
   owner.bulk.fire_copy({&other});
   require(!state->finished && destroyed == before, "unrelated deletion keeps selected view");
   state->finish();
   require(calls == 1, "unrelated deletion preserves final render");
  }
  if (scenario == 5) {
   state->render = [stateWeak = std::weak_ptr<ShotState>(state), &owner, &item, &calls](bool) {
    ++calls; owner.removed.fire_copy(&item); item.alive = false;
    if (auto state = stateWeak.lock()) require(state->views->empty(), "reentrant delete releases views");
   };
   state->finish();
  }
  if (scenario == 6) {
   previewNow(state);
   owner.clear.fire({});
   item.alive = false;
   require(calls == 1, "preview closure may survive synchronous cancellation");
  }
  if (scenario == 7) state->finish();
  if (scenario == 8) {
   state.reset();
   require(destroyed == before + 1, "state destruction releases registered views");
   item.alive = false; continue;
  }
  require(destroyed == before + 1 && state->views->empty(), "selected view destroyed before item free");
  item.alive = false;
  const auto previousCalls = calls;
  state->finish(); state->cancel(); state->finish();
  require(calls == previousCalls && destroyed == before + 1, "late timeout/completion idempotent");
  delegate->alive = false;
 }
 {
  auto state = std::make_shared<ShotState>();
  state->views = std::make_shared<Views>();
  int calls = 0;
  state->render = [attempt = 0, &calls](bool final) mutable { ++attempt; require(attempt == (final ? 2 : 1), "preview retains mutable state for final"); ++calls; };
  previewNow(state); state->finish(); require(calls == 2, "preview and final each called once");
 }
 HistoryItem item{7};
 restoreBeforeCallback(&item, [&] { require(item.forwarded.real, "callback observes real header"); item.alive = false; });
 std::cout << "PASS11: actual ShotState/subscriptions/preview move, real rpl/GSL, synthetic items/views; no native Qt/ASan\n";
}
'''
cpp = cpp.replace('STATE_HERE', state).replace('SUBSCRIPTIONS_HERE', subscriptions).replace('RESTORE_HERE', restore).replace('PREVIEW_HERE', preview)
parser = argparse.ArgumentParser()
parser.add_argument('--cxx', default=os.environ.get('CXX') or shutil.which('g++') or r'C:\Strawberry\c\bin\g++.exe')
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix='luxury-shot-') as directory:
    work = Path(directory)
    test = work / 'shot.cpp'
    test.write_text(cpp, encoding='utf-8')
    exe = work / ('shot.exe' if os.name == 'nt' else 'shot')
    subprocess.run([args.cxx, '-std=c++20', '-O0', '-D_DEBUG', '-I', str(root / 'Telegram/lib_rpl'), '-I', str(root / 'Telegram/lib_base'), '-I', str(root / 'Telegram/ThirdParty/GSL/include'), str(test), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
