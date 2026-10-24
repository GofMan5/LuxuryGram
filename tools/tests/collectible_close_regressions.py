"""Execute the collectible (Fragment username/number) info box close icon; no copy/link/network actions."""
import sys
from pathlib import Path
import tempfile
from library_regressions import cpp, function, source


OWNER = 'Telegram/SourceFiles/ui/boxes/collectible_info_box.cpp'


def check(root, path=OWNER):
    body = function(source(path), 'void CollectibleInfoBox(')
    start = body.index('const auto close = Ui::CreateChild<Ui::IconButton>(')
    block = body[start:body.index('box->widthValue(', start)]
    if block.count('close->setAccessibleName(tr::lng_close(tr::now));') != 1:
        raise AssertionError('RED: collectible info box close icon has no localized accessible name')
    assert 'st::boxTitleClose' in block and 'buttonsParent,' in block
    cpp(root, 'collectible_close', r'''
#include <cassert>
#include <functional>
#include <iostream>
#include <string>
using QString=std::string;namespace tr{constexpr int now=0;QString lng_close(int){return "Close";}}
namespace st{constexpr int boxTitleClose=1;}
struct Widget{};struct Box{int closes=0;void closeBox(){++closes;}};
namespace Ui{struct IconButton{QString name;std::function<void()>callback;IconButton(Widget*,int){}void setAccessibleName(QString s){name=s;}void setClickedCallback(std::function<void()>f){callback=f;}};template<class T>T*CreateChild(Widget*p,int s){return new T(p,s);}}
Ui::IconButton*build(Box*box,Widget*buttonsParent){
''' + block + r'''
return close;}
int main(){Box box;Widget parent;auto close=build(&box,&parent);assert(close->name=="Close");assert(box.closes==0);close->callback();assert(box.closes==1);std::cout<<"Actual collectible close PASS: localized name, click closes the box once, no copy/link/network\n";}
''')


if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='luxury-collectible-close-') as directory:
        try:
            check(Path(directory), *sys.argv[1:2])
        except AssertionError as error:
            print(error)
            raise SystemExit(1)
