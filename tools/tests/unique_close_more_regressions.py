"""Execute the shared unique-gift close/menu/craft icon helper; no gift, craft, payment or network actions."""
import sys
from pathlib import Path
import tempfile
from library_regressions import cpp, function, source


OWNER = 'Telegram/SourceFiles/settings/settings_credits_graphics.cpp'
NAMES = (
    'close->setAccessibleName(tr::lng_close(tr::now));',
    'menu->setAccessibleName(tr::lng_credits_more_options(tr::now));',
    'craft->setAccessibleName(tr::lng_gift_craft_menu_button(tr::now));',
)


def check(root, path=OWNER):
    body = function(source(path), 'void AddUniqueCloseMoreButton(')
    if any(body.count(name) != 1 for name in NAMES):
        raise AssertionError('RED: unique-gift close/menu/craft icons have no localized accessible names')
    cpp(root, 'unique_close_more', r'''
#include <cassert>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
using QString=std::string;template<class T>using Fn=std::function<T>;
namespace tr{constexpr int now=0;QString lng_close(int){return "Close";}QString lng_credits_more_options(int){return "More Options";}QString lng_gift_craft_menu_button(int){return "Craft";}}
struct Style{};namespace style{using IconButton=Style;}namespace st{const Style uniqueCloseButton,uniqueMenuButton,uniqueCraftButton,popupMenuWithIcons;}
struct QPoint{};struct QCursor{static QPoint pos(){return {};}};
struct Lifetime{std::shared_ptr<void>held;template<class T>T*make_state(){auto p=std::make_shared<T>();held=p;return p.get();}};
struct Value{};
namespace rpl{template<class F>struct OnNext{F f;};template<class F>OnNext<F> on_next(F f,Lifetime&){return {f};}}
template<class F>void operator|(Value,rpl::OnNext<F> n){n.f(400);}
template<class T>struct not_null{T p;not_null(T v):p(v){}T operator->()const{return p;}operator T()const{return p;}};
namespace Ui{
struct GenericBox{int closes=0;void closeBox(){++closes;}Value widthValue(){return {};}};
struct PopupMenu{PopupMenu(void*,const Style&){}bool empty()const{return true;}void popup(QPoint){}};
struct IconButton{QString name;std::function<void()>callback;Lifetime life;int right=-1;IconButton(GenericBox*,const Style&){}void setAccessibleName(QString s){name=s;}void setClickedCallback(std::function<void()>f){callback=f;}void show(){}void raise(){}void moveToRight(int x,int,int){right=x;}int width()const{return 24;}Lifetime&lifetime(){return life;}};
template<class T>T*CreateChild(GenericBox*p,const Style&s){return new T(p,s);}}
namespace base{template<class T>struct unique_qptr{std::unique_ptr<T>p;unique_qptr()=default;unique_qptr(std::nullptr_t){}unique_qptr&operator=(std::nullptr_t){p.reset();return *this;}explicit operator bool()const{return bool(p);}T*get()const{return p.get();}T*operator->()const{return p.get();}};template<class T,class...A>unique_qptr<T>make_unique_q(A&&...a){unique_qptr<T>r;r.p=std::make_unique<T>(std::forward<A>(a)...);return r;}}
namespace Settings{struct CreditsEntryBoxStyleOverrides{const Style*menu=nullptr;};
Ui::IconButton*lastClose,*lastMenu,*lastCraft;
void AddUniqueCloseMoreButton(not_null<Ui::GenericBox*>,CreditsEntryBoxStyleOverrides,Fn<void(not_null<Ui::PopupMenu*>)> =nullptr,Fn<void()> =nullptr,const style::IconButton* =nullptr,const style::IconButton* =nullptr);
''' + body.replace('const auto close = Ui::CreateChild<Ui::IconButton>(', 'const auto close = lastClose = Ui::CreateChild<Ui::IconButton>(', 1)
           .replace('const auto menu = fillMenu', 'const auto menu = lastMenu = fillMenu', 1)
           .replace('const auto craft = launchCraft', 'const auto craft = lastCraft = launchCraft', 1) + r'''
}
using namespace Settings;
int main(){
Ui::GenericBox box;int crafts=0,fills=0;
AddUniqueCloseMoreButton(&box,{},[&](not_null<Ui::PopupMenu*>){++fills;},[&]{++crafts;});
assert(lastClose->name=="Close"&&lastMenu->name=="More Options"&&lastCraft->name=="Craft");
assert(box.closes==0&&crafts==0&&fills==0);
lastCraft->callback();assert(crafts==1&&box.closes==0);
lastMenu->callback();assert(fills==1&&box.closes==0);
lastClose->callback();assert(box.closes==1);
Ui::GenericBox plain;AddUniqueCloseMoreButton(&plain,{},nullptr,nullptr);
assert(lastClose->name=="Close"&&!lastMenu&&!lastCraft);
std::cout<<"Actual unique close/menu/craft PASS: localized names, optional icons absent, callbacks fire once, no gift/craft/network\n";}
''')


if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='luxury-unique-close-more-') as directory:
        try:
            check(Path(directory), *sys.argv[1:2])
        except AssertionError as error:
            print(error)
            raise SystemExit(1)
