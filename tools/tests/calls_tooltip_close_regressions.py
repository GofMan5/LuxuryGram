"""Execute the group-call sticked tooltip close icon block; no call, media or network actions."""
import sys
from pathlib import Path
import tempfile
from library_regressions import cpp, source


OWNER = 'Telegram/SourceFiles/calls/group/calls_group_panel.cpp'
END = '_stickedTooltipClose = button;'


def check(root, path=OWNER):
    text = source(path).replace('\r\n', '\n')
    start = text.index('const auto button = Ui::CreateChild<Ui::IconButton>(\n\t\t\tcontainer,\n\t\t\tst::groupCallStickedTooltipClose);')
    block = text[start:text.index(END, start) + len(END)]
    if block.count('button->setAccessibleName(tr::lng_close(tr::now));') != 1:
        raise AssertionError('RED: group-call sticked tooltip close icon has no localized accessible name')
    cpp(root, 'calls_tooltip_close', r'''
#include <algorithm>
#include <cassert>
#include <functional>
#include <iostream>
#include <string>
using QString=std::string;namespace tr{constexpr int now=0;QString lng_close(int){return "Close";}}
namespace st{constexpr int groupCallStickedTooltipClose=1;}
struct QSize{int w,h;int width()const{return w;}int height()const{return h;}};
struct Lifetime{};struct Producer{QSize s;};
struct Widget{QSize size{0,0};int x=0,y=0;Lifetime life;Producer sizeValue(){return {size};}void resize(int w,int h){size={w,h};}void move(int a,int b){x=a;y=b;}Lifetime&lifetime(){return life;}};
struct Pair{QSize a,b;};
namespace rpl{inline Pair combine(Producer a,Producer b){return {a.s,b.s};}template<class F>struct OnNext{F f;};template<class F>OnNext<F> on_next(F f,Lifetime&){return {f};}}
template<class F>void operator|(Pair p,rpl::OnNext<F> n){n.f(p.a,p.b);}
enum class StickedTooltipHide{Unavailable,Activated,Discarded};
namespace Ui{struct IconButton:Widget{QString name;std::function<void()>callback;IconButton(Widget*,int){size={20,20};}void setAccessibleName(QString s){name=s;}void setClickedCallback(std::function<void()>f){callback=f;}};template<class T>T*CreateChild(Widget*p,int s){return new T(p,s);}}
struct Panel{Ui::IconButton*_stickedTooltipClose=nullptr;int discarded=0;
void hideStickedTooltip(StickedTooltipHide h){if(h==StickedTooltipHide::Discarded)++discarded;}
void build(Widget*container,Widget*label){
''' + block + r'''
}};
int main(){Panel p;Widget container,label;label.size={100,10};p.build(&container,&label);auto b=p._stickedTooltipClose;assert(b&&b->name=="Close");assert(container.size.width()==120&&container.size.height()==20&&b->x==100);assert(p.discarded==0);b->callback();assert(p.discarded==1);std::cout<<"Actual group-call sticked tooltip close PASS: localized name, layout unchanged, click discards the tooltip once, no call/media/network\n";}
''')


if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='luxury-calls-tooltip-close-') as directory:
        try:
            check(Path(directory), *sys.argv[1:2])
        except AssertionError as error:
            print(error)
            raise SystemExit(1)
