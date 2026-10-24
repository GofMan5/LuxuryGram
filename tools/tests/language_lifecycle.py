"""Compile actual language owner/generation/cancel/batch tail on synthetic owners.
The batch tail also proves the Russian overlay outranks the CDN pack, pack
values never name AyuGram, and English neither fetches nor applies a pack.
python tools/tests/language_lifecycle.py [--cxx g++]
Real rpl lifetime/GSL, synthetic Qt/network/cache/string parser; no live CDN/UI.
"""
from pathlib import Path
import argparse
import os
import re
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'Telegram/SourceFiles/luxury/luxury_lang.cpp').read_text('utf-8')
instance = (root / 'Telegram/SourceFiles/lang/lang_instance.cpp').read_text('utf-8')
owner = source[source.index('bool LuxuryLanguage::currentLanguageMatches()'):source.index('QString LuxuryLanguage::getCacheDir()')]
clear = source[source.index('void LuxuryLanguage::clearReply()'):source.index('void LuxuryLanguage::applyLanguageJson(')]
current = source[source.index('\tconst auto current = [this, reply, generation] {'):source.index('\n\tconnect(', source.index('\tconst auto current = [this, reply, generation] {'))]
notify = instance[instance.index('void Instance::notifyUpdated()'):instance.index('std::map<ushort, QString> Instance::ParseStrings(')]
assert 'idChanges() | rpl::on_next' in source and 'reloadLanguage();\n\t}, _lifetime);' in source
assert source.count('if (!_chkReply || !currentLanguageMatches())') == 2
assert source.count('if (current())') == 2
assert source.index('++_requestGeneration;') < source.index('reply->abort();', source.index('void LuxuryLanguage::clearReply()'))
assert 'const auto baseId = _languageBaseId;' in source and 'const auto langPackBaseId = _languageBaseId;' in source
assert 'const auto requestedId = (needFallback || finalLangPackId.isEmpty())' in source
assert '!doc.isObject()' in source and '!json.value(brokenKey).isString()' in source
apply = source[source.index('void LuxuryLanguage::applyLanguageJson('):]
assert apply.count('language.notifyUpdated();') == 1
assert apply.index('for (const QString &brokenKey') < apply.index('if (changed)') < apply.index('language.notifyUpdated();')
tail = apply[apply.index('\t\tconst auto keyBytes'):].removesuffix('\n')
english = re.search(r'\tconst auto english = [^;]*;\n', apply).group(0)
# Exact batch mutation/notification tail; normalization is covered by source contract.
cpp = r'''
#include <cassert>
#include <cstdint>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <rpl/lifetime.h>
struct QString : std::string {
 using std::string::string;
 bool isEmpty() const { return empty(); }
};
using quint64 = uint64_t;
namespace base { template <class T> T take(T &value) { return std::exchange(value, T{}); } }
struct Signal { int count = 0; void fire(std::initializer_list<int>) { ++count; } };
struct Instance {
 QString language = "A", base = "base-A";
 bool custom = false;
 Instance *_derived = nullptr;
 Signal _updated;
 std::map<std::string, std::string> values;
 QString id() const { return language; }
 QString baseId() const { return base; }
 bool isCustom() const { return custom; }
 void resetValue(const std::string &key) { values.erase(key); }
 void applyValue(const std::string &key, const std::string &value) { values[key] = value; }
 void updatePluralRules() {}
 void notifyUpdated();
};
NOTIFY_HERE
namespace Lang { Instance pack; Instance &GetInstance() { return pack; } QString DefaultLanguageId() { return "en"; } }
namespace LuxuryRussian { void applyIfRussian(Instance *language, const QString &id) { if (id == "ru") language->values["luxury_A"] = "overlay"; } }
std::string operator""_q(const char16_t *data, std::size_t size) { return std::string(data, data + size); }
struct Reply { bool aborted = false, disconnected = false, deleted = false; std::function<void()> onAbort; void abort() { aborted = true; if(onAbort) onAbort(); } void deleteLater() { deleted = true; } };
void disconnect(Reply *reply, std::nullptr_t, void*, std::nullptr_t) { reply->disconnected = true; }
class LuxuryLanguage {
public:
 Reply *_chkReply = nullptr;
 bool needFallback = false;
 QString _languageId, _languageBaseId;
 quint64 _requestGeneration = 0;
 std::vector<std::pair<QString, QString>> loads, requests;
 std::function<void()> onLoad;
 bool currentLanguageMatches() const;
 void reloadLanguage();
 void clearReply();
 void loadCachedLanguage() { loads.emplace_back(_languageId, _languageBaseId); if(onLoad) std::exchange(onLoad, {})(); }
 void fetchLanguage(const QString &id, const QString &base) { requests.emplace_back(id, base); }
 std::function<bool()> guard() {
  const auto reply = _chkReply;
  const auto generation = _requestGeneration;
CURRENT_HERE
  return current;
 }
};
OWNER_HERE
CLEAR_HERE
struct Text {
 std::string value;
 std::string toUtf8() const { return value; }
 Text &replace(const std::string &from, const std::string &to) {
  for (auto i = value.find(from); i != std::string::npos; i = value.find(from, i + to.size())) value.replace(i, from.size(), to);
  return *this;
 }
};
void batch(const std::vector<std::pair<Text, Text>> &entries) {
 auto &language = Lang::GetInstance();
ENGLISH_HERE auto changed = false;
 for (auto [key, val] : entries) {
TAIL_HERE
int main() {
 LuxuryLanguage state;
 Reply first, second;
 state.reloadLanguage();
 assert(state.loads.back().first == "A" && state.requests.back().second == "base-A");
 state._chkReply = &first; const auto old = state.guard(); assert(old());
 bool invalidatedBeforeAbort = false;
 first.onAbort = [&] { invalidatedBeforeAbort = !old(); };
 Lang::pack.language = "B"; Lang::pack.base = "base-B";
 state.needFallback = true; state.reloadLanguage();
 assert(first.aborted && first.disconnected && first.deleted && invalidatedBeforeAbort && !old());
 assert(!state.needFallback && state.loads.back().first == "B" && state.requests.back().second == "base-B");
 state._chkReply = &second; const auto latest = state.guard(); assert(latest());
 Lang::pack.base = "new-base"; assert(!latest()); state.reloadLanguage(); assert(state.requests.back().second == "new-base");
 state._chkReply = &first; const auto reused = state.guard(); state.clearReply(); state._chkReply = &first;
 assert(!reused()); // Even an equal pointer cannot restore an old generation.
 const auto before = state.requests.size();
 state.onLoad = [&] { Lang::pack.language = "C"; Lang::pack.base = "base-C"; state.reloadLanguage(); };
 state.reloadLanguage();
 assert(state.requests.size() == before + 1 && state.requests.back().first == "C");
 Lang::pack.custom = true; state.reloadLanguage(); assert(state.requests.size() == before + 1);
 Lang::pack.custom = false; Lang::pack.language.clear(); state.reloadLanguage(); assert(state.requests.size() == before + 1);
 Lang::pack._updated.count = 0;
 batch({{{"luxury_A"}, {"one"}}, {{"luxury_B"}, {"two"}}});
 assert(Lang::pack.values.size() == 2 && Lang::pack._updated.count == 1);
 batch({}); assert(Lang::pack._updated.count == 1);
 Lang::pack.applyValue("luxury_C", "three"); assert(Lang::pack._updated.count == 1); // Shared primitive remains silent.
 Instance derived; Lang::pack._derived = &derived; batch({{{"luxury_D"}, {"four"}}});
 assert(derived._updated.count == 1 && Lang::pack._updated.count == 1);
 Lang::pack._derived = nullptr; Lang::pack.values.clear(); Lang::pack.language = "ru";
 batch({{{"luxury_A"}, {"CDN wording"}}, {{"luxury_E"}, {"AyuGram and AyuGram"}}});
 assert(Lang::pack.values["luxury_A"] == "overlay"); // LuxuryGram's own translation wins.
 assert(Lang::pack.values["luxury_E"] == "LuxuryGram and LuxuryGram");
 Lang::pack.language = "en"; Lang::pack._updated.count = 0;
 batch({{{"luxury_E"}, {"from an old en cache"}}});
 assert(!Lang::pack.values.count("luxury_E") && Lang::pack._updated.count == 1); // Reset to lang.strings.
 const auto beforeEnglish = state.requests.size(); Lang::pack.base.clear(); state.reloadLanguage();
 assert(state.loads.back().first == "en" && state.requests.size() == beforeEnglish);
 std::cout << "PASS: actual language owner/generation/cancel/batch tail; synthetic Qt/network/cache, native UI pending\n";
}
'''
cpp = cpp.replace('OWNER_HERE', owner).replace('CLEAR_HERE', clear).replace('CURRENT_HERE', current).replace('NOTIFY_HERE', notify).replace('TAIL_HERE', tail).replace('ENGLISH_HERE', english)
parser = argparse.ArgumentParser()
parser.add_argument('--cxx', default=os.environ.get('CXX') or shutil.which('g++') or r'C:\Strawberry\c\bin\g++.exe')
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix='luxury-language-') as directory:
    work = Path(directory); test = work / 'language.cpp'; test.write_text(cpp, encoding='utf-8')
    exe = work / ('language.exe' if os.name == 'nt' else 'language')
    subprocess.run([args.cxx, '-std=c++20', '-O0', '-D_DEBUG', '-I', str(root / 'Telegram/lib_rpl'), '-I', str(root / 'Telegram/lib_base'), str(test), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
