"""Compile actual ad/custom-suggestion/notification response slice on doubles.
python tools/tests/promo_notifications.py [--cxx g++]
Synthetic MTP/Qt/session data; not live network or top-bar rendering.
"""
from pathlib import Path
import argparse
import os
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'Telegram/SourceFiles/data/components/promo_suggestions.cpp').read_text('utf-8')
start = source.index('\t\t\tconst auto &settings = LuxurySettings::getInstance();')
end = source.index('\n\t\t});', start)
response = source[start:end]
assert 'return;' not in response
assert response.index('settings.disableAds()') < response.index('_custom != custom') < response.index('_refreshed.fire({})')
cpp = r'''
#include <cassert>
#include <iostream>
#include <optional>
#include <utility>
struct QString {};
struct Value { int value = 0; int value_or_empty() const { return value; } };
struct Data {
 std::optional<int> peer = 7, custom = 4;
 auto vpeer() const { return peer; }
 auto vcustom_pending_suggestion() const { return custom; }
 Value vpsa_type() const { return {}; }
 Value vpsa_message() const { return {}; }
};
struct SessionData { int history(int id) { return id; } };
struct Session { SessionData value; SessionData &data() { return value; } };
int peerFromMTP(int id) { return id; }
int CustomFromTL(Session*, int value) { return value; }
struct LuxurySettings {
 bool disable = true;
 bool disableAds() const { return disable; }
 static LuxurySettings &getInstance() { static LuxurySettings result; return result; }
};
struct Date { int day() const { return 8; } };
struct QDate { static Date currentDate() { return {}; } };
struct Signal { int count = 0; void fire(std::initializer_list<int>) { ++count; } };
struct Suggestions {
 Session session;
 Session *_session = &session;
 std::optional<int> _custom;
 int _contactBirthdaysLastDayRequest = -1;
 Signal _refreshed;
 int promoted = 0;
 void setTopPromoted(int peer, int, int) { promoted = peer; }
 void setTopPromoted(std::nullptr_t, QString, QString) { promoted = 0; }
 void response(const Data &data, bool changedPendingSuggestions = false, bool changedDismissedSuggestions = false) {
RESPONSE_HERE
 }
};
int main() {
 for (bool disable : {false, true}) {
  LuxurySettings::getInstance().disable = disable;
  Suggestions state;
  Data data;
  state.response(data, true);
  assert(state._custom == 4 && state._refreshed.count == 1 && state.promoted == (disable ? 0 : 7));
  state.response(data); assert(state._refreshed.count == 1);
  data.custom = 9; state.response(data); assert(state._custom == 9 && state._refreshed.count == 2);
  data.custom.reset(); state.response(data); assert(!state._custom && state._refreshed.count == 3);
  state.response(data, false, true); assert(state._refreshed.count == 4);
  state._contactBirthdaysLastDayRequest = 7; state.response(data); assert(state._refreshed.count == 5);
  data.peer.reset(); state._contactBirthdaysLastDayRequest = 8; state.response(data); assert(state.promoted == 0 && state._refreshed.count == 5);
 }
 std::cout << "PASS14: actual ad/custom/notification response slice on synthetic MTP/Qt/session, native UI pending\n";
}
'''.replace('RESPONSE_HERE', response)
parser = argparse.ArgumentParser()
parser.add_argument('--cxx', default=os.environ.get('CXX') or shutil.which('g++') or r'C:\Strawberry\c\bin\g++.exe')
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix='luxury-promo-') as directory:
    work = Path(directory); test = work / 'promo.cpp'; test.write_text(cpp, encoding='utf-8')
    exe = work / ('promo.exe' if os.name == 'nt' else 'promo')
    subprocess.run([args.cxx, '-std=c++20', '-O0', '-D_DEBUG', str(test), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
