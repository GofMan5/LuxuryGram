"""Main-menu regressions extracted from production C++; not native Qt/UIA QA."""
from pathlib import Path
import tempfile
from library_regressions import ROOT, cpp, function, source


def link_text(root):
    text = source('Telegram/lib_ui/ui/widgets/buttons.cpp')
    methods = '\n'.join(function(text, signature) for signature in (
        'void LinkButton::setText(', 'void LinkButton::resizeToText()'))
    cpp(root, 'main_menu_link_text', r'''
#include <cassert>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
using QString=std::string;
template<class T>using Fn=std::function<T>;
namespace base {struct Weak {std::weak_ptr<bool> alive;operator bool()const{auto a=alive.lock();return a&&*a;}};template<class T>Weak make_weak(T *p){return {p->alive};}}
struct Font {int width(const QString &s)const{return int(s.size())*7;}};
struct Padding {int left()const{return 2;}int right()const{return 3;}};
struct Style {Font fontData;Font *font=&fontData;Padding padding;};
namespace Ui {
struct LinkButton {
    std::shared_ptr<bool> alive=std::make_shared<bool>(true);
    Style _st;QString _text;int _textWidth=0,natural=5,updates=0,names=0;
    Fn<void()> onName,onResize;
    ~LinkButton(){*alive=false;}
    void setNaturalWidth(int v){natural=v;if(onResize){auto f=onResize;f();}}
    void update(){assert(*alive);++updates;}
    void accessibilityNameChanged(){++names;if(onName){auto f=onName;f();}}
    void setText(const QString &);void resizeToText();
};
''' + methods + r'''
}
int main(){
    auto b=std::make_unique<Ui::LinkButton>();
    b->onName=[&]{assert(b->_text=="Preferences");assert(b->natural==82&&b->updates==1);};
    b->setText("Preferences");assert(b->names==1);
    b->setText("Preferences");assert(b->names==1&&b->updates==1);
    b->onName=[&]{assert(b->_text=="Long localized preferences");assert(b->natural==187);b.reset();};
    b->setText("Long localized preferences");assert(!b);
    b=std::make_unique<Ui::LinkButton>();b->onResize=[&]{b.reset();};
    b->setText("Destroy during resize");assert(!b);
    std::cout<<"LinkButton actual setter PASS: committed layout before NameChanged, equal text no-op, destruction at name/resize boundaries\n";
}
''')


