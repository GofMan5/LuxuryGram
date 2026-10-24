"""Execute the emoji game stake box close icon; no stake, roll, TON or network actions."""
import sys
from pathlib import Path
import tempfile
from library_regressions import cpp, source


OWNER = 'Telegram/SourceFiles/ui/boxes/emoji_stake_box.cpp'


def check(root, path=OWNER):
    text = source(path).replace('\r\n', '\n')
    start = text.index('const auto close = CreateChild<IconButton>(', text.index('void EmojiGameStakeBox('))
    block = text[start:text.index('container->widthValue()', start)]
    if block.count('close->setAccessibleName(tr::lng_close(tr::now));') != 1:
        raise AssertionError('RED: emoji game stake box close icon has no localized accessible name')
    assert 'st::boxTitleClose' in block and 'container,' in block
    cpp(root, 'emoji_stake_close', r'''
#include <cassert>
#include <functional>
#include <iostream>
#include <string>
using QString=std::string;namespace tr{constexpr int now=0;QString lng_close(int){return "Close";}}
namespace st{constexpr int boxTitleClose=1;}
struct Widget{};struct Box{int closes=0;void closeBox(){++closes;}};
struct IconButton{QString name;std::function<void()>callback;IconButton(Widget*,int){}void setAccessibleName(QString s){name=s;}void setClickedCallback(std::function<void()>f){callback=f;}};
template<class T>T*CreateChild(Widget*p,int s){return new T(p,s);}
IconButton*build(Box*box,Widget*container){
''' + block + r'''
return close;}
int main(){Box box;Widget container;auto close=build(&box,&container);assert(close->name=="Close");assert(box.closes==0);close->callback();assert(box.closes==1);std::cout<<"Actual emoji stake close PASS: localized name, click closes the box once, no stake/roll/TON/network\n";}
''')


if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='luxury-emoji-stake-close-') as directory:
        try:
            check(Path(directory), *sys.argv[1:2])
        except AssertionError as error:
            print(error)
            raise SystemExit(1)
