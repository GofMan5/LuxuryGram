"""Execute the giveaway/boost premium top-bar close icon; no giveaway, payment or network actions."""
import sys
from pathlib import Path
import tempfile
from library_regressions import cpp, source


OWNER = 'Telegram/SourceFiles/info/channel_statistics/boosts/create_giveaway_box.cpp'


def check(root, path=OWNER):
    text = source(path).replace('\r\n', '\n')
    start = text.index('const auto close = Ui::CreateChild<Ui::IconButton>(', text.index('void AddPremiumTopBarWithDefaultTitleBar('))
    block = text[start:text.index('closeTopBar->widthValue(', start)]
    if block.count('close->setAccessibleName(tr::lng_close(tr::now));') != 1:
        raise AssertionError('RED: giveaway premium top-bar close icon has no localized accessible name')
    assert 'st::startGiveawayBoxTitleClose' in block and 'closeTopBar.get(),' in block
    cpp(root, 'giveaway_close', r'''
#include <cassert>
#include <functional>
#include <iostream>
#include <string>
using QString=std::string;namespace tr{constexpr int now=0;QString lng_close(int){return "Close";}}
namespace st{constexpr int startGiveawayBoxTitleClose=1;}
struct Widget{};struct Bar{Widget w;Widget*get(){return &w;}};struct Box{int closes=0;void closeBox(){++closes;}};
namespace Ui{struct IconButton{QString name;std::function<void()>callback;IconButton(Widget*,int){}void setAccessibleName(QString s){name=s;}void setClickedCallback(std::function<void()>f){callback=f;}};template<class T>T*CreateChild(Widget*p,int s){return new T(p,s);}}
Ui::IconButton*build(Box*box,Bar&closeTopBar){
''' + block + r'''
return close;}
int main(){Box box;Bar bar;auto close=build(&box,bar);assert(close->name=="Close");assert(box.closes==0);close->callback();assert(box.closes==1);std::cout<<"Actual giveaway top-bar close PASS: localized name, click closes the box once, no giveaway/payment/network\n";}
''')


if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='luxury-giveaway-close-') as directory:
        try:
            check(Path(directory), *sys.argv[1:2])
        except AssertionError as error:
            print(error)
            raise SystemExit(1)
