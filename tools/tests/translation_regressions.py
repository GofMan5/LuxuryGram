"""Compile actual batch scheduler/cancel and reset against deterministic transports.
python tools/tests/translation_regressions.py [--cxx g++]
Qt pointer/timer/reply/MTP/cache and provider shells are explicit doubles, no egress.
"""
from pathlib import Path
import argparse
import os
import shutil
import subprocess
import tempfile
root = Path(__file__).resolve().parents[2]
def read(path): return (root / 'Telegram/SourceFiles' / path).read_text('utf-8')
base = read('luxury/features/translator/implementations/base.cpp')
batch = base[base.index('CallbackCancel MultiThreadTranslator::startTranslation('):base.rindex('\n}')]
manager = read('luxury/features/translator/luxury_translator.cpp')
reset = manager[manager.index('void TranslateManager::resetCache()'):manager.index('TranslateManager *TranslateManager::currentInstance()')]
mtproto = read('lang/translate_mtproto_provider.cpp')
rich = mtproto[mtproto.index('bool SupportsRichMessageTranslation('):mtproto.index('std::unique_ptr<TranslateProvider> CreateMTProtoTranslateProvider(')]
assert 'SupportsRichMessageTranslation(state->provider.get())' in read('boxes/translate_box.cpp')
assert 'Ui::SupportsRichMessageTranslation(_provider.get())' in read('history/view/history_view_translate_tracker.cpp')
assert 'supportsMessageId() const override {\n\t\treturn true;' in read('luxury/features/translator/luxury_translate_provider.cpp')
success = manager[manager.index('](const std::vector<TextWithEntities> &translated) mutable {'):manager.index('\n\tCallbackFail onFail')]
assert success.index('if (!_pending.contains(id))') < success.index('insertToCache(')
assert 'auto cancel = translator->startTranslation(args);' in manager and 'current->second.cancel = std::move(cancel);' in manager
assert 'else if (cancel) {\n\t\t\tcancel();' in manager
yandex = read('luxury/features/translator/implementations/yandex.cpp')
assert 'Html::entitiesToHtml(text)' not in yandex
form_body = yandex[yandex.index('\tconst auto postDataEncoded'):yandex.index('\n\n\tQPointer<QNetworkReply> reply')]
cpp = r'''
#include <cassert>
#include <cctype>
#include <cmath>
#include <deque>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>
struct QString : std::string { using std::string::string; bool isEmpty() const { return empty(); } QString trimmed() const { return *this; } };
struct TextWithEntities { QString text; };
#define QStringLiteral(value) std::string(value)
// Qt: every byte outside RFC 3986 unreserved A-Z a-z 0-9 - . _ ~ is %XX.
struct QUrl { static std::string toPercentEncoding(const QString &value) {
 std::string result; const char *hex = "0123456789ABCDEF";
 for (const unsigned char c : value) {
  if (std::isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~') result += char(c);
  else { result += '%'; result += hex[c >> 4]; result += hex[c & 15]; }
 }
 return result;
} };
// application/x-www-form-urlencoded as the server reads it.
std::map<std::string, std::string> formDecode(const std::string &body) {
 std::map<std::string, std::string> result; std::string key, value, *current = &key;
 const auto flush = [&] { result[key] = value; key.clear(); value.clear(); current = &key; };
 for (std::size_t i = 0; i < body.size(); ++i) {
  const char c = body[i];
  if (c == '&') flush();
  else if (c == '=' && current == &key) current = &value;
  else if (c == '+') *current += ' ';
  else if (c == '%' && i + 2 < body.size() && std::isxdigit((unsigned char)body[i + 1]) && std::isxdigit((unsigned char)body[i + 2])) { *current += char(std::stoi(body.substr(i + 1, 2), nullptr, 16)); i += 2; }
  else *current += c;
 }
 flush(); return result;
}
std::map<std::string, std::string> yandexForm(const QString &to, const TextWithEntities &text) { FORM_BODY_HERE return formDecode(postDataEncoded); }
using CallbackSuccess = std::function<void(const std::vector<TextWithEntities>&)>;
using CallbackFail = std::function<void()>;
using CallbackCancel = std::function<void()>;
struct ParsedData { std::vector<TextWithEntities> texts; QString fromLang, toLang; };
struct StartTranslationArgs { ParsedData parsedData; CallbackSuccess onSuccess; CallbackFail onFail; };
struct MultiThreadArgs { struct Data { TextWithEntities text; QString fromLang, toLang; } parsedData; std::function<void(const TextWithEntities&)> onSuccess; CallbackFail onFail; };
template<class T> using QPointer = T*;
struct QNetworkReply { bool running = true, aborted = false; std::function<void()> onAbort; bool isRunning() const { return running; } void abort() { aborted = true; running = false; if(onAbort) onAbort(); } };
struct QTimer {
 static inline std::vector<QTimer*> all;
 bool active = false, deleted = false; std::function<void()> callback;
 explicit QTimer(void*) { all.push_back(this); }
 void setSingleShot(bool) {} void start(int) { active = true; } void stop() { active = false; } void deleteLater() { deleted = true; }
 void timeout() {} void fire() { if(callback) callback(); }
};
struct QObject { template<class Callback> static void connect(QTimer *timer, void(QTimer::*)(), Callback callback) { timer->callback = std::move(callback); } };
struct MultiThreadTranslator {
 int concurrency = 1, retries = 3; bool synchronous = false;
 std::deque<MultiThreadArgs> sent; std::vector<std::unique_ptr<QNetworkReply>> replies;
 int getConcurrencyLimit() const { return concurrency; } int getMaxRetries() const { return retries; } int getBaseWaitTimeMs() const { return 1; }
 QPointer<QNetworkReply> startSingleTranslation(const MultiThreadArgs &args) {
  sent.push_back(args); auto reply = std::make_unique<QNetworkReply>(); auto ptr = reply.get(); replies.push_back(std::move(reply));
  ptr->onAbort = args.onFail;
  if(synchronous) args.onSuccess({"sync"});
  return ptr;
 }
 CallbackCancel startTranslation(const StartTranslationArgs &args);
};
BATCH_HERE
namespace base { template<class T> T take(T &v) { return std::exchange(v, T{}); } }
namespace MTP { struct Error { std::string type; explicit Error(std::string s) : type(std::move(s)) {} static std::string MTPLocal(const char *s, const char*) { return s; } }; }
struct TranslateManager {
 struct Pending { std::function<void(const MTP::Error&)> fail; CallbackCancel cancel; };
 std::map<int, Pending> _pending; std::vector<int> _cacheList, _cacheMap;
 void resetCache();
};
RESET_HERE
namespace Ui {
struct TranslateProvider { virtual ~TranslateProvider() = default; virtual bool supportsMessageId() const = 0; };
struct MTProtoTranslateProvider : TranslateProvider { bool supportsMessageId() const override { return true; } };
struct External : TranslateProvider { bool supportsMessageId() const override { return true; } };
struct Native : TranslateProvider { bool supportsMessageId() const override { return false; } };
template<class T> struct not_null { T p; T get() const { return p; } };
RICH_HERE
}
StartTranslationArgs args(int &success, int &fail, int count = 2) {
 StartTranslationArgs result; result.parsedData.toLang = "ru";
 for(int i = 0; i < count; ++i) result.parsedData.texts.push_back({"input"});
 result.onSuccess = [&](const auto&) { ++success; }; result.onFail = [&] { ++fail; }; return result;
}
int main() {
 {
  int success = 0, fail = 0; MultiThreadTranslator t; auto cancel = t.startTranslation(args(success, fail));
  assert(t.sent.size() == 1); cancel(); assert(t.replies[0]->aborted && t.sent.size() == 1 && !fail);
  t.sent[0].onSuccess({"late"}); t.sent[0].onFail(); cancel(); assert(t.sent.size() == 1 && !success && !fail);
 }
 {
  int success = 0, fail = 0; MultiThreadTranslator t; auto cancel = t.startTranslation(args(success, fail));
  t.replies[0]->running = false; t.sent[0].onFail(); auto timer = QTimer::all.back(); assert(timer->active);
  cancel(); assert(!timer->active && timer->deleted); const auto count = t.sent.size(); timer->fire(); assert(t.sent.size() == count && !fail && !success);
 }
 {
  int success = 0, fail = 0; MultiThreadTranslator t; auto cancel = t.startTranslation(args(success, fail));
  t.sent[0].onSuccess({"first"}); assert(t.sent.size() == 2); t.sent[0].onSuccess({"duplicate"}); assert(t.sent.size() == 2);
  t.sent[1].onSuccess({"second"}); assert(success == 1 && !fail); cancel(); assert(!t.replies[1]->aborted);
 }
 {
  int success = 0, fail = 0; MultiThreadTranslator t; t.synchronous = true; auto cancel = t.startTranslation(args(success, fail, 3));
  assert(t.sent.size() == 3 && success == 1 && !fail); cancel();
 }
 {
  int success = 0, fail = 0; MultiThreadTranslator t; auto empty = args(success, fail, 0); auto cancel = t.startTranslation(empty); assert(!cancel && fail == 1 && t.sent.empty());
 }
 {
  int success = 0, fail = 0; MultiThreadTranslator t; t.concurrency = 2; auto cancel = t.startTranslation(args(success, fail, 4));
  assert(t.sent.size() == 2); cancel(); assert(t.replies[0]->aborted && t.replies[1]->aborted && t.sent.size() == 2);
 }
 {
  TranslateManager manager; int cancellations = 0, failures = 0;
  for(int id = 1; id <= 2; ++id) manager._pending[id] = {
   [&](const MTP::Error &error) { assert(error.type == "REQUEST_CANCELED" && cancellations == 2); ++failures; manager._pending[99] = {}; },
   [&] { assert(manager._pending.empty()); ++cancellations; }
  };
  manager._cacheList = {1}; manager._cacheMap = {2}; manager.resetCache();
  assert(cancellations == 2 && failures == 2 && manager._pending.size() == 1 && manager._pending.contains(99) && manager._cacheList.empty() && manager._cacheMap.empty());
 }
 for (const char *literal : {"a\nb", "<br> & <tag>", "x > y", "&#10;", "https://example.org/?x=1&y=2", "C++ and +7", "50% %41 %2B", "a=b&c#d", "\xD0\x9F\xD1\x80\xD0\xB8 \xD0\xB2\xD0\xB5\xD1\x82"}) {
  TextWithEntities input; input.text = QString(literal); const auto form = yandexForm("en", input);
  assert(form.size() == 2 && form.at("lang") == "en" && form.at("text") == literal);
 }
 Ui::MTProtoTranslateProvider telegram; Ui::External google, yandex; Ui::Native native;
 assert(Ui::SupportsRichMessageTranslation({&telegram}) && !Ui::SupportsRichMessageTranslation({&google}) && !Ui::SupportsRichMessageTranslation({&yandex}) && !Ui::SupportsRichMessageTranslation({&native}));
 assert(google.supportsMessageId() && yandex.supportsMessageId());
 // Release fixture callback cycles before deleting fake QObject timers.
 for(auto timer : QTimer::all) { timer->callback = {}; delete timer; } QTimer::all.clear();
 std::cout << "PASS: actual translation batch/cancel/reset/rich capability with deterministic transport and provider doubles; no native Qt/egress\n";
}
'''.replace('BATCH_HERE', batch).replace('RESET_HERE', reset).replace('RICH_HERE', rich).replace('FORM_BODY_HERE', form_body)
parser = argparse.ArgumentParser()
parser.add_argument('--cxx', default=os.environ.get('CXX') or shutil.which('g++') or r'C:\Strawberry\c\bin\g++.exe')
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix='luxury-translation-') as directory:
    work = Path(directory); test = work / 'translation.cpp'; test.write_text(cpp, encoding='utf-8')
    exe = work / ('translation.exe' if os.name == 'nt' else 'translation')
    subprocess.run([args.cxx, '-std=c++20', '-O0', '-D_DEBUG', str(test), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
