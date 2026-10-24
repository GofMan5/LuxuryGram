"""Run Watcher production filename/clear/retry/photo/lookup slices on explicit doubles.
python tools/tests/watched_media_regressions.py [--cxx g++]
Deterministic filesystem and loader doubles, real mutex/atomic; no profile/Qt I/O.
"""
from pathlib import Path
import argparse
import os
import shutil
import subprocess
import tempfile
root = Path(__file__).resolve().parents[2]
def read(path): return (root / 'Telegram/SourceFiles' / path).read_text('utf-8')
source = read('luxury/features/watch/watched_media.cpp')
state = source[source.index('struct MediaScope {'):source.index('[[nodiscard]] QString SafeSuffix(')]
filename = source[source.index('[[nodiscard]] QString FileName('):source.index('// The folder remains')]
photo = source[source.index('void SavePhotoAsync('):source.index('void DropAwaitingPhotos(')]
retry = source[source.index('[[nodiscard]] bool MoveToKept('):source.index('void SubscribeToDownloads(')]
clear = source[source.index('void ClearMedia('):source.index('\n} // namespace\n\nvoid clearKeptMedia')]
keep = source[source.index('QString keepMediaForDeleted('):source.index('QString keptFileForMessage(')]
lookup = source[source.index('QString keptFileForMessage('):source.index('bool ownsFetchedPath(')]
helpers = read('luxury/utils/telegram_helpers.cpp')
resolver = helpers[helpers.index('Main::Session *getSessionByUniqueId('):helpers.index('Main::Session *getSession(ID')]
logout = source[source.index('\t\tDropAwaitingPhotos(id);', source.index('sessionDataAboutToBeCleared()')):source.index('\n\t}, session->lifetime());', source.index('sessionDataAboutToBeCleared()'))]
drop = source[source.index('void DropAwaitingPhotos('):source.index('[[nodiscard]] bool MoveToKept(')]
weak_header = (root / 'Telegram/lib_base/base/weak_ptr.h').read_text('utf-8').replace('#include "base/basic_types.h"', '#include <gsl/gsl>')
photo_data = read('data/data_photo.cpp')
photo_cancel = photo_data[photo_data.index('Fn<void()> PhotoData::cancelLoadCallback('):photo_data.index('void PhotoData::clearLocalCache()')]
history = read('luxury/ui/message_history/history_inner.cpp')
assert 'view->data()->id);' not in history[history.index('// Media a watched chat'):history.index('if (_menu->empty())', history.index('// Media a watched chat'))]
assert '_sourceMessageIds.find(view->data())->second' in history and '_sourceMessageIds.emplace(item->data(), realId);' in history
assert '0, MsgId(message.messageId)' in read('luxury/ui/message_history/history_item.cpp')
assert source.count('getSession(') == 0
assert 'sessionDataAboutToBeCleared()' in source
assert '{ u"v2_*"_q }' in source
cpp = r'''
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <compare>
#include <type_traits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include <base/flat_map.h>
#include <gsl/util>
WEAK_HEADER_HERE
using uint64 = uint64_t; using ID = long long; using int64 = int64_t;
template<class T> using not_null = T;
template<class T> using Fn = std::function<T>;
struct QString : std::string {
 using std::string::string; QString(const std::string &s) : std::string(s) {}
 bool isEmpty() const { return empty(); } bool contains(char ch) const { return find(ch) != npos; }
 bool startsWith(char ch) const { return !empty() && front() == ch; }
 template<class T> QString arg(const T &value) const { QString r = *this; auto pos = r.find('%'); if(pos != npos) { auto end = pos + 1; while(end < r.size() && r[end] >= '0' && r[end] <= '9') ++end; std::string v; if constexpr(std::is_convertible_v<T,std::string>) v = value; else v = std::to_string(value); r.replace(pos, end-pos, v); } return r; }
};
QString operator""_q(const char16_t *text, size_t size) { QString r; for(size_t i = 0; i < size; ++i) r.push_back(char(text[i])); return r; }
QString operator+(const QString &a, const QString &b) { return QString(std::string(a) + std::string(b)); }
QString operator+(const QString &a, const char *b) { return QString(std::string(a) + b); }
QString operator+(const QString &a, char b) { return QString(std::string(a) + b); }
using QStringList = std::vector<QString>;
struct MsgId { int64 bare = 0; };
struct QUuid { static inline int count = 0; int value; enum { WithoutBraces }; static QUuid createUuid() { return {++count}; } QString toString(int) const { return std::to_string(value); } };
namespace base { template<class T> struct flat_set : std::set<T> { using std::set<T>::set; bool remove(const T &value) { return this->erase(value) != 0; } }; template<class T> T take(T &value) { return std::exchange(value,T{}); } }
namespace crl { using time = int64; inline time clock = 0; time now() { return clock; } std::vector<std::function<void()>> work, main; template<class F> void async(F f) { work.push_back(std::move(f)); } template<class F> void on_main(F f) { main.push_back(std::move(f)); } }
namespace base { inline std::vector<std::function<void()>> delayed; template<class F> void call_delayed(crl::time, F f) { delayed.push_back(std::move(f)); } }
namespace ranges { template<class C,class P> bool any_of(const C &c,P p) { return std::any_of(c.begin(),c.end(),p); } template<class C,class V,class P> bool contains(const C &c,const V &v,P p) { return any_of(c,[&](const auto &item) { return std::invoke(p,item) == v; }); } }
void drain(std::vector<std::function<void()>> &queue) { auto calls = std::exchange(queue,{}); for(auto &f : calls) f(); }
#define LOG(x) do {} while(0)
constexpr auto kMaxPendingKeeps = 256;
constexpr auto kKeepWaitTimeout = crl::time(30 * 60 * 1000);
#define DEBUG_LOG(x) do {} while(0)
struct QIODevice { enum { WriteOnly }; };
std::map<QString, std::string> files;
std::set<QString> held;
struct QFile {
 QString path; explicit QFile(QString p) : path(std::move(p)) {}
 bool open(int) { files[path] = {}; return true; } size_t write(const std::string &data) { files[path] = data; return data.size(); }
 static bool exists(const QString &p) { return files.contains(p); }
 static bool remove(const QString &p) { return !held.contains(p) && files.erase(p); }
 static bool rename(const QString &a,const QString &b) { if(!files.contains(a) || files.contains(b) || held.contains(a)) return false; files[b] = files[a]; files.erase(a); return true; }
};
struct QImage { bool null = false; bool isNull() const { return null; } bool save(const QString &p,const char*) const { files[p] = "frame"; return true; } };
struct QFileInfo { QString path; QString absoluteFilePath() const { return path; } };
struct QDir {
 enum { Files = 1 }; QString path; explicit QDir(QString p = {}) : path(std::move(p)) {} bool mkpath(QString) { return true; }
 QStringList entryList(const QStringList &patterns,int) const { QStringList result; for(const auto &[file,data] : files) { if(file.rfind(path,0) != 0) continue; auto name = QString(file.substr(path.size())); for(const auto &pattern : patterns) { auto prefix = pattern.substr(0,pattern.find('*')); if(name.rfind(prefix,0) == 0) { result.push_back(name); break; } } } return result; }
 std::vector<QFileInfo> entryInfoList(const QStringList &patterns,int flags) const { std::vector<QFileInfo> result; for(auto name : entryList(patterns,flags)) result.push_back({path + name}); return result; }
};
QString cWorkingDir() { return "/sandbox/"; }
int cancelledPhotos = 0;
namespace Data {
 enum class PhotoSize { Large };
 struct Bytes : std::string { using std::string::string; bool isEmpty() const { return empty(); } };
 struct PhotoOwner { int cancellations = 0; void cancel() { ++cancellations; ++cancelledPhotos; } };
 struct PhotoMedia { PhotoOwner owned; PhotoOwner *owner() { return &owned; } bool ready = true; Bytes bytes{"photo"}; bool loaded() const { return ready; } Bytes videoContent(PhotoSize) { return {}; } Bytes imageBytes(PhotoSize) { return bytes; } struct Image { QImage original() const { return {}; } } value; Image *image(PhotoSize) { return &value; } };
}
struct FakeLoader : base::has_weak_ptr { int &count; explicit FakeLoader(int &v) : count(v) {} void cancel() { ++count; } };
using PhotoSize = Data::PhotoSize;
struct PhotoData { struct File { std::unique_ptr<FakeLoader> loader; }; std::array<File,1> _images; int validSizeIndex(PhotoSize) const { return 0; } Fn<void()> cancelLoadCallback(PhotoSize) const; };
PHOTO_CANCEL_HERE
struct Document { QString path; int cancellations = 0; std::function<void()> onCancel; QString loadingFilePath() const { return path; } void cancel() { ++cancellations; held.erase(path); path = {}; if(onCancel) onCancel(); } };
struct DataOwner { std::map<uint64, Document> documents; Document *document(uint64 id) { return &documents[id]; } };
namespace Main { struct Session { uint64 unique = 0, bare = 0; DataOwner owner; uint64 uniqueId() const { return unique; } struct User { uint64 bare; }; User userId() const { return {bare}; } DataOwner &data() { return owner; } }; }
struct Account { Main::Session *owner; Main::Session *maybeSession() { return owner; } };
namespace Core { struct Domain { std::map<int,Account*> entries; auto &accounts() { return entries; } }; struct Application { Domain value; Domain &domain() { return value; } } app; Application &App() { return app; } }
RESOLVER_HERE
struct Peer { struct Value { ID value; } id; }; struct History { Main::Session *s; Peer *peer; Main::Session &session() { return *s; } }; struct HistoryItem { History *h; History *history() { return h; } };
ID getDialogIdFromPeer(Peer *peer) { return peer->id.value; }
constexpr auto kKeepRetryDelay = crl::time(30000), kGiveUpToastDelay = crl::time(60000);
namespace tr { constexpr int now = 0; int luxury_WatcherMediaSaveFailed(int,int,int count) { return count; } }
constexpr int lt_count = 0;
namespace Ui { struct Toast { static void Show(int) {} }; }
STATE_HERE
FILENAME_HERE
PHOTO_HERE
DROP_HERE
void logout(uint64 id) {
LOGOUT_HERE
}
RETRY_HERE
QString deletingName; QString NameForItem(HistoryItem*) { return deletingName; }
KEEP_HERE
LOOKUP_HERE
CLEAR_HERE
std::shared_ptr<MediaScope> scope(Main::Session &s,ID dialog) { Peer peer{{dialog}}; History history{&s,&peer}; HistoryItem item{&history}; return ScopeForItem(&item); }
int main() {
 int oldLoads = 0, newLoads = 0; PhotoData photoData; photoData._images[0].loader = std::make_unique<FakeLoader>(oldLoads); auto cancelOld = photoData.cancelLoadCallback(PhotoSize::Large); cancelOld(); assert(oldLoads == 1); photoData._images[0].loader = std::make_unique<FakeLoader>(newLoads); cancelOld(); assert(newLoads == 0); photoData.cancelLoadCallback(PhotoSize::Large)(); assert(newLoads == 1);
 base::flat_map<void*,int> sourceIds; int view = 0; sourceIds.emplace(&view,45); assert(sourceIds.contains(&view) && sourceIds.find(&view)->second == 45);
 Main::Session test{100 | 0x0100000000000000ULL,100}, a{100,100}, b{200,200}; Account at{&test}, aa{&a}, ab{&b}; Core::app.value.entries = {{0,&at},{1,&aa},{2,&ab}};
 assert(getSessionByUniqueId(100) == &a && getSessionByUniqueId(test.uniqueId()) == &test && !getSessionByUniqueId(999));
 auto chatScope = scope(a,ID((uint64(1) << 48) | 42)), channelScope = scope(a,ID((uint64(2) << 48) | 42));
 auto chatName = FileName(chatScope,chatScope->dialogId,{9},9,"jpg"), channelName = FileName(channelScope,channelScope->dialogId,{9},9,"jpg"); assert(chatName != channelName);
 files[KeptDir() + chatName] = "chat"; files[KeptDir() + channelName] = "channel"; ClearMedia(std::pair(a.uniqueId(),chatScope->dialogId)); drain(crl::work); drain(crl::main); assert(!files.contains(KeptDir() + chatName) && files.contains(KeptDir() + channelName));
 auto sa = scope(a,5), sb = scope(b,5); auto na = FileName(sa,5,{45},7,"jpg"), nb = FileName(sb,5,{45},7,"jpg");
 assert(na != nb && na.rfind("v2_100_5_45_7_",0) == 0 && nb.rfind("v2_200_5_45_7_",0) == 0);
 files[KeptDir() + na] = "A"; files[KeptDir() + nb] = "B"; files[KeptDir() + "5_45_7.jpg"] = "legacy";
 assert(keptFileForMessage(100,5,{45}) == KeptDir() + na && keptFileForMessage(200,5,{45}) == KeptDir() + nb && keptFileForMessage(100,5,{456}).isEmpty());
 auto pendingA = FileName(sa,5,{46},8,"bin"), pendingB = FileName(sb,5,{47},9,"bin"); files[PendingDir() + pendingA] = "partial"; files[PendingDir() + pendingB] = "other";
 a.owner.documents[8].path = PendingDir() + pendingA; b.owner.documents[9].path = PendingDir() + pendingB; held.insert(a.owner.documents[8].path); held.insert(b.owner.documents[9].path);
 AwaitingDocuments = {{100,8,a.owner.documents[8].path,sa},{200,9,b.owner.documents[9].path,sb}};
 PendingKeeps = {{pendingA,sa,999999},{pendingB,sb,999999}}; RetryPendingKeepsLater(); auto oldTimer = base::delayed.back();
 // A retry while loaders are writing must not move either file.
 held.clear(); FlushPendingKeeps(); assert(PendingKeeps.size() == 2 && files.contains(PendingDir() + pendingA)); held.insert(a.owner.documents[8].path); held.insert(b.owner.documents[9].path);
 auto photo = std::make_shared<Data::PhotoMedia>(); auto photoPath = PendingDir() + FileName(sa,5,{50},50,"jpg"); SavePhotoAsync(photo,photoPath,sa);
 auto ownedPhoto = std::make_shared<Data::PhotoMedia>(); 
 AwaitingPhotos = {{photoPath,sa,ownedPhoto,100,999999,[] { ++cancelledPhotos; }},{PendingDir() + "other-photo",sb,photo,200,999999,{}}};
 ownedPhoto.reset();
 QString newer; a.owner.documents[8].onCancel = [&] { assert(PendingKeeps.size() == 1 && !sa->active); auto fresh = scope(a,5); newer = FileName(fresh,5,{45},7,"jpg"); assert(newer != na); files[KeptDir() + newer] = "new"; };
 ClearMedia(std::pair(uint64(100),ID(5))); assert(a.owner.documents[8].cancellations == 1 && !b.owner.documents[9].cancellations && !sa->active && sb->active);
 assert(AwaitingPhotos.size() == 1 && AwaitingDocuments.size() == 1 && PendingKeeps.size() == 1 && keptFileForMessage(100,5,{45}) == KeptDir() + newer);
 assert(cancelledPhotos == 1 && !photo->owned.cancellations);
 auto generation = RetryGeneration; oldTimer(); assert(RetryGeneration == generation && PendingKeepsRetrying);
 drain(crl::work); drain(crl::main); assert(!files.contains(KeptDir() + na) && !files.contains(PendingDir() + pendingA) && !files.contains(photoPath));
 assert(files.contains(KeptDir() + nb) && files.contains(KeptDir() + newer) && files.contains(KeptDir() + "5_45_7.jpg"));
 ClearMedia(std::pair(uint64(100),ID(56))); drain(crl::work); drain(crl::main); assert(files.contains(KeptDir() + newer));
 ClearMedia(std::nullopt); drain(crl::work); drain(crl::main); assert(PendingKeeps.empty() && AwaitingDocuments.empty() && AwaitingPhotos.empty()); assert(!files.contains(KeptDir() + newer) && !files.contains(KeptDir() + nb) && files.contains(KeptDir() + "5_45_7.jpg"));
 auto fresh = scope(a,5); auto freshName = FileName(fresh,5,{99},99,"jpg"); SavePhotoAsync(photo,PendingDir() + freshName,fresh); drain(crl::work); assert(files.contains(PendingDir() + freshName));
 auto logoutScope = scope(a,56); auto completeName = FileName(logoutScope,56,{1},1,"jpg"); auto queuedPath = PendingDir() + FileName(logoutScope,56,{2},2,"jpg");
 files[PendingDir() + completeName] = "complete"; PendingKeeps.push_back({completeName,logoutScope,999999}); SavePhotoAsync(photo,queuedPath,logoutScope); logout(100); assert(logoutScope->active && !logoutScope->photoWritesAllowed);
 ClearMedia(std::nullopt); drain(crl::work); drain(crl::main); assert(!files.contains(queuedPath) && !files.contains(PendingDir() + completeName));
 auto sharedScopeA = scope(a,71), sharedScopeB = scope(a,72); auto sharedPhoto = std::make_shared<Data::PhotoMedia>();
 AwaitingPhotos = {{PendingDir() + "first",sharedScopeA,sharedPhoto,100,999999,[] { ++cancelledPhotos; }},{PendingDir() + "second",sharedScopeB,sharedPhoto,100,999999,{}}}; sharedPhoto.reset();
 ClearMedia(std::pair(uint64(100),ID(71))); assert(AwaitingPhotos.size() == 1 && AwaitingPhotos[0].cancelLoad && cancelledPhotos == 1);
 ClearMedia(std::pair(uint64(100),ID(72))); assert(AwaitingPhotos.empty() && cancelledPhotos == 2);
 auto userScope = scope(a,73); auto userPhoto = std::make_shared<Data::PhotoMedia>(); AwaitingPhotos = {{PendingDir() + "user-shared",userScope,userPhoto,100,999999,[] { ++cancelledPhotos; }}};
 ClearMedia(std::pair(uint64(100),ID(73))); assert(cancelledPhotos == 2);
 drain(crl::work); drain(crl::main);
 auto expiredScope = scope(a,74); auto expiredPhoto = std::make_shared<Data::PhotoMedia>(); expiredPhoto->ready = false;
 AwaitingPhotos = {{PendingDir() + "expired",expiredScope,expiredPhoto,100,1,[] { ++cancelledPhotos; }}}; expiredPhoto.reset(); crl::clock = 2; FlushAwaitingPhotos(); assert(AwaitingPhotos.empty() && cancelledPhotos == 3);
 auto lateScope = scope(a,81); auto lateName = FileName(lateScope,81,{2},2,"jpg"); auto latePhoto = std::make_shared<Data::PhotoMedia>();
 Peer latePeer{{81}}; History lateHistory{&a,&latePeer}; HistoryItem lateItem{&lateHistory}; deletingName = lateName;
 AwaitingPhotos.push_back({PendingDir() + lateName,lateScope,latePhoto,100,999999,{}}); assert(keepMediaForDeleted(&lateItem).isEmpty()); FlushPendingKeeps(); assert(PendingKeeps.size() == 1); FlushAwaitingPhotos(); FlushPendingKeeps(); assert(PendingKeeps.size() == 1 && PhotoWrites.contains(PendingDir() + lateName)); drain(crl::work); FlushPendingKeeps(); assert(PendingKeeps.size() == 1); drain(crl::main); assert(PendingKeeps.empty() && files.contains(KeptDir() + lateName));
 auto keepAfterLogout = scope(a,55); auto survivingName = FileName(keepAfterLogout,55,{1},1,"jpg"); files[PendingDir() + survivingName] = "complete"; PendingKeeps.push_back({survivingName,keepAfterLogout,999999}); logout(100); FlushPendingKeeps(); assert(files.contains(KeptDir() + survivingName));
 std::cout << "PASS: actual Watch account filenames/lookup/clear/retry/photo slices; original-ID source wiring; deterministic Qt/files/loader doubles\n";
}
'''
for marker,value in {'STATE_HERE':state, 'FILENAME_HERE':filename, 'PHOTO_HERE':photo, 'RETRY_HERE':retry, 'KEEP_HERE':keep, 'LOOKUP_HERE':lookup, 'CLEAR_HERE':clear, 'RESOLVER_HERE':resolver, 'LOGOUT_HERE':logout, 'DROP_HERE':drop, 'WEAK_HEADER_HERE':weak_header, 'PHOTO_CANCEL_HERE':photo_cancel}.items(): cpp = cpp.replace(marker,value)
parser = argparse.ArgumentParser()
parser.add_argument('--cxx', default=os.environ.get('CXX') or shutil.which('g++') or r'C:\Strawberry\c\bin\g++.exe')
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix='luxury-watch-') as directory:
    work = Path(directory); test = work / 'watch.cpp'; test.write_text(cpp, encoding='utf-8')
    exe = work / ('watch.exe' if os.name == 'nt' else 'watch')
    subprocess.run([args.cxx, '-std=c++20', '-O0', '-D_DEBUG', '-I', str(root / 'Telegram/lib_base'), '-I', str(root / 'Telegram/ThirdParty/GSL/include'), str(test), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
