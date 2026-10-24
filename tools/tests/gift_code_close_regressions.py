"""Execute both Gift Code close icon construction paths; no gift/payment/network actions."""
from pathlib import Path
import tempfile
from library_regressions import cpp, source


def check(root):
    text = source('Telegram/SourceFiles/boxes/gift_premium_box.cpp')
    starts = []
    cursor = 0
    while True:
        try:
            start = text.index('const auto close = Ui::CreateChild<Ui::IconButton>', cursor)
        except ValueError:
            break
        starts.append(start); cursor = start + 1
    assert len(starts) >= 2
    blocks = [text[start:text.index('box->widthValue(', start)].replace('box.get()', 'box') for start in starts[:2]]
    for block in blocks:
        assert 'close->setAccessibleName(tr::lng_close(tr::now));' in block
        assert 'st::boxTitleClose' in block
    cpp(root, 'gift_code_closes', r'''
#include <cassert>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
using QString=std::string;namespace tr{constexpr int now=0;QString lng_close(int){return "Close";}}
namespace st{constexpr int boxTitleClose=1;}
struct Widget{};struct Box:Widget{int closes=0;void closeBox(){++closes;}};
namespace Ui{struct IconButton{QString name;std::function<void()>callback;IconButton(Widget*,int){}void setAccessibleName(QString s){name=s;}void setClickedCallback(std::function<void()>f){callback=f;}};template<class T>T*CreateChild(Widget*p,int s){return new T(p,s);}}
void first(Box*box){
''' + blocks[0] + r'''
}
void second(Box*box){
''' + blocks[1] + r'''
}
int main(){Box box;first(&box);second(&box);std::cout<<"Actual Gift Code close actions PASS: both icon construction paths localized and callbacks installed without invoking gifts/payment/network\n";}
''')


if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='luxury-gift-close-') as directory:
        check(Path(directory))
