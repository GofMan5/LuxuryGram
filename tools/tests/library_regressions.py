"""Offline library regressions: extracted production logic, not native Qt QA.

Uses actual CMake embedding and xxHash; Qt/file/queue doubles only for narrow
control-flow predicates. All generated artifacts live in a temporary directory.
"""
from pathlib import Path
import os
import re
import shutil
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
CXX = os.environ.get('CXX', r'C:\Strawberry\c\bin\g++.exe')
# A fixture links its runtime in: an older libstdc++-6.dll earlier in PATH
# (Git's mingw64) would otherwise fail it with 0xC0000139.
STATIC = ('-static',)
RUNTIME_DLLS = ('libstdc++', 'libgcc', 'libwinpthread')


def standalone(exe):
    # Fails a fixture that would load the compiler runtime from PATH.
    if os.name != 'nt':
        return
    objdump = Path(CXX).with_name('objdump.exe')
    imports = run([str(objdump), '-p', str(exe)], Path(exe).parent).lower()
    loaded = [dll for dll in RUNTIME_DLLS if 'dll name: ' + dll in imports]
    assert not loaded, f'{exe} imports {loaded}: link it with STATIC'


def source(path):
    return (ROOT / path).read_text(encoding='utf-8')


def function(text, signature):
    start = text.index(signature)
    opening = text.index('{', start)
    level = 1
    i = opening + 1
    while level:
        if text[i] == '{':
            level += 1
        elif text[i] == '}':
            level -= 1
        i += 1
    return text[start:i]


def run(args, cwd, expected=0):
    result = subprocess.run(args, cwd=cwd, capture_output=True, text=True, encoding='utf-8', errors='replace')
    if result.returncode != expected:
        raise AssertionError(f'{args}: exit {result.returncode}\n{result.stdout}{result.stderr}')
    return result.stdout + result.stderr


def cpp(root, name, text, extra=()):
    path = root / (name + '.cpp')
    path.write_text(text, encoding='utf-8')
    exe = root / (name + ('.exe' if os.name == 'nt' else ''))
    run([CXX, '-std=c++20', '-O0', '-g', '-D_DEBUG', str(path), *extra, *STATIC, '-o', str(exe)], root)
    standalone(exe)
    print(run([str(exe)], root).strip())


def embedding(root):
    cmake = shutil.which('cmake')
    assert cmake, 'CMake unavailable'
    text = source('Telegram/lib_icu/CMakeLists.txt')
    embed = text[text.index('function(embed_binary_data'):text.index('endfunction()', text.index('function(embed_binary_data'))+len('endfunction()')]
    project = root / 'embed'
    project.mkdir()
    data = bytes(range(256)) + bytes.fromhex('00abcdff')
    (project / 'data.dat').write_bytes(data)
    (project / 'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.25)\nproject(Embed NONE)\n'+embed+'\nadd_library(probe STATIC IMPORTED)\nembed_binary_data(probe probe_data "${CMAKE_CURRENT_SOURCE_DIR}/data.dat")\n', encoding='utf-8')
    # Replace imported probe: target_sources PRIVATE accepts no imported sources.
    p = project / 'CMakeLists.txt'
    p.write_text(p.read_text().replace('add_library(probe STATIC IMPORTED)', 'add_custom_target(probe)\nfunction(target_sources)\nendfunction()\nfunction(target_include_directories)\nendfunction()'), encoding='utf-8')
    build = project / 'out'
    run([cmake, '-S', str(project), '-B', str(build)], root)
    generated = build / 'generated/probe_data.cpp'
    content = generated.read_text()
    values = bytes(int(h,16) for h in re.findall(r'0x([0-9a-fA-F]{2}),', content))
    assert values == data
    assert 'alignas(16)' in content
    stamp = generated.stat().st_mtime_ns
    run([cmake, '-S', str(project), '-B', str(build)], root)
    assert generated.stat().st_mtime_ns == stamp, 'unchanged generated file was rewritten'
    cpp(root, 'icu_generated', '#include <cassert>\n#include <cstdint>\n#include "'+generated.as_posix()+'"\nint main(){assert(probe_data_size==260);assert(probe_data[257]==0xab);assert(reinterpret_cast<uintptr_t>(probe_data)%16==0);}', ())
    (project / 'data.dat').write_bytes(b'')
    result = subprocess.run([cmake, '-S', str(project), '-B', str(build)], cwd=root, capture_output=True, text=True)
    assert result.returncode != 0 and 'Cannot embed empty ICU data' in result.stderr
    # Real data transformation byte-for-byte; no app or ICU model build.
    real = ROOT / 'Telegram/lib_icu/data/icudt78l.dat'
    (project / 'data.dat').write_bytes(real.read_bytes())
    started = time.perf_counter()
    run([cmake, '-S', str(project), '-B', str(build)], root)
    elapsed = time.perf_counter()-started
    values = bytes(int(h,16) for h in re.findall(r'0x([0-9a-fA-F]{2}),', generated.read_text()))
    assert values == real.read_bytes()
    print(f'ICU embedding PASS: {len(values)} real bytes, alignment/control/no-rewrite/empty rejection; configure {elapsed:.3f}s (not app benchmark)')


