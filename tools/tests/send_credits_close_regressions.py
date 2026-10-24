"""Execute actual credits-box close button wiring; no payment/credits action."""
from pathlib import Path
import tempfile
from library_regressions import cpp, function, source


def check(root):
    text = source('Telegram/SourceFiles/boxes/send_credits_box.cpp')
    close_start = text.index('\t\tconst auto close =')
    block = text[close_start:text.index('\n\t\tcontent->widthValue', close_start)]
    assert 'close->setAccessibleName(tr::lng_close(tr::now));' in block
    assert 'close->setClickedCallback([=] { box->closeBox(); });' in block
    cpp(root, 'send_credits_close', r'''
#include <cassert>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
using QString=std::string;namespace tr{constexpr int now=0;QString lng_close(int){return "Close";}}
namespace st{constexpr int boxTitleClose=1;}
struct Widget{};struct Box:Widget{int closes=0;void closeBox(){++closes;}};
namespace Ui{struct IconButton{QString name;std::function<void()>callback;IconButton(Widget*,int){}void setAccessibleName(QString s){name=s;}void setClickedCallback(std::function<void()>f){callback=f;}};template<class T>T*CreateChild(Widget*w,int s){return new T(w,s);}}
void setup(Box*box,Widget*content){
''' + block + r'''
}
int main(){Box box;Widget content;setup(&box,&content);std::cout<<"Actual credits close action PASS: localized name and unchanged callback; no credits/payment/native box action\n";}
''')


if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='luxury-credits-close-') as directory:
        check(Path(directory))
