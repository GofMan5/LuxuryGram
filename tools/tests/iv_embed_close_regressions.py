"""Execute the IV markdown embed overlay close icon setup; no webview, embed loading or network actions."""
import sys
from pathlib import Path
import tempfile
from library_regressions import cpp, source


OWNER = 'Telegram/SourceFiles/iv/markdown/iv_markdown_embed_overlay.cpp'
END = '_close->hide();'


def check(root, path=OWNER):
    text = source(path).replace('\r\n', '\n')
    start = text.index('_close = Ui::CreateChild<Ui::IconButton>(this, st::markdownEmbedOverlayClose);', text.index('EmbedOverlay::EmbedOverlay('))
    block = text[start:text.index(END, start) + len(END)]
    if block.count('_close->setAccessibleName(tr::lng_close(tr::now));') != 1:
        raise AssertionError('RED: IV embed overlay close icon has no localized accessible name')
    assert 'nativeIvEmbedOverlayClose' in block
    cpp(root, 'iv_embed_close', r'''
#include <cassert>
#include <functional>
#include <iostream>
#include <string>
using QString=std::string;namespace tr{constexpr int now=0;QString lng_close(int){return "Close";}}
inline QString operator""_q(const char*s,std::size_t n){return QString(s,n);}
namespace st{constexpr int markdownEmbedOverlayClose=1;}
struct Widget{};
namespace Ui{struct IconButton{QString name,object;bool hidden=false;std::function<void()>callback;IconButton(Widget*,int){}void setAccessibleName(QString s){name=s;}void setObjectName(QString s){object=s;}void setClickedCallback(std::function<void()>f){callback=f;}void hide(){hidden=true;}};template<class T>T*CreateChild(Widget*p,int s){return new T(p,s);}}
struct Overlay:Widget{Ui::IconButton*_close=nullptr;int closes=0;void closeEmbed(){++closes;}
void build(){
''' + block.replace('u"nativeIvEmbedOverlayClose"_q', '"nativeIvEmbedOverlayClose"_q') + r'''
}};
int main(){Overlay o;o.build();assert(o._close->name=="Close"&&o._close->object=="nativeIvEmbedOverlayClose"&&o._close->hidden);assert(o.closes==0);o._close->callback();assert(o.closes==1);std::cout<<"Actual IV embed overlay close PASS: localized name, object name and hidden state kept, click closes the embed once, no webview/embed/network\n";}
''')


if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='luxury-iv-embed-close-') as directory:
        try:
            check(Path(directory), *sys.argv[1:2])
        except AssertionError as error:
            print(error)
            raise SystemExit(1)