def lexer(root):
    text = source('Telegram/codegen/codegen/common/clean_file.cpp')
    start = text.index('auto escaped = false;')
    guard = text[start:text.index('if (!escaped)', start)]
    cpp(root, 'lexer', r'''#include <cassert>
#include <iostream>
#include <string>
bool escaped(const char *begin,const char *ch){'''+guard+r'''return escaped;}
int main(){
for(char prefix: {'x','\\'}) { const char data[]={prefix,'\\','"'};assert(escaped(data+1,data+2)); }
const char emptyQuote[]={'"'};assert(!escaped(emptyQuote,emptyQuote));
const char two[]={'\\','\\','"'};assert(!escaped(two,two+2));
const char regular[]={'x','"'};assert(!escaped(regular,regular+1));
for(int count=0;count!=10;++count){std::string s(count,'\\');s+='"';assert(escaped(s.data(),s.data()+count)==bool(count%2));}
std::cout<<"Lexer boundary/parity PASS: 5 boundary controls and 0-9 backslash runs\n";
}
''')


def lexer_comments(root):
    text=source('Telegram/codegen/codegen/common/clean_file.cpp')
    cpp(root,'lexer_comments',r'''#include <cassert>
#include <iostream>
#include <string>
#include <vector>
using QString=std::string;
struct QByteArray{std::string bytes;QByteArray()=default;QByteArray(std::string s):bytes(std::move(s)){}QByteArray(const char*p,long n):bytes(p,n){}const char*cbegin()const{return bytes.data();}const char*cend()const{return bytes.data()+bytes.size();}bool isEmpty()const{return bytes.empty();}void reserve(long n){if(n>0)bytes.reserve(n);}void append(const char*p,long n){bytes.append(p,n);}void append(char c){bytes.push_back(c);}};
struct QFileInfo{QString s;QFileInfo(QString p):s(p){}QString absoluteFilePath()const{return s;}};
bool readFile(const QString&,QByteArray*){assert(false);return false;}constexpr int kErrorUnexpectedEndOfFile=1;
namespace common{void logError(int,const QString&){} }
struct CleanFile{bool read_=false;QString filepath_;QByteArray content_,result_;std::vector<QByteArray> singleLineComments_;explicit CleanFile(const std::string&s):content_(s){}bool read();};
'''+function(text,'bool CleanFile::read()')+r'''
int main(){
for(int slashes=0;slashes!=10;++slashes)for(auto marker:{std::string("//kept"),std::string("/*kept*/")}){
const auto quoted=std::string("value: \"a")+std::string(slashes,'\\')+"\""+(slashes%2?marker+"\"":"")+"; ";
CleanFile f(quoted+"//strip\nnext: 1;\n");assert(f.read());assert(f.result_.bytes==quoted+" \nnext: 1;\n");}
std::cout<<"Full extracted CleanFile PASS: slash runs 0-9, in-string // and /* */, real trailing comments (Qt value doubles)\n";
}
''')


