"""Custom forwarding gives up on a failed media download at once instead of waiting out the timeout.

Compiles the real loadPhotoSync / loadDocumentSync (all four overloads),
WaitUntil and the file checks from luxury_sync.cpp, and the load-update
producers from telegram_helpers.cpp, with real rpl, base::weak_ptr and gsl. Session, photo/document, files, the main
thread and the clock are doubles: the clock only moves 250 ms per wait slice,
so a missed failure shows up as the full 5 or 15 minute timeout.
Run: python tools/tests/forward_media_failure_regressions.py
"""
import tempfile
from pathlib import Path

from library_regressions import CXX, ROOT, function, run, source

SYNC = 'Telegram/SourceFiles/luxury/features/forward/luxury_sync.cpp'
HELPERS = 'Telegram/SourceFiles/luxury/utils/telegram_helpers.cpp'

CPP = r'''
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <gsl/gsl>
using gsl::not_null;
template <typename... Args> constexpr bool false_t(Args&&...) { return false; }
#include <rpl/rpl.h>
WEAK_HEADER
namespace base::assertion { void log(const char*, const char*, int) { std::abort(); } }
template <class T> using Fn = std::function<T>;
using int64 = long long;
using qint64 = long long;
using PhotoId = unsigned long long;
using DocumentId = unsigned long long;
#define LOG(...) do {} while (0)

struct QString {
	std::string value;
	QString() = default;
	QString(const char *text) : value(text) {}
	bool isEmpty() const { return value.empty(); }
	void clear() { value.clear(); }
	bool operator==(const QString &other) const = default;
	QString arg(const QString &) const { return *this; }
};
std::map<std::string, qint64> files;
struct QFileInfo {
	QString path;
	explicit QFileInfo(const QString &path) : path(path) {}
	bool isFile() const { return files.contains(path.value); }
	qint64 size() const { return isFile() ? files[path.value] : 0; }
	QString absolutePath() const { return "dir"; }
	QString canonicalFilePath() const { return isFile() ? path : QString(); }
	QString absoluteFilePath() const { return path; }
};
struct QDir { bool mkpath(const QString &) const { return true; } };

namespace Test {
struct Clock {
	static inline std::chrono::steady_clock::time_point time;
	static auto now() { return time; }
};
std::deque<Fn<void()>> main, steps;
void tick(std::chrono::milliseconds slice) {
	while (!main.empty()) {
		auto f = std::move(main.front());
		main.pop_front();
		f();
	}
	if (!steps.empty()) {
		auto f = std::move(steps.front());
		steps.pop_front();
		f();
	}
	Clock::time += slice;
}
} // namespace Test

struct TimedCountDownLatch {
	static inline TimedCountDownLatch *current = nullptr;
	int count;
	explicit TimedCountDownLatch(int n) : count(n) { current = this; }
	~TimedCountDownLatch() { if (current == this) current = nullptr; }
	void countDown() { if (count) --count; }
	bool await(std::chrono::milliseconds slice) {
		if (count) Test::tick(slice);
		return !count;
	}
};
bool waiting() { return TimedCountDownLatch::current && TimedCountDownLatch::current->count; }

struct FullMsgId { long long peer = 0; long long msg = 0; };
enum FileStatus { FileDownloadFailed = -2, FileReady = 1 };
namespace Data {
enum class PhotoSize { Small, Thumbnail, Large };
struct FileOrigin { FileOrigin() = default; FileOrigin(FullMsgId) {} };
FileOrigin FileOriginMessage(FullMsgId id) { return id; }
}

struct PhotoMedia;
struct PhotoData {
	bool failedFlag = false;
	bool loadedFlag = false;
	int started = 0;
	int startedWhileFailed = 0;
	std::shared_ptr<PhotoMedia> createMediaView();
	bool failed(Data::PhotoSize) const { return failedFlag; }
	void clearFailed(Data::PhotoSize) { failedFlag = false; }
};
struct PhotoMedia {
	PhotoData *owner = nullptr;
	void wanted(Data::PhotoSize, Data::FileOrigin) {
		// The real loader does not start for a file marked failed.
		++(owner->failedFlag ? owner->startedWhileFailed : owner->started);
	}
	bool loaded() const { return owner->loadedFlag; }
	bool saveToFile(const QString &path) {
		if (!owner->loadedFlag) return false;
		files[path.value] = 7;
		return true;
	}
};
std::shared_ptr<PhotoMedia> PhotoData::createMediaView() {
	return std::make_shared<PhotoMedia>(PhotoMedia{ this });
}

struct DocumentData {
	qint64 size = 10;
	int status = FileReady;
	bool loadingFlag = false;
	QString target;
	QString loaded;
	int saves = 0;
	QString filepath(bool) const { return loaded; }
	bool loading() const { return loadingFlag; }
	QString loadingFilePath() const { return target; }
	void save(Data::FileOrigin, const QString &path) {
		++saves;
		loadingFlag = true;
		target = path;
	}
};

struct Media {
	PhotoData *photoValue = nullptr;
	DocumentData *documentValue = nullptr;
	PhotoData *photo() const { return photoValue; }
	DocumentData *document() const { return documentValue; }
};
struct HistoryItem {
	Media mediaValue;
	const Media *media() const { return &mediaValue; }
};

namespace Data {
struct Session {
	rpl::event_stream<> cleared;
	rpl::event_stream<not_null<PhotoData*>> photoProgress;
	rpl::event_stream<not_null<DocumentData*>> documentProgress;
	PhotoData photoValue;
	DocumentData documentValue;
	HistoryItem item;
	auto sessionDataAboutToBeCleared() { return cleared.events(); }
	auto photoLoadProgress() { return photoProgress.events(); }
	auto documentLoadProgress() { return documentProgress.events(); }
	not_null<PhotoData*> photo(PhotoId) { return &photoValue; }
	not_null<DocumentData*> document(DocumentId) { return &documentValue; }
	HistoryItem *message(FullMsgId) { return &item; }
};
}
namespace Main {
struct Session : base::has_weak_ptr {
	Data::Session dataValue;
	rpl::event_stream<> downloads;
	Data::Session &data() { return dataValue; }
	auto downloaderTaskFinished() { return downloads.events(); }
};
}
namespace crl {
template <class T, class F> void on_main(base::weak_ptr<T> owner, F f) {
	Test::main.push_back([owner, f = std::move(f)]() mutable { if (owner.get()) f(); });
}
template <class F> void on_main_sync(F f) { f(); }
}

namespace LuxurySync {
using WeakSession = base::weak_ptr<Main::Session>;
using Cancelled = Fn<bool()>;
QString filePath(not_null<Main::Session*>, not_null<PhotoData*>) { return "photo.jpg"; }
namespace {
SOURCE_HELPERS
[[nodiscard]] QString NextDocumentPath(not_null<Main::Session*>, not_null<DocumentData*>) { return "doc.bin"; }
}
SOURCE_LOADERS
}

using namespace std::chrono_literals;
using LuxurySync::loadDocumentSync;
using LuxurySync::loadPhotoSync;

std::chrono::milliseconds elapsedSince(std::chrono::steady_clock::time_point start) {
	return std::chrono::duration_cast<std::chrono::milliseconds>(Test::Clock::time - start);
}

int main() {
	const auto prompt = 2s;
	{
		// A photo whose download fails reports it through its own load progress.
		auto session = std::make_unique<Main::Session>();
		auto &data = session->data();
		auto other = PhotoData();
		Test::steps.push_back([&] { data.photoProgress.fire(&other); assert(waiting()); });
		Test::steps.push_back([&] {
			data.photoValue.failedFlag = true;
			data.photoProgress.fire(&data.photoValue);
		});
		const auto start = Test::Clock::time;
		assert(loadPhotoSync(base::make_weak(session.get()), PhotoId(1), Data::FileOrigin(), nullptr).isEmpty());
		assert(elapsedSince(start) < prompt);
		assert(data.photoValue.started == 1);
	}
	{
		// A photo that failed before, e.g. in the chat, is asked for again.
		files.clear();
		Test::steps.clear();
		auto session = std::make_unique<Main::Session>();
		auto &data = session->data();
		data.photoValue.failedFlag = true;
		Test::steps.push_back([&] {
			data.photoValue.loadedFlag = true;
			data.photoProgress.fire(&data.photoValue);
		});
		assert(loadPhotoSync(base::make_weak(session.get()), PhotoId(1), Data::FileOrigin(), nullptr) == QString("photo.jpg"));
		assert(data.photoValue.started == 1 && data.photoValue.startedWhileFailed == 0);
	}
	{
		// A successful download still completes on the downloader signal alone.
		files.clear();
		Test::steps.clear();
		auto session = std::make_unique<Main::Session>();
		auto &data = session->data();
		Test::steps.push_back([&] {
			data.photoValue.loadedFlag = true;
			session->downloads.fire({});
		});
		assert(loadPhotoSync(base::make_weak(session.get()), PhotoId(1), Data::FileOrigin(), nullptr) == QString("photo.jpg"));
	}
	{
		// The message-photo overload used by plain custom forwarding.
		files.clear();
		Test::steps.clear();
		auto session = std::make_unique<Main::Session>();
		auto &data = session->data();
		data.item.mediaValue.photoValue = &data.photoValue;
		data.photoValue.failedFlag = true;
		Test::steps.push_back([&] {
			assert(data.photoValue.started == 1);
			data.photoValue.failedFlag = true;
			data.photoProgress.fire(&data.photoValue);
		});
		const auto start = Test::Clock::time;
		loadPhotoSync(base::make_weak(session.get()), FullMsgId{ 1, 2 }, QString("item.jpg"), nullptr);
		assert(elapsedSince(start) < prompt);
		assert(!files.contains("item.jpg"));
	}
	{
		// A document whose download fails, by document id (rich pages).
		files.clear();
		Test::steps.clear();
		auto session = std::make_unique<Main::Session>();
		auto &data = session->data();
		auto other = DocumentData();
		Test::steps.push_back([&] { data.documentProgress.fire(&other); assert(waiting()); });
		Test::steps.push_back([&] {
			data.documentValue.loadingFlag = false;
			data.documentValue.status = FileDownloadFailed;
			data.documentProgress.fire(&data.documentValue);
		});
		const auto start = Test::Clock::time;
		assert(loadDocumentSync(base::make_weak(session.get()), DocumentId(1), Data::FileOrigin(), nullptr).isEmpty());
		assert(elapsedSince(start) < prompt);
		assert(data.documentValue.saves == 1);
	}
	{
		// A document that downloads fine is returned as before.
		files.clear();
		Test::steps.clear();
		auto session = std::make_unique<Main::Session>();
		auto &data = session->data();
		Test::steps.push_back([&] {
			data.documentValue.loadingFlag = false;
			data.documentValue.loaded = "doc.bin";
			files["doc.bin"] = 10;
			session->downloads.fire({});
		});
		assert(loadDocumentSync(base::make_weak(session.get()), DocumentId(1), Data::FileOrigin(), nullptr) == QString("doc.bin"));
	}
	{
		// The message-document overload used by plain custom forwarding.
		files.clear();
		Test::steps.clear();
		auto session = std::make_unique<Main::Session>();
		auto &data = session->data();
		data.item.mediaValue.documentValue = &data.documentValue;
		Test::steps.push_back([&] {
			data.documentValue.loadingFlag = false;
			data.documentValue.status = FileDownloadFailed;
			data.documentProgress.fire(&data.documentValue);
		});
		const auto start = Test::Clock::time;
		loadDocumentSync(base::make_weak(session.get()), FullMsgId{ 1, 3 }, QString("item.bin"), nullptr);
		assert(elapsedSince(start) < prompt);
		assert(data.documentValue.saves == 1);
	}
	{
		// Cancelled from the download bar: the loader is gone, status stays
		// normal. The wait must end now, not after the 15-minute timeout.
		files.clear();
		Test::steps.clear();
		auto session = std::make_unique<Main::Session>();
		auto &data = session->data();
		data.item.mediaValue.documentValue = &data.documentValue;
		Test::steps.push_back([&] {
			data.documentValue.loadingFlag = false;
			data.documentProgress.fire(&data.documentValue);
		});
		const auto start = Test::Clock::time;
		loadDocumentSync(base::make_weak(session.get()), FullMsgId{ 1, 3 }, QString("item.bin"), nullptr);
		assert(elapsedSince(start) < prompt);
	}
	std::puts("forward media failure PASS: photo/document failures by id and by message end the wait at once, other files' progress does not, an earlier photo failure is retried, successes unchanged; real loaders/rpl/weak_ptr, Qt/session/files/clock doubles");
}
'''


