"""Execute actual custom reactions picker toggle construction name wiring."""
from pathlib import Path
import tempfile
from library_regressions import cpp, source


def check(root):
    text = source('Telegram/SourceFiles/boxes/peers/edit_peer_reactions.cpp')
    start = text.index('\tconst auto toggle = Ui::CreateChild<Ui::IconButton>')
    block = text[start:text.index('\n\tusing SelectorState', start)]
    assert 'toggle->setAccessibleName(tr::lng_manage_peer_reactions(tr::now));' in block
    cpp(root, 'reaction_picker_toggle', r'''
#include <cassert>
#include <functional>
#include <iostream>
#include <string>
using QString=std::string;namespace tr{constexpr int now=0;QString lng_manage_peer_reactions(int){return "Reactions";}}
namespace st{constexpr int manageGroupReactions=1;}
struct Widget{};template<class T>struct not_null{T p;not_null(T p):p(p){}T get(){return p;}operator T(){return p;}T operator->(){return p;}};namespace Ui{struct IconButton{QString name;std::function<void()>callback;IconButton(Widget*,int){}void setAccessibleName(QString s){name=s;}void addClickHandler(std::function<void()>f){callback=f;}};template<class T>T*CreateChild(Widget*p,int s){return new T(p,s);}}
void setup(not_null<Widget*> parent){
''' + block + r'''
}
int main(){Widget p;setup(&p);std::cout<<"Actual custom reaction picker toggle name PASS: localized Reactions action, no picker/network/action activation\n";}
''')


if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='luxury-reactions-toggle-') as directory:
        check(Path(directory))