def accounts(root):
    text = source('Telegram/SourceFiles/window/window_main_menu.cpp')
    state_h = source('Telegram/lib_ui/ui/rp_widget.h')
    state_cpp = source('Telegram/lib_ui/ui/rp_widget.cpp')
    methods = '\n'.join(function(text, s) for s in (
        'QString MainMenu::ToggleAccountsButton::accessibilityName()',
        'Ui::AccessibilityState MainMenu::ToggleAccountsButton::accessibilityState() const',
        'QStringList MainMenu::ToggleAccountsButton::accessibilityActionNames()',
        'void MainMenu::ToggleAccountsButton::accessibilityDoAction('))
    start = text.index('\tsettings.mainMenuAccountsShownValue(', text.index('MainMenu::ToggleAccountsButton::ToggleAccountsButton('))
    end = text.index('\n}\n', start)
    subscription = text[start:end]
    cpp(root, 'main_menu_accounts', r'''
#include <cassert>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <rpl/variable.h>
#include <rpl/filter.h>
#include <rpl/event_stream.h>
namespace base::assertion{void log(const char*,const char*,int){std::abort();}}
#undef Expects
#define Expects(x) assert(x)
using QString=std::string;using QStringList=std::vector<QString>;template<class T>using Fn=std::function<T>;
struct QAccessibleActionInterface{static QString pressAction(){return "press";}static QString showMenuAction(){return "menu";}};
namespace QAccessible{struct State{bool checkable=false,checked=false,extSelectable=false,multiSelectable=false,pressed=false,readOnly=false,selectable=false,selected=false,expandable=false,expanded=false,collapsed=false,disabled=true;};}
namespace base{struct Weak{std::weak_ptr<bool>p;operator bool()const{auto v=p.lock();return v&&*v;}};template<class T>Weak make_weak(T*p){return{p->alive};}}
namespace crl{struct Task{base::Weak alive;Fn<void()>f;};std::vector<Task>tasks;template<class T,class F>void on_main(T*p,F f){tasks.push_back({base::make_weak(p),f});}void drain(){auto jobs=std::exchange(tasks,{});for(auto&t:jobs)if(t.alive)t.f();}}
namespace tr{constexpr auto now=0;QString show="Show accounts",hide="Hide accounts";QString luxury_ShowAccounts(int){return show;}QString luxury_HideAccounts(int){return hide;}}
namespace Lang{rpl::event_stream<>updated;auto Updated(){return updated.events();}}
namespace st{constexpr auto slideWrapDuration=150;}
namespace Ui{
''' + function(state_h, 'struct AccessibilityState') + r''';
''' + function(state_cpp, 'void AccessibilityState::writeTo(') + r'''
struct AbstractButton{bool disabled=false,down=false;int presses=0;Fn<void()>clicked;AccessibilityState accessibilityState()const{return{.pressed=down};}bool isDisabled()const{return disabled;}
''' + function(source('Telegram/lib_ui/ui/abstract_button.cpp'), 'void AbstractButton::accessibilityDoAction(').replace('void AbstractButton::accessibilityDoAction(', 'void accessibilityDoAction(').replace('clicked(Qt::NoModifier, Qt::LeftButton);', '++presses;if(clicked){auto f=clicked;f();}') + r'''
};
}
struct Settings{rpl::variable<bool>shown=false;auto mainMenuAccountsShownValue(){return shown.value();}};
struct MainMenu{struct ToggleAccountsButton:Ui::AbstractButton{std::shared_ptr<bool>alive=std::make_shared<bool>(true);rpl::lifetime life;bool _toggled=false;int validations=0,names=0,changes=0;Fn<void()>onName,onState,onValidate;struct Animation{template<class F>void start(F,double,double,int){}void stop(){}}_toggledAnimation;~ToggleAccountsButton(){*alive=false;}auto&lifetime(){return life;}void update(){}void validateUnreadBadge(){++validations;if(onValidate){auto f=onValidate;f();}}void accessibilityNameChanged(){++names;if(onName){auto f=onName;f();}}void accessibilityStateChanged(Ui::AccessibilityState s){assert(!s.expandable&&s.expanded&&s.collapsed&&!s.checked);++changes;if(onState){auto f=onState;f();}}QString accessibilityName();Ui::AccessibilityState accessibilityState()const;QStringList accessibilityActionNames();void accessibilityDoAction(const QString&);void setup(Settings&settings){
''' + subscription + r'''
}};};
''' + methods + r'''
int main(){Settings settings;auto b=std::make_unique<MainMenu::ToggleAccountsButton>();b->setup(settings);b->clicked=[&]{settings.shown=!settings.shown.current();};
auto s=b->accessibilityState();assert(s.expandable&&s.collapsed&&!s.expanded&&!s.checkable);QAccessible::State q;s.writeTo(q);assert(q.expandable&&q.collapsed&&!q.expanded&&q.disabled);assert(b->accessibilityName()=="Show accounts");assert(b->accessibilityActionNames()==QStringList{"menu"});
b->accessibilityDoAction("other");assert(b->presses==0);b->disabled=true;b->accessibilityDoAction("menu");assert(b->presses==0&&!settings.shown.current());b->disabled=false;
b->accessibilityDoAction("menu");assert(settings.shown.current()&&b->presses==1&&b->changes==0);b->onName=[&]{assert(b->_toggled&&b->accessibilityState().expanded&&b->accessibilityName()=="Hide accounts");};crl::drain();assert(b->changes==1&&b->names==1);
b->accessibilityDoAction("menu");assert(b->presses==1);b->onName={};b->accessibilityDoAction("press");assert(!settings.shown.current()&&b->presses==2);crl::drain();assert(b->changes==2);settings.shown=false;crl::drain();assert(b->changes==2);
tr::show="Localized show";Lang::updated.fire({});crl::drain();assert(b->accessibilityName()=="Localized show"&&b->changes==2);
b->onName=[&]{b.reset();};settings.shown=true;crl::drain();assert(!b);
b=std::make_unique<MainMenu::ToggleAccountsButton>();b->setup(settings);b.reset();crl::drain();
b=std::make_unique<MainMenu::ToggleAccountsButton>();b->setup(settings);crl::drain();b->onValidate=[&]{b.reset();};settings.shown=false;crl::drain();assert(!b);
std::cout<<"Main-menu actual disclosure state/mapping/actions/subscription PASS: press/expand/disabled/equality/localization/committed queued state/deletion boundaries; real rpl, explicit Qt/style/queue doubles\n";
}
''', extra=[arg for p in ('Telegram/lib_rpl', 'Telegram/lib_base', 'Telegram/ThirdParty/GSL/include') for arg in ('-I', str(ROOT / p))])


