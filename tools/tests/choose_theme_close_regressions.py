"""Execute the chat-theme chooser close icon construction; no theme apply/network actions."""
import sys
from pathlib import Path
import tempfile
from library_regressions import cpp, function, source


OWNER = 'Telegram/SourceFiles/ui/chat/choose_theme_controller.cpp'


def check(root, path=OWNER):
    body = function(source(path), 'void ChooseThemeController::init(')
    start = body.index('const auto close = Ui::CreateChild<Ui::IconButton>(')
    block = body[start:body.index('initButtons();', start)]
    if block.count('close->setAccessibleName(tr::lng_close(tr::now));') != 1:
        raise AssertionError('RED: chat-theme chooser close icon has no localized accessible name')
    assert 'st::boxTitleClose' in block and 'this->close();' in block
    cpp(root, 'choose_theme_close', r'''
#include <cassert>
#include <functional>
#include <iostream>
#include <string>
using QString=std::string;namespace tr{constexpr int now=0;QString lng_close(int){return "Close";}}
namespace st{constexpr int boxTitleClose=1;}
struct QPoint{};struct Lifetime{};struct Value{};
struct Wrap{Value widthValue(){return {};}};struct Title{Value positionValue(){return {};}};
template<class T>struct Owner{T *p;T*get()const{return p;}T*operator->()const{return p;}};
namespace rpl{struct Combined{};inline Combined combine(Value,Value){return {};}template<class F>struct OnNext{F f;};template<class F>OnNext<F> on_next(F f,Lifetime&){return {f};}}
template<class F>void operator|(rpl::Combined,rpl::OnNext<F> n){n.f(320,QPoint());}
namespace Ui{struct IconButton{QString name;std::function<void()>callback;Lifetime life;int right=-1;IconButton(Wrap*,int){}void setAccessibleName(QString s){name=s;}void setClickedCallback(std::function<void()>f){callback=f;}void moveToRight(int x,int,int){right=x;}Lifetime&lifetime(){return life;}};template<class T>T*CreateChild(Wrap*p,int s){return new T(p,s);}}
struct Controller{Wrap wrapValue;Title titleValue;Owner<Wrap> _wrap{&wrapValue};Title*titleWrap=&titleValue;int closes=0;void close(){++closes;}
Ui::IconButton*build(){
''' + block + r'''
return close;}};
int main(){Controller c;auto close=c.build();assert(close->name=="Close"&&close->right==0);assert(c.closes==0);close->callback();assert(c.closes==1);std::cout<<"Actual chat-theme close PASS: localized name, click closes the chooser once, no theme apply/network\n";}
''')


if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='luxury-theme-close-') as directory:
        try:
            check(Path(directory), *sys.argv[1:2])
        except AssertionError as error:
            print(error)
            raise SystemExit(1)
