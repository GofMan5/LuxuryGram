"""Extract attachment bounds predicate and saved music action with weak owners.
Qt/event rendering and session/media are doubles; real bundled base weak is used.
"""
from pathlib import Path
import os, shutil, subprocess, tempfile
ROOT = Path(__file__).resolve().parents[2]
def read(p): return (ROOT / 'Telegram/SourceFiles' / p).read_text('utf-8')
files = read('boxes/send_files_box.cpp')
predicate = files[files.index('bool SendFilesBox::isFileBlock('):files.index('void SendFilesBox::moveFile(')]
assert files.count('toInt(&valid)') == 2 and files.count('if (valid && isFileBlock(from)') == 2
music = read('info/saved/info_saved_music_common.cpp')
action_start = music.index('LuxurySettings::getInstance().setAdaptiveCoverColor')
action = music[action_start:music.index('DocumentMusicButtonData(documentRefreshed, current));',action_start) + len('DocumentMusicButtonData(documentRefreshed, current));')]
assert 'new Ui::PopupMenu(\n' in music and 'musicButton->entity(),\n' in music[music.index('new Ui::PopupMenu('):]
assert 'item->media()' not in action and 'musicButton->entity()->updateData' not in action
weak = (ROOT / 'Telegram/lib_base/base/weak_ptr.h').read_text('utf-8').replace('#include "base/basic_types.h"','#include <gsl/gsl>')
cpp = r'''
#include <array>
#include <atomic>
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <utility>
#include <vector>
WEAK_HERE
namespace base::assertion { void log(const char*,const char*,int) { std::abort(); } }
namespace Ui { struct PreparedFile { enum class Type { File, Music, None, Photo, Video }; Type type; }; }
struct SendFilesBox { struct List { std::vector<Ui::PreparedFile> files; } _list; struct Way { bool photos=false; bool sendImagesAsPhotos() const { return photos; } }; struct Value { Way way; const Way &current() const { return way; } } _sendWay; bool isFileBlock(int) const; };
PREDICATE_HERE
struct Document {} document;
struct Media { Document *document() { return &::document; } } media;
struct Item { Media *media() { return &::media; } } item;
struct Owner : base::has_weak_ptr { struct Data { Item *value=&::item; Item *message(int) { return value; } } d; Data &data() { return d; } };
struct Button : base::has_weak_ptr { int updates=0; Button *entity() { return this; } void updateData(int) { ++updates; } };
struct LuxurySettings { bool color=false; static LuxurySettings &getInstance() { static LuxurySettings s; return s; } bool adaptiveCoverColor() const { return color; } void setAdaptiveCoverColor(bool v) { color=v; } };
int DocumentMusicButtonData(Document*,Item*) { return 1; }
int main() {
 SendFilesBox box; box._list.files = {{Ui::PreparedFile::Type::File},{Ui::PreparedFile::Type::Music},{Ui::PreparedFile::Type::None},{Ui::PreparedFile::Type::Photo},{Ui::PreparedFile::Type::Video}};
 for(int index : {-100,-1,5,2147483647}) assert(!box.isFileBlock(index)); for(int i=0;i!=4;++i) assert(box.isFileBlock(i)); assert(!box.isFileBlock(4)); box._sendWay.way.photos=true; assert(!box.isFileBlock(3));
 auto owner=std::make_unique<Owner>(); auto row=std::make_unique<Button>(); const auto session=base::make_weak(owner.get()); const auto button=base::make_weak(row.get()); const auto itemId=42;
 auto callback=[=] { ACTION_HERE };
 callback(); assert(row->updates==1); owner->d.value=nullptr; callback(); assert(row->updates==1); owner.reset(); callback(); assert(row->updates==1); row.reset(); callback();
 std::cout << "PASS: actual attachment bounds and saved-music action with real weak owners; Qt/event/session doubles\n";
}
'''
for marker,text in {'WEAK_HERE':weak,'PREDICATE_HERE':predicate,'ACTION_HERE':action}.items(): cpp=cpp.replace(marker,text)
with tempfile.TemporaryDirectory(prefix='luxury-attachment-music-') as directory:
    work=Path(directory); source=work/'check.cpp'; source.write_text(cpp,encoding='utf-8'); exe=work/('check.exe' if os.name=='nt' else 'check')
    subprocess.run([os.environ.get('CXX') or shutil.which('g++') or r'C:\Strawberry\c\bin\g++.exe','-std=c++20','-O0','-D_DEBUG','-I',str(ROOT/'Telegram/ThirdParty/GSL/include'),str(source),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