def fixture(text, updates):
    weak = source('Telegram/lib_base/base/weak_ptr.h')
    weak = weak.replace('#include "base/basic_types.h"', '#include <gsl/gsl>').replace('#pragma once', '')
    wait = function(text, 'bool WaitUntil(').replace('std::chrono::steady_clock::now()', 'Test::Clock::now()')
    timeout = text[text.index('constexpr auto kDocumentDownloadTimeout'):]
    timeout = timeout[:timeout.index('\n')]
    helpers = '\n'.join([
        wait,
        timeout,
        function(text, '[[nodiscard]] bool SameFilePath('),
        function(text, '[[nodiscard]] bool FileHasExactSize('),
        function(text, '[[nodiscard]] bool DocumentReady('),
        function(updates, 'rpl::producer<> PhotoLoadUpdates('),
        function(updates, 'rpl::producer<> DocumentLoadUpdates('),
    ])
    loaders = '\n'.join(function(text, signature) for signature in (
        'void loadDocumentSync(',
        'QString loadDocumentSync(',
        'void loadPhotoSync(',
        'QString loadPhotoSync(',
    ))
    return (CPP.replace('WEAK_HEADER', weak)
        .replace('SOURCE_HELPERS', helpers)
        .replace('SOURCE_LOADERS', loaders))


def check(text=None, updates=None):
    text = text if text is not None else source(SYNC).replace('\r\n', '\n')
    updates = updates if updates is not None else source(HELPERS)
    with tempfile.TemporaryDirectory(prefix='luxury-forward-media-failure-') as directory:
        tmp = Path(directory)
        path = tmp / 'failure.cpp'
        path.write_text(fixture(text, updates), encoding='utf-8')
        exe = tmp / 'failure.exe'
        includes = []
        for include in ('Telegram/lib_rpl', 'Telegram/lib_base', 'Telegram/ThirdParty/GSL/include'):
            includes += ['-I', str(ROOT / include)]
        run([CXX, '-std=c++20', '-O0', '-g', '-D_DEBUG', *includes, str(path), '-o', str(exe)], tmp)
        print(run([str(exe)], tmp).strip())


if __name__ == '__main__':
    check()
