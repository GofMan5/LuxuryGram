"""Russian overlay start-up path and localized "last seen ... ago" units.
python tools/tests/ru_overlay_regressions.py [--cxx g++]
Compiles the actual LastSeenAgoElapsed body against stub tr:: keys; checks the
cached-langpack path re-applies LuxuryRussian and the overlay covers every
luxury_* key of lang.strings; the CDN pack never outranks the
overlay, never carries AyuGram's name and is not applied over English.
No Qt, no UI.
"""
from pathlib import Path
import argparse
import os
import re
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
sources = root / 'Telegram/SourceFiles'
instance = (sources / 'lang/lang_instance.cpp').read_text('utf-8')
values = (sources / 'data/data_peer_values.cpp').read_text('utf-8')
settings = (sources / 'luxury/luxury_settings.cpp').read_text('utf-8')
cdn = (sources / 'luxury/luxury_lang.cpp').read_text('utf-8')

fill = instance[instance.index('void Instance::fillFromSerialized('):instance.index('void Instance::loadFromContent(')]
loop = fill.index('applyValue(nonDefaultStrings[i], nonDefaultStrings[i + 1]);')
overlay = fill.index('LuxuryRussian::applyIfRussian(not_null<Lang::Instance*>(this), _id);')
assert loop < overlay < fill.index('updatePluralRules();') < fill.index('_idChanges.fire_copy(_id);'), \
    'cached langpack must re-apply the Russian overlay after its own values'

reset = instance[instance.index('void Instance::reset('):instance.index('QString Instance::systemLangCode()')]
assert 'LuxuryRussian::applyIfRussian(' in reset, 'language switch keeps the overlay'

table = settings[settings.index('kRussianSettingsStrings'):settings.index('void applyIfRussian(')]
ru = dict(re.findall(r'\{\s*"(luxury_[A-Za-z0-9_#]+)",\s*"((?:[^"\\]|\\.)*)"\s*\}', table))
for key in (
        'WatchShowSavedMedia', 'StickersMoveToFront', 'DatabaseError',
        'RegexFiltersPatternTooLong', 'TranslateBatchFailed',
        'FiltersToastFailUnsupported', 'MessageShotAnonymousName',
        'LuxuryForwardStatusPreparing', 'LuxuryForwardStatusLoadingMedia',
        'LuxuryForwardStatusForwarding', 'LuxuryForwardStatusFinished',
        'LuxuryForwardStatusSentCount', 'LuxuryForwardStatusChunkCount',
        'LastSeenAgo', 'UpdateLuxuryGram', 'ContextSelectBetween',
        'PlayerExpand', 'PlayerTitle', 'PlayerSaveCover',
        'PlayerSaveCoverFailed'):
    value = ru.get('luxury_' + key)
    assert value and re.search('[а-яё]', value, re.I), f'missing Russian luxury_{key}'
for key in ('TranslateBatchOthersDone', 'LuxuryForwardStatusSkippedCount'):
    for form in ('one', 'few', 'many', 'other'):
        assert f'luxury_{key}#{form}' in ru, f'missing luxury_{key}#{form}'

# The CDN pack arrives late or never (offline, first start), and its words
# differ from ours ("режим призрака" next to "гост-режим"), so every key
# needs its own Russian text, every Russian plural form included.
strings = (root / 'Telegram/Resources/langs/lang.strings').read_text('utf-8')
families = {}
for key, form in re.findall(r'^"(luxury_[A-Za-z0-9_]+)(?:#(\w+))?"\s*=', strings, re.M):
    families.setdefault(key, set()).add(form)
for key, forms in families.items():
    needed = [key] if forms == {''} else [f'{key}#{form}' for form in ('one', 'few', 'many', 'other')]
    for name in needed:
        assert name in ru, f'{name} has no Russian overlay'

reload = cdn[cdn.index('void LuxuryLanguage::reloadLanguage()'):cdn.index('QString LuxuryLanguage::getCacheDir()')]
assert 'id != Lang::DefaultLanguageId()' in reload[:reload.index('fetchLanguage(id, baseId);')], \
    'lang.strings is the English source; the CDN English pack must not replace it'
apply = cdn[cdn.index('void LuxuryLanguage::applyLanguageJson('):]
assert 'if (!english) {' in apply[:apply.index('language.applyValue(')], \
    'a cached English pack only clears its stale values'
assert apply.index('val.replace(u"AyuGram"_q, u"LuxuryGram"_q);') < apply.index('language.applyValue('), \
    'CDN strings must not name AyuGram'
assert apply.rindex('language.applyValue(') < apply.index('LuxuryRussian::applyIfRussian(') < apply.index('language.notifyUpdated();'), \
    'the Russian overlay must win over the CDN pack'

body = values[values.index('QString LastSeenAgoElapsed(qint64 elapsed) {'):values.index('QString ExactLastSeenText(')]
assert all(ord(c) < 128 for c in body), 'elapsed units must come from the language pack'

cpp = r'''
#include <algorithm>
#include <cassert>
#include <iostream>
#include <string>
struct QString : std::string {
 QString() = default;
 QString(std::string value) : std::string(std::move(value)) {}
};
using qint64 = long long;
constexpr int lt_count = 0;
namespace tr {
struct now_t {};
constexpr now_t now{};
QString lng_seconds_tiny(now_t, int, int n) { return std::to_string(n) + "s"; }
QString lng_minutes_tiny(now_t, int, int n) { return std::to_string(n) + "m"; }
QString lng_hours_tiny(now_t, int, int n) { return std::to_string(n) + "h"; }
}
BODY_HERE
int main() {
 assert(LastSeenAgoElapsed(-5) == "0s");
 assert(LastSeenAgoElapsed(0) == "0s");
 assert(LastSeenAgoElapsed(59) == "59s");
 assert(LastSeenAgoElapsed(60) == "1m 0s");
 assert(LastSeenAgoElapsed(61) == "1m 1s");
 assert(LastSeenAgoElapsed(3599) == "59m 59s");
 assert(LastSeenAgoElapsed(3600) == "1h 0m 0s");
 assert(LastSeenAgoElapsed(3725) == "1h 2m 5s");
 assert(LastSeenAgoElapsed(90061) == "25h 1m 1s");
 std::cout << "PASS: RU overlay start-up order, overlay coverage, localized last-seen units\n";
}
'''.replace('BODY_HERE', body)

parser = argparse.ArgumentParser()
parser.add_argument('--cxx', default=os.environ.get('CXX') or shutil.which('g++') or r'C:\Strawberry\c\bin\g++.exe')
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix='luxury-ru-overlay-') as directory:
    work = Path(directory)
    test = work / 'ru_overlay.cpp'
    test.write_text(cpp, encoding='utf-8')
    exe = work / ('ru_overlay.exe' if os.name == 'nt' else 'ru_overlay')
    subprocess.run([args.cxx, '-std=c++20', '-O0', str(test), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
