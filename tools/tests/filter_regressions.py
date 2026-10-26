"""Compile actual filter evaluation/ownership and Groups/cache invalidation slices.
python tools/tests/filter_regressions.py [--cxx g++]
Qt/ICU/session doubles; F3 additionally checks common pre-apply source control flow.
Not native ICU pattern semantics or Qt message edit/render verification.
"""
from pathlib import Path
import argparse
import os
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
def read(path):
    return (root / 'Telegram/SourceFiles' / path).read_text('utf-8-sig')
controller = read('luxury/features/filters/filters_controller.cpp')
utils = read('luxury/features/filters/filters_utils.cpp')
groups = read('data/data_groups.cpp')
cache = read('luxury/features/filters/filters_cache_controller.cpp')
session = read('data/data_session.cpp')
evaluate = controller[controller.index('std::optional<bool> isFiltered('):controller.rindex('} // namespace', 0, controller.index('bool isBlocked('))]
compile_pattern = utils[utils.index('bool CompilesPattern('):utils.index('enum class PeerResolveHintType')]
group_functions = groups[groups.index('Groups::Groups('):groups.index('not_null<HistoryItem*> Groups::findItemToEdit')]
invalidate = cache[cache.index('void invalidate(not_null<HistoryItem*> item)'):cache.index('void invalidateSession(')]
filtered = controller[controller.index('bool filtered('):controller.index('std::optional<bool> filteredMessagesShown(')]
assert '!settings.filtersEnabled() || item->out()' in filtered
assert 'shownFor(' in filtered and 'filterBlocked(item)' in filtered
assert 'isEnabled(' not in filtered
assert 'settings.filtersEnabledInChats() || item->history()->peer->isBroadcast()' in filtered
edit = session[session.index('void Session::updateEditedMessage('):session.index('void Session::processMessages(')]
assert edit.count('FiltersController::invalidate(existing);') == 1
assert edit.index('LuxuryMessages::addEditedMessage(') < edit.index('FiltersController::invalidate(existing);') < edit.index('existing->isLocalUpdateMedia()') < edit.index('existing->applyEdition(')
assert 'goto ' not in edit
cpp = r'''
#include <algorithm>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>
using UChar = char16_t;
using UErrorCode = int;
constexpr auto U_ZERO_ERROR = 0, U_REGEX_TIME_OUT = 1, U_REGEX_STACK_OVERFLOW = 2;
constexpr auto UREGEX_MULTILINE = 1, UREGEX_CASE_INSENSITIVE = 2;
bool U_SUCCESS(int status) { return status <= 0; }
bool U_FAILURE(int status) { return status > 0; }
#define LOG(...) do {} while (false)
struct QString {
 std::u16string text;
 QString(const char16_t *value = u"") : text(value) {}
 bool isEmpty() const { return text.empty(); }
 const UChar *constData() const { return text.data(); }
 int length() const { return text.size(); }
 std::string toStdString() const { return std::string(text.begin(), text.end()); }
};
struct QElapsedTimer { void start() {} int elapsed() const { return 0; } bool hasExpired(int) const { return false; } };
int patternsAlive = 0, compileStatus = 0;
bool compileReturnsNull = false;
namespace icu {
struct UnicodeString {
 std::string text;
 UnicodeString(const UChar *value, int size) { for(int i = 0; i != size; ++i) text.push_back(char(value[i])); }
 static UnicodeString fromUTF8(const std::string &text) { UnicodeString out(nullptr, 0); out.text = text; return out; }
};
struct RegexMatcher {
 bool matched;
 void setTimeLimit(int, int&) {} void setStackLimit(int, int&) {}
 bool find(int&) { return matched; }
};
struct RegexPattern {
 std::string text;
 RegexPattern(std::string value) : text(std::move(value)) { ++patternsAlive; }
 ~RegexPattern() { --patternsAlive; }
 RegexMatcher *matcher(const UnicodeString &str, int&) { return new RegexMatcher{str.text.find(text) != std::string::npos}; }
 static RegexPattern *compile(const UnicodeString &str, int, int &status) {
  status = compileStatus; return compileReturnsNull ? nullptr : new RegexPattern(str.text);
 }
};
}
struct ReversiblePattern { std::shared_ptr<icu::RegexPattern> pattern; bool reversed = false; };
struct HashablePattern { int id; ReversiblePattern pattern; bool operator<(const HashablePattern &other) const { return id < other.id; } };
namespace FiltersCacheController { struct Cache {
 std::vector<HashablePattern> sharedPatterns;
 std::unordered_map<long long, std::vector<ReversiblePattern>> patternsByDialogId;
 std::unordered_map<long long, std::set<HashablePattern>> exclusionsByDialogId;
}; }
constexpr auto kRegexEvaluationBudgetMs = 25, kRegexStackLimitBytes = 1024 * 1024;
EVALUATE_HERE
COMPILE_HERE
template<class T> using not_null = T;
using MessageGroupId = int;
struct HistoryItem;
using HistoryItemsList = std::vector<HistoryItem*>;
namespace Data {
class Session;
struct Group { HistoryItemsList items; };
class Groups {
public:
 Groups(Session *data);
 bool isGrouped(const HistoryItem *item) const;
 bool isGroupOfOne(const HistoryItem *item) const;
 void registerMessage(HistoryItem *item);
 void unregisterMessage(const HistoryItem *item);
 void refreshMessage(HistoryItem *item, bool justRefreshViews = false);
 const Group *find(const HistoryItem *item) const;
private:
 HistoryItemsList::const_iterator findPositionForItem(const HistoryItemsList &group, HistoryItem *item);
 void refreshViews(const HistoryItemsList &items);
 Session *_data;
 std::map<MessageGroupId, Group> _groups;
};
class Session {
public:
 Groups group{this}; int refreshes = 0;
 Groups &groups() { return group; }
 void requestItemViewRefresh(HistoryItem*) { ++refreshes; }
};
}
struct History { Data::Session *session; Data::Session &owner() { return *session; } };
struct Media { bool canBeGrouped() const { return true; } };
struct HistoryItem {
 int id; int group = 1; History *context; Media part;
 int groupId() const { return group; }
 const Media *media() const { return &part; }
 History *history() { return context; }
 bool isRegular() const { return true; } bool isScheduled() const { return false; } bool isUploading() const { return false; }
 void invalidateChatListEntry() {}
};
std::map<int, bool> verdicts;
namespace ranges {
 template<class C, class V> bool contains(const C &c, const V &v) { return std::find(c.begin(), c.end(), v) != c.end(); }
 template<class C, class V> auto remove(C &c, const V &v) { return std::remove(c.begin(), c.end(), v); }
 template<class C, class V> auto find(C &c, const V &v) { return std::find(c.begin(), c.end(), v); }
}
#define Unexpected(...) throw std::runtime_error("unexpected")
namespace FiltersCacheController {
 std::mutex filteredMessagesMutex;
 void invalidateSingle(HistoryItem *item) { verdicts.erase(item->id); }
INVALIDATE_HERE
}
namespace FiltersController { void invalidate(HistoryItem *item) { FiltersCacheController::invalidate(item); } }
namespace Data { constexpr auto kMaxItemsInGroup = 10;
GROUPS_HERE
}
void require(bool condition, const char *label) { if (!condition) throw std::runtime_error(label); }
int main() {
 {
  auto cache = std::make_shared<FiltersCacheController::Cache>();
  const auto scoped = ReversiblePattern{std::make_shared<icu::RegexPattern>("scoped")};
  const auto shared = HashablePattern{1, {std::make_shared<icu::RegexPattern>("shared")}};
  cache->patternsByDialogId[7] = {scoped}; cache->sharedPatterns = {shared};
  require(isFiltered(QString(u"scoped"), 7, cache, false) == true, "chat-specific rules survive shared-off");
  require(isFiltered(QString(u"shared"), 7, cache, false) == false, "shared-off excludes shared rules");
  require(isFiltered(QString(u"shared"), 7, cache, true) == true, "broadcast/shared-on match");
  require(isFiltered(QString(u"scoped"), 8, cache, false) == false, "scoped peer isolation");
  require(!isFiltered(QString(), 7, cache, true), "empty input remains uncached");
  cache->exclusionsByDialogId[7].insert(shared);
  require(isFiltered(QString(u"shared"), 7, cache, true) == false, "shared exclusions retained");
  cache->patternsByDialogId[7][0].reversed = true;
  require(isFiltered(QString(u"other"), 7, cache, false) == true, "reversed scoped rule retained");
 }
 require(!patternsAlive, "evaluation fixture ownership");
 for(int i = 0; i != 100; ++i) { compileStatus = 0; require(CompilesPattern(QString(u"ok"), false), "valid regex"); require(!patternsAlive, "repeat import temporary released"); }
 compileStatus = 3; require(!CompilesPattern(QString(u"bad"), true) && !patternsAlive, "ICU error rejects allocated pointer safely");
 compileStatus = -1; require(CompilesPattern(QString(u"warning"), true) && !patternsAlive, "ICU success warning accepted");
 compileStatus = 0; compileReturnsNull = true; require(!CompilesPattern(QString(u"null"), false) && !patternsAlive, "null compile rejected");
 for(int count : {1, 2, 3}) {
  Data::Session data; History history{&data};
  HistoryItem a{1, 1, &history}, b{2, 1, &history}, c{3, 1, &history};
  auto items = std::vector<HistoryItem*>{&a, &b, &c};
  for(int i = 0; i != count; ++i) data.group.registerMessage(items[i]);
  verdicts.clear(); for(int i = 0; i != count; ++i) verdicts[items[i]->id] = true;
  HistoryItem other{9, 1, &history}; data.group.unregisterMessage(&other);
  require(verdicts.size() == count, "unrelated removal no cache changes");
  data.group.unregisterMessage(&a);
  require(verdicts.empty(), "old album members invalidated before detach including 2-to-1");
  if(count == 2) require(data.group.isGroupOfOne(&b), "remaining member is singleton");
 }
 {
  Data::Session data; History history{&data}; HistoryItem a{1, 1, &history}, b{2, 1, &history};
  data.group.registerMessage(&a); verdicts = {{1, false}, {2, true}};
  data.group.registerMessage(&b); require(verdicts.empty(), "join invalidates old singleton and joining cached item");
  verdicts = {{1, true}, {2, true}}; data.group.refreshMessage(&a, true);
  require(verdicts.empty(), "metadata/view refresh invalidates album");
  verdicts = {{1, true}, {2, true}}; require(data.group.find(&a) != nullptr && verdicts.size() == 2, "retained saved-deleted item has no detach mutation");
 }
 std::cout << "PASS: actual F1/F2/F4 production slices, 100 repeated compiles; F3 common pre-apply source gate. Native Qt/ICU pending\n";
}
'''
cpp = cpp.replace('EVALUATE_HERE', evaluate).replace('COMPILE_HERE', compile_pattern).replace('GROUPS_HERE', group_functions).replace('INVALIDATE_HERE', invalidate)
parser = argparse.ArgumentParser()
parser.add_argument('--cxx', default=os.environ.get('CXX') or shutil.which('g++') or r'C:\Strawberry\c\bin\g++.exe')
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix='luxury-filters-') as directory:
    work = Path(directory); test = work / 'filters.cpp'; test.write_text(cpp, encoding='utf-8')
    exe = work / ('filters.exe' if os.name == 'nt' else 'filters')
    subprocess.run([args.cxx, '-std=c++20', '-O0', '-D_DEBUG', str(test), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
