"""Execute the credits subscription/rejoin box close icon; no subscription, payment or network actions."""
import sys
from pathlib import Path
import tempfile
from library_regressions import cpp, function, source


OWNER = 'Telegram/SourceFiles/settings/settings_credits_graphics.cpp'
SIGNATURE = 'std::shared_ptr<Data::GiftUpgradeSpinner> upgradeSpinner,\n\t\tCreditsEntryBoxStyleOverrides st) {'


def check(root, path=OWNER):
    body = function(source(path).replace('\r\n', '\n'), SIGNATURE)
    start = body.index('const auto close = Ui::CreateChild<Ui::IconButton>(', body.index('if (willBusy) {'))
    block = body[start:body.index('content->widthValue()', start)]
    if block.count('close->setAccessibleName(tr::lng_close(tr::now));') != 1:
        raise AssertionError('RED: credits subscription box close icon has no localized accessible name')
    assert 'st::boxTitleClose' in block and 'content,' in block
    cpp(root, 'credits_subscription_close', r'''
#include <cassert>
#include <functional>
#include <iostream>
#include <string>
using QString=std::string;namespace tr{constexpr int now=0;QString lng_close(int){return "Close";}}
namespace st{constexpr int boxTitleClose=1;}
struct Widget{};struct Box{int closes=0;void closeBox(){++closes;}};
namespace Ui{struct IconButton{QString name;std::function<void()>callback;IconButton(Widget*,int){}void setAccessibleName(QString s){name=s;}void setClickedCallback(std::function<void()>f){callback=f;}};template<class T>T*CreateChild(Widget*p,int s){return new T(p,s);}}
Ui::IconButton*build(Box*box,Widget*content){
''' + block + r'''
return close;}
int main(){Box box;Widget content;auto close=build(&box,&content);assert(close->name=="Close");assert(box.closes==0);close->callback();assert(box.closes==1);std::cout<<"Actual credits subscription close PASS: localized name, click closes the box once, no subscription/payment/network\n";}
''')


if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='luxury-credits-subscription-close-') as directory:
        try:
            check(Path(directory), *sys.argv[1:2])
        except AssertionError as error:
            print(error)
            raise SystemExit(1)