def highlight(root):
    text = source('Telegram/lib_spellcheck/spellcheck/spellcheck_highlight_syntax.cpp')
    body = text[text.index('XXH64_reset(state, 0);'):text.index('const auto hash = XXH64_digest(state);')+len('const auto hash = XXH64_digest(state);')]
    xx = ROOT / 'Telegram/ThirdParty/xxHash'
    cpp(root, 'highlight', r'''#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>
#define XXH_STATIC_LINKING_ONLY
#define XXH_IMPLEMENTATION
#include "xxhash.h"
using uint64=uint64_t;using ushort=char16_t;
uint64_t hash(const std::u16string &view,const std::u16string &language){
XXH64_state_t S;auto state=&S;
'''+body+r'''
return hash;
}
int main(){
assert(hash(u"as",u"css")!=hash(u"a",u"scss"));
assert(hash(u"",u"scss")!=hash(u"s",u"css"));
assert(hash(u"a",u"css")==hash(u"a",u"css"));
assert(hash(u"",u"")!=hash(u"x",u""));
std::cout<<"Highlight actual xxHash PASS: ambiguous pairs separated, stable/empty controls\n";
}
''', ['-I', str(xx)])


def compactor(root):
    text = source('Telegram/lib_storage/storage/cache/storage_cache_compactor.cpp')
    body = function(text, 'void CompactorObject::start()')
    cpp(root, 'compactor', r'''#include <cassert>
#include <iostream>
struct MultiStore{};struct MultiStoreWithTime{};
struct CompactorObject {
struct { bool trackEstimatedTime=false; } _settings;
int failAt=0, steps=0, failed=0, parsed=0, inited=0;
bool openBinlog(){++steps;return failAt!=1;}bool readHeader(){++steps;return failAt!=2;}bool openCompact(){++steps;return failAt!=3;}
void fail(){++failed;}template<class T>void initList(){++inited;}void parseChunk(){++parsed;}void start();
};
'''+body+r'''
int main(){for(int n=1;n<=3;++n){CompactorObject c;c.failAt=n;c.start();assert(c.failed==1&&c.parsed==0&&c.inited==0&&c.steps==n);}
for(bool tracking:{false,true}){CompactorObject c;c._settings.trackEstimatedTime=tracking;c.start();assert(c.failed==0&&c.parsed==1&&c.inited==1);}
std::cout<<"Compactor extracted startup PASS: 3 failures and 2 healthy paths\n";}
''')


def move(root):
    text = source('Telegram/lib_storage/storage/storage_encrypted_file.cpp')
    body = function(text, 'bool File::Move(')
    cpp(root, 'move', r'''#include <cassert>
#include <iostream>
#include <map>
#include <string>
using QString=std::string;
std::map<std::string,std::string> files;bool lockOk=true,renameOk=true;
namespace QIODevice{enum OpenMode{WriteOnly,ReadWrite};}
struct QFile{std::string path;QFile(std::string p):path(p){}bool exists(){return files.contains(path);}void close(){} };
namespace base{struct FileLock{bool lock(QFile &file,QIODevice::OpenMode mode){assert(mode==QIODevice::ReadWrite);return lockOk;}};
namespace Platform{bool RenameWithOverwrite(const QString &from,const QString &to){if(!renameOk)return false;files[to]=files.at(from);files.erase(from);return true;}}}
struct File{static bool Move(const QString &from,const QString &to);};
'''+body+r'''
int main(){
files={{"from","new"},{"to","old"}};lockOk=false;assert(!File::Move("from","to"));assert(files.at("to")=="old");
lockOk=true;renameOk=false;assert(!File::Move("from","to"));assert(files.at("to")=="old"&&files.at("from")=="new");
renameOk=true;assert(File::Move("from","to"));assert(files.at("to")=="new"&&!files.contains("from"));
assert(!File::Move("missing","to"));
std::cout<<"Cache move extracted routing PASS: non-truncating lock, failure-preserving native helper, success/missing source (not OS-lock QA)\n";
}
''')


def main():
    os.environ['PATH']=str(Path(CXX).parent)+os.pathsep+os.environ.get('PATH','')
    with tempfile.TemporaryDirectory(prefix='luxury-libs-') as temp:
        root=Path(temp)
        embedding(root)
        lexer(root)
        lexer_comments(root)
        highlight(root)
        compactor(root)
        move(root)
    print('Library focused regressions PASS; NOT full Qt/platform/library fuzz QA')


if __name__ == '__main__':
    main()