def preferences(root):
    text = source('Telegram/SourceFiles/window/window_main_menu.cpp')
    setup = function(text, 'void MainMenu::setupPreferencesButton()')
    geometry = function(text, 'void MainMenu::updateControlsGeometry()')
    layout = geometry[geometry.index('\t_preferencesButton->resizeToNaturalWidth'):geometry.index('\t_toggleAccounts->setGeometry')]
    rp = source('Telegram/lib_ui/ui/rp_widget.h')
    cpp(root, 'main_menu_preferences', r'''
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <string>
#include <utility>
#include <rpl/combine.h>
#include <rpl/variable.h>
namespace base::assertion{void log(const char*,const char*,int){std::abort();}}
#undef Expects
#define Expects(x) assert(x)
using QString=std::string;
namespace Qt{constexpr auto TabFocus=1;}
namespace style{bool rtl=false;bool RightToLeft(){return rtl;}}
struct Margins{int left(){return 0;}int top(){return 0;}};
struct Widget{int w=0,x=0,y=0;Widget*p=nullptr;int width()const{return w;}Widget*parentWidget(){return p;}void move(int a,int b){x=a;y=b;}};
struct Link:Widget{using Widget=::Widget;int natural=0,focus=0;QString text;rpl::variable<int>n=0;rpl::lifetime life;std::function<void()>clicked;auto&lifetime(){return life;}int naturalWidth(){return natural;}auto naturalWidthValue(){return n.value();}void setFocusPolicy(int v){focus=v;}void setClickedCallback(std::function<void()>f){clicked=f;}void setText(const QString&s){text=s;natural=int(s.size())*7;n=natural;}void resizeToWidth(int v){w=v;}Margins getMargins(){return{};}
''' + function(rp, 'void resizeToNaturalWidth(int newWidth)') + '\n' + function(rp, 'void moveToLeft(int x, int y, int outerw = 0)') + r'''
};
struct Toggle{rpl::variable<int>skip=42;auto rightSkipValue(){return skip.value();}int rightSkip(){return skip.current();}};
namespace tr{rpl::variable<QString>label="Preferences";auto luxury_LuxuryPreferences(){return label.value();}}
namespace st{constexpr int mainMenuCoverStatusLeft=26,mainMenuCoverStatusTop=103;}
namespace Settings{struct LuxuryMain{static int Id(){return 77;}};}
struct Controller{int target=0,calls=0;void showSettings(int id){target=id;++calls;}};
struct MainMenu:Widget{Link link;Toggle toggle;Controller controller;Link*_preferencesButton=&link;Toggle*_toggleAccounts=&toggle;Controller*_controller=&controller;rpl::lifetime life;int layouts=0;MainMenu(){w=274;link.p=this;}auto&lifetime(){return life;}void setupPreferencesButton();void updateControlsGeometry(){++layouts;
''' + layout + r'''
}};
''' + setup + r'''
int main(){int cases=0;for(bool rtl:{false,true})for(int width:{50,120,274,400})for(int skip:{42,96,160}){style::rtl=rtl;MainMenu menu;menu.w=width;menu.toggle.skip=skip;menu.setupPreferencesButton();assert(menu.link.focus==Qt::TabFocus);for(const auto&text:{QString("Preferences"),QString(120,'x'),QString("Short")}){tr::label=text;const auto available=std::max(width-26-skip,0);assert(menu.link.width()==std::min(available,int(text.size())*7));const auto logical=rtl?width-menu.link.x-menu.link.width():menu.link.x;assert(logical==26&&menu.link.y==103);++cases;}menu.toggle.skip=20;assert(menu.link.width()==std::min(std::max(width-46,0),35));menu.link.clicked();assert(menu.controller.target==77&&menu.controller.calls==1);const auto before=menu.layouts;menu.life.destroy();tr::label="Disposed";menu.toggle.skip=10;assert(menu.layouts==before);menu.link.life.destroy();}
std::cout<<"Actual preferences setup/layout + RpWidget LTR/RTL methods PASS: "<<cases<<" width/badge/long-label variants, reactive natural width, LuxuryMain callback, disposed subscriptions; explicit glyph/widget doubles, no raster\n";
}
''', extra=[arg for p in ('Telegram/lib_rpl', 'Telegram/lib_base', 'Telegram/ThirdParty/GSL/include') for arg in ('-I', str(ROOT / p))])


def check(root):
    link_text(root)
    accounts(root)
    preferences(root)


if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='luxury-main-menu-') as directory:
        check(Path(directory))
    print('Main-menu extracted checks PASS; native Qt/UIA/rendering remains separate.')
