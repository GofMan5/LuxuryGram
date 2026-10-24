"""Execute the restore-windows offer constructor body; no window restore, settings or network actions."""
import sys
from pathlib import Path
import tempfile
from library_regressions import cpp, source


OWNER = 'Telegram/SourceFiles/dialogs/ui/restore_windows_offer.cpp'
INIT = ', _shadow(st::dialogsTopBarSuggestionShadow) {\n'


def check(root, path=OWNER):
    text = source(path).replace('\r\n', '\n')
    start = text.index(INIT, text.index('RestoreWindowsOffer::RestoreWindowsOffer(')) + len(INIT)
    body = text[start:text.index('\n}\n', start)]
    if body.count('_close->setAccessibleName(tr::lng_close(tr::now));') != 1:
        raise AssertionError('RED: restore-windows offer close icon has no localized accessible name')
    assert 'st::restoreWindowsOfferClose))' in text
    cpp(root, 'restore_windows_close', r'''
#include <cassert>
#include <functional>
#include <iostream>
#include <string>
#include <vector>
using QString=std::string;
namespace tr{constexpr int now=0;QString lng_close(int){return "Close";}QString lng_restore_windows_question(int){return "Restore?";}}
namespace st{constexpr int restoreWindowsOfferQuestionStyle=1;}
enum class RestoreWindowsChoice{Always,Once,Never,Dismiss};
struct Label{QString text;void setText(int,QString t){text=t;}};
struct Button{QString name;std::function<void()>callback;void setAccessibleName(QString s){name=s;}void setClickedCallback(std::function<void()>f){callback=f;}};
struct Event{std::vector<RestoreWindowsChoice>fired;void fire_copy(RestoreWindowsChoice c){fired.push_back(c);}};
struct Offer{
Label _question;Event _chosen;Button*_always=new Button,*_restore=new Button,*_never=new Button,*_wideAlways=new Button,*_wideRestore=new Button,*_wideNever=new Button,*_close=new Button;
Offer(){
''' + body + r'''
}};
int main(){Offer o;assert(o._close->name=="Close"&&o._question.text=="Restore?");assert(o._chosen.fired.empty());o._close->callback();assert(o._chosen.fired.size()==1&&o._chosen.fired[0]==RestoreWindowsChoice::Dismiss);o._always->callback();assert(o._chosen.fired.size()==2&&o._chosen.fired[1]==RestoreWindowsChoice::Always);std::cout<<"Actual restore-windows offer close PASS: localized name, close fires Dismiss once, other choices unchanged, no window restore/settings/network\n";}
''')


if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='luxury-restore-windows-close-') as directory:
        try:
            check(Path(directory), *sys.argv[1:2])
        except AssertionError as error:
            print(error)
            raise SystemExit(1)
