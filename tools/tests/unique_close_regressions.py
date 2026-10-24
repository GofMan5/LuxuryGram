"""Execute the shared star-gift AddUniqueCloseButton helper; no gift/payment/network actions."""
import sys
from pathlib import Path
import tempfile
from library_regressions import cpp, function, source


OWNER = 'Telegram/SourceFiles/ui/boxes/about_cocoon_box.cpp'


def check(root, path=OWNER):
    body = function(source(path), 'void AddUniqueCloseButton(')
    if body.count('close->setAccessibleName(tr::lng_close(tr::now));') != 1:
        raise AssertionError('RED: shared unique-gift close icon has no localized accessible name')
    assert 'st::uniqueCloseButton' in body
    cpp(root, 'unique_close', r'''
#include <cassert>
#include <functional>
#include <iostream>
#include <string>
using QString=std::string;namespace tr{constexpr int now=0;QString lng_close(int){return "Close";}}
namespace st{constexpr int uniqueCloseButton=1;}
struct Lifetime{};struct Width{};
namespace rpl{template<class F>struct OnNext{F f;};template<class F>OnNext<F> on_next(F f,Lifetime&){return {f};}}
template<class F>void operator|(Width,rpl::OnNext<F> n){n.f(320);}
struct GenericBox{int closes=0;void closeBox(){++closes;}Width widthValue(){return {};}};
template<class T>struct not_null{T p;T operator->()const{return p;}operator T()const{return p;}};
namespace Ui{struct IconButton{QString name;std::function<void()>callback;Lifetime life;int right=-1,shown=0;IconButton(GenericBox*,int){}void setAccessibleName(QString s){name=s;}void setClickedCallback(std::function<void()>f){callback=f;}void show(){++shown;}void raise(){}void moveToRight(int x,int,int){right=x;}Lifetime&lifetime(){return life;}};}Ui::IconButton*made=nullptr;namespace Ui{template<class T>T*CreateChild(GenericBox*p,int s){return made=new T(p,s);}}
using Ui::IconButton;
''' + body + r'''
int main(){GenericBox box;AddUniqueCloseButton({&box});assert(made&&made->name=="Close");assert(made->shown==1&&made->right==0);assert(box.closes==0);made->callback();assert(box.closes==1);std::cout<<"Actual unique-gift close PASS: localized name, click closes the box once, no gift/payment/network\n";}
''')
    # Callers only reach the button through the helper, so the name covers them all.
    assert source('Telegram/SourceFiles/ui/boxes/about_cocoon_box.h').count(
        'void AddUniqueCloseButton(not_null<GenericBox*> box);') == 1


if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='luxury-unique-close-') as directory:
        try:
            check(Path(directory), *sys.argv[1:2])
        except AssertionError as error:
            print(error)
            raise SystemExit(1)
