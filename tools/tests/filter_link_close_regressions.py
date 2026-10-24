"""Execute the chat-folder link header constructor body; no folder join, chat list or network actions."""
import sys
from pathlib import Path
import tempfile
from library_regressions import cpp, source


OWNER = 'Telegram/SourceFiles/ui/controls/filter_link_header.cpp'
INIT = '_horizontalFilters(descriptor.horizontalFilters) {\n'


def check(root, path=OWNER):
    text = source(path).replace('\r\n', '\n')
    start = text.index(INIT, text.index('Widget::Widget(\n\tnot_null<QWidget*> parent,\n\tFilterLinkHeaderDescriptor &&descriptor)')) + len(INIT)
    body = text[start:text.index('\n}\n', start)]
    if body.count('_close->setAccessibleName(tr::lng_close(tr::now));') != 1:
        raise AssertionError('RED: chat-folder link header close icon has no localized accessible name')
    assert ', _close(CreateChild<IconButton>(this, st::boxTitleClose))' in text
    cpp(root, 'filter_link_close', r'''
#include <cassert>
#include <functional>
#include <iostream>
#include <string>
using QString=std::string;namespace tr{constexpr int now=0;QString lng_close(int){return "Close";}}
struct QPoint{int x()const{return 1;}int y()const{return 2;}};
namespace st{constexpr int boxTitleHeight=10;const QPoint boxTitlePosition;}
struct Lifetime{};struct Producer{};
namespace rpl{template<class F>struct OnNext{F f;};template<class F>OnNext<F> on_next(F f,Lifetime&){return {f};}}
template<class F>void operator|(Producer,rpl::OnNext<F>){}
struct Badge{Producer changes(){return {};}};
struct IconButton{QString name;void setAccessibleName(QString s){name=s;}};
struct PreviewState{};
struct Widget{
IconButton*_close=new IconButton;Badge _badge;PreviewState _preview;Lifetime life;int minHeight=0,refreshes=0,updates=0,tx=0,ty=0;
void setMinimumHeight(int h){minHeight=h;}void refreshTitleText(){++refreshes;}void setTitlePosition(int x,int y){tx=x;ty=y;}
void update(){++updates;}Lifetime&lifetime(){return life;}
Widget(){
''' + body + r'''
}};
int main(){Widget w;assert(w._close->name=="Close");assert(w.minHeight==10&&w.refreshes==1&&w.tx==1&&w.ty==2&&w.updates==0);std::cout<<"Actual filter link header close PASS: localized name, title/height setup unchanged, no folder join/chat list/network\n";}
''')


if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='luxury-filter-link-close-') as directory:
        try:
            check(Path(directory), *sys.argv[1:2])
        except AssertionError as error:
            print(error)
            raise SystemExit(1)
