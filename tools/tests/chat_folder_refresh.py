"""Execute actual hidden-All folder selection fallback on synthetic menu/session.
python tools/tests/chat_folder_refresh.py [--cxx g++]
Not Qt focus/scroll/accessibility rendering; their source wiring remains unchanged.
"""
from pathlib import Path
import argparse
import os
import shutil
import subprocess
import tempfile
root = Path(__file__).resolve().parents[2]
source = (root / 'Telegram/SourceFiles/window/window_filters_menu.cpp').read_text('utf-8')
refresh = source[source.index('void FiltersMenu::refresh()'):source.index('void FiltersMenu::setupList()')]
start = refresh.index('\tif (settings.hideAllChatsFolder()')
end = refresh.index('\n\t// Re-establish', start)
fallback = refresh[start:end]
assert refresh.index('_filters = std::move(now);') < start < refresh.index('setListTabStop(')
assert refresh.count('setActiveChatsFilter(filters->lookupId(0));') == 1
assert 'if (nextIsLocked && (currentFilter == filter.id()))' in refresh
assert 'focusedId' in refresh and 'refocus->setFocus();' in refresh and 'scrollToButton(refocus);' in refresh
cpp = r'''
#include <cassert>
#include <iostream>
#include <set>
#include <vector>
struct Settings { bool hidden; bool hideAllChatsFolder() const { return hidden; } };
struct Widget { bool content; bool sessionContent() const { return content; } };
struct Session {
 int active = 0, calls = 0; Widget view; bool primary = true;
 Widget *widget() { return &view; }
 int activeChatsFilterCurrent() const { return active; }
 void setActiveChatsFilter(int id) { if(primary) { active = id; ++calls; } }
};
struct Filters { std::vector<int> ids; int lookupId(int index) const { return index < ids.size() ? ids[index] : 0; } };
void apply(Session *_session, const Settings &settings, Filters *filters, const std::set<int> &_filters) {
FALLBACK_HERE
}
int main() {
 Filters filters{{11, 22}};
 for(bool hidden : {false, true}) for(bool content : {false, true}) for(bool primary : {false, true}) {
  Session session{22, 0, {content}, primary};
  apply(&session, {hidden}, &filters, {11, 22});
  assert(session.active == 22 && !session.calls); // Rename/Premium/reorder with valid current folder.
  session.active = 33; apply(&session, {hidden}, &filters, {11, 22});
  assert(session.active == (hidden && content && primary ? 11 : 33));
 }
 Session session{0, 0, {true}, true}; apply(&session, {true}, &filters, {11, 22}); assert(session.active == 11);
 session = {0, 0, {true}, true}; apply(&session, {true}, &filters, {0, 11, 22}); assert(session.active == 0 && !session.calls);
 session = {0, 0, {true}, true}; apply(&session, {false}, &filters, {0, 11, 22}); assert(session.active == 0);
 std::cout << "PASS: hidden-All refresh preserves valid current selection and falls back only when invalid; synthetic widgets\n";
}
'''.replace('FALLBACK_HERE', fallback)
parser = argparse.ArgumentParser()
parser.add_argument('--cxx', default=os.environ.get('CXX') or shutil.which('g++') or r'C:\Strawberry\c\bin\g++.exe')
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix='luxury-folder-') as directory:
    work = Path(directory); test = work / 'folder.cpp'; test.write_text(cpp, encoding='utf-8')
    exe = work / ('folder.exe' if os.name == 'nt' else 'folder')
    subprocess.run([args.cxx, '-std=c++20', '-O0', '-D_DEBUG', str(test), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
