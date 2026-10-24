"""Execute the duplicated settings/starref pinned top-bar back/close icons; no layer, navigation or network actions."""
import sys
from pathlib import Path
import tempfile
from library_regressions import cpp, source


OWNERS = (
    'Telegram/SourceFiles/settings/sections/settings_credits.cpp',
    'Telegram/SourceFiles/settings/sections/settings_business.cpp',
    'Telegram/SourceFiles/settings/sections/settings_premium.cpp',
    'Telegram/SourceFiles/info/bot/starref/info_bot_starref_join_widget.cpp',
    'Telegram/SourceFiles/info/bot/starref/info_bot_starref_setup_widget.cpp',
)
NAMES = (
    '_back->entity()->setAccessibleName(tr::lng_go_back(tr::now));',
    '_close->setAccessibleName(tr::lng_close(tr::now));',
)
PRELUDE = r'''
#include <cassert>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <type_traits>
using QString=std::string;
namespace tr{constexpr int now=0;QString lng_go_back(int){return "Go back";}QString lng_close(int){return "Close";}}
struct QPoint{int x()const{return 0;}int y()const{return 0;}};
struct IconStyle{int width=40;};struct TopBarStyle{IconStyle back;QPoint titlePosition;};struct Ripple{int color=0;};
namespace st{const IconStyle infoTopBarBack,infoLayerTopBarBack,settingsPremiumLayerTopBarBack,settingsPremiumTopBarBack,infoTopBarClose,settingsPremiumTopBarClose;const int infoTopBarScale=1;const TopBarStyle infoLayerTopBar,infoTopBar;const Ripple universalRippleAnimation;}
struct Lifetime{};struct Producer{};struct Variable{Producer value()const{return {};}};
namespace rpl{struct TypeErased{}constexpr type_erased{};inline Producer single(bool){return {};}template<class F>struct OnNext{F f;};template<class F>OnNext<F> on_next(F f,Lifetime&){return {f};}}
inline Producer operator|(Producer,rpl::TypeErased){return {};}
template<class F>void operator|(Producer,rpl::OnNext<F> n){if constexpr(std::is_invocable_v<F,bool>)n.f(true);else n.f();}
struct Widget{Lifetime life;void setTextPosition(int,int){}Producer widthValue(){return {};}Lifetime&lifetime(){return life;}};
namespace Info{enum class Wrap{Layer,Narrow,Side};}
template<class T>struct object_ptr{T*p;template<class...A>object_ptr(A&&...a):p(new T(a...)){}};
namespace Ui{
struct IconButton{QString name;std::function<void()>click;Lifetime life;IconButton(Widget*,const IconStyle&){}void setAccessibleName(QString s){name=s;}void addClickHandler(std::function<void()>f){click=f;}void setRippleColorOverride(const int*){}void moveToRight(int,int){}Lifetime&lifetime(){return life;}};
template<class T>struct FadeWrap{T*inner;Lifetime life;FadeWrap(Widget*,object_ptr<T>o,int):inner(o.p){}void setDuration(int){}void toggleOn(Producer){}T*entity()const{return inner;}Producer toggledValue(){return {};}Lifetime&lifetime(){return life;}};}
namespace base{template<class T>struct unique_qptr{std::unique_ptr<T>p;unique_qptr()=default;unique_qptr(std::nullptr_t){}unique_qptr&operator=(std::nullptr_t){p.reset();return *this;}explicit operator bool()const{return bool(p);}T*operator->()const{return p.get();}};template<class T,class...A>unique_qptr<T>make_unique_q(A&&...a){unique_qptr<T>r;r.p=std::make_unique<T>(std::forward<A>(a)...);return r;}}
struct Parent{int hides=0,special=0;void hideLayer(){++hides;}void hideSpecialLayer(){++special;}};
struct Controller{Parent parent;int backs=0;Parent*parentController(){return &parent;}void showBackFromStack(){++backs;}};
struct CtlRef{Controller*c;Controller*operator->()const{return c;}Controller*operator()()const{return c;}};
struct Empty{};struct Event{int fired=0;void fire(Empty){++fired;}};
struct Owner{
base::unique_qptr<Ui::FadeWrap<Ui::IconButton>>_back;base::unique_qptr<Ui::IconButton>_close;
Variable _backToggles,_backEnabled;Event _showBack;bool isEmojiStatus=false;Widget widget;Widget*content=&widget;Widget*raw=&widget;
void build(Info::Wrap wrap,CtlRef controller){
'''
MAIN = r'''
}};
int main(){
Controller ctl;Owner o;o.build(Info::Wrap::Layer,{&ctl});
assert(o._back->entity()->name=="Go back"&&o._close&&o._close->name=="Close");
assert(ctl.parent.hides==0&&ctl.parent.special==0&&ctl.backs==0&&o._showBack.fired==0);
o._close->click();assert(ctl.parent.hides==1&&ctl.parent.special==1);
o._back->entity()->click();assert(ctl.backs+o._showBack.fired==1&&ctl.parent.hides==1);
o.isEmojiStatus=true;o.build(Info::Wrap::Layer,{&ctl});assert(o._back->entity()->name=="Go back"&&o._close->name=="Close");
o.build(Info::Wrap::Narrow,{&ctl});assert(o._back->entity()->name=="Go back"&&!o._close);
std::cout<<"Actual top-bar back/close PASS: "<<OWNER<<"\n";}
'''


def block(text):
    text = text.replace('\r\n', '\n')
    start = text.index('const auto isLayer = (wrap == Info::Wrap::Layer);',
                       text.index('_back = base::make_unique_q<Ui::FadeWrap<Ui::IconButton>>(') - 200)
    end = text.index('\n\t}, ', text.index('_close->moveToRight(0, 0);', start))
    return text[start:end]


def check(root, paths=OWNERS):
    blocks = [block(source(path)) for path in paths]
    red = [Path(owner).stem for owner, b in zip(OWNERS, blocks)
           if any(b.count(name) != 1 for name in NAMES)]
    if red:
        raise AssertionError('RED: pinned top-bar back/close icons have no localized accessible names in ' + ', '.join(red))
    for owner, b in zip(OWNERS, blocks):
        assert 'IconButton>(' in b and '_close = nullptr;' in b
        cpp(root, 'topbar_' + Path(owner).stem, f'#define OWNER "{Path(owner).stem}"\n' + PRELUDE + b + MAIN)
    print('Actual pinned top-bar back/close PASS: 5 owners, localized Go back/Close names, layer-only close, clicks fire once, no layer/navigation/network')


if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='luxury-topbar-back-close-') as directory:
        try:
            check(Path(directory), *([sys.argv[1:]] if sys.argv[1:] else []))
        except AssertionError as error:
            print(error)
            raise SystemExit(1)
