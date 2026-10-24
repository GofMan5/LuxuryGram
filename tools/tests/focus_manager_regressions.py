"""Execute production FocusManager with real rpl and explicit Qt pointer/focus/timer doubles."""
from pathlib import Path
import tempfile
from library_regressions import ROOT, cpp, function, source


def check(root):
    text = source('Telegram/lib_ui/ui/accessible/ui_accessible_widget.cpp')
    klass = function(text, 'class FocusManager final') + ';'
    constructor = text[text.index('FocusManager::FocusManager()'):text.index('void FocusManager::registerWidget(')]
    methods = constructor + '\n'.join(function(text, sig) for sig in (
        'void FocusManager::registerWidget(', 'void FocusManager::updateFocusPolicy(',
        'void FocusManager::cleanup()'))
    cpp(root, 'focus_manager', r'''
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <ranges>
#include <utility>
#include <vector>
#include <rpl/variable.h>
namespace base::assertion{void log(const char*,const char*,int){std::abort();}}
#undef Expects
#define Expects(x) assert(x)
#define LOG(...) do{}while(false)
template<class T>struct not_null{T p;not_null(T v):p(v){assert(v);}T get()const{return p;}T operator->()const{return p;}operator T()const{return p;}};
namespace Qt{enum FocusPolicy{NoFocus=0,TabFocus=1,ClickFocus=2,StrongFocus=11,WheelFocus=15};}
namespace crl{using time=long long;}
namespace ranges{template<class C,class F>auto remove_if(C&c,F f){return std::remove_if(c.begin(),c.end(),f);}using std::ranges::find;using std::ranges::find_if;}
namespace base{struct Timer{bool active=false;std::function<void()>callback;template<class F>Timer(F f):callback(f){}bool isActive(){return active;}void callOnce(long long){active=true;}};}
namespace Ui{rpl::variable<bool>mode=false;auto ScreenReaderModeActiveValue(){return mode.value();}
struct RpWidget{std::shared_ptr<bool>alive=std::make_shared<bool>(true);Qt::FocusPolicy regular=Qt::NoFocus,accessible=Qt::TabFocus;int writes=0;~RpWidget(){*alive=false;}Qt::FocusPolicy focusPolicy(){return regular;}Qt::FocusPolicy accessibilityFocusPolicy(){return accessible;}void setFocusPolicy(Qt::FocusPolicy p){regular=p;++writes;}};
}
template<class T>struct QPointer{T*p=nullptr;std::weak_ptr<bool>alive;QPointer()=default;QPointer(T*v):p(v),alive(v->alive){}T*data()const{auto v=alive.lock();return v&&*v?p:nullptr;}T*get()const{return data();}operator bool()const{return data();}operator T*()const{return data();}T*operator->()const{return data();}friend bool operator==(const QPointer&a,T*b){return a.data()==b;}friend bool operator==(const QPointer&a,const QPointer&b){return a.data()==b.data();}};
namespace Ui::Accessible{constexpr auto kCleanupDelay=5*crl::time(1000);
''' + klass + '\n' + methods + r'''
}
int main(){using Ui::Accessible::FocusManager;int cases=0;
for(bool initial:{false,true})for(auto ordinary:{Qt::NoFocus,Qt::TabFocus,Qt::ClickFocus,Qt::StrongFocus,Qt::WheelFocus})for(auto accessible:{Qt::NoFocus,Qt::TabFocus,Qt::ClickFocus,Qt::StrongFocus}){
 Ui::mode=initial;FocusManager manager;Ui::RpWidget widget;widget.regular=ordinary;widget.accessible=accessible;manager.registerWidget(&widget);
 const auto combined=accessible==Qt::NoFocus?ordinary:Qt::FocusPolicy(ordinary|accessible);
 assert(widget.focusPolicy()==(initial?combined:ordinary));Ui::mode=true;assert(widget.focusPolicy()==combined);Ui::mode=false;assert(widget.focusPolicy()==ordinary);Ui::mode=true;assert(widget.focusPolicy()==combined);Ui::mode=false;assert(widget.focusPolicy()==ordinary);++cases;
}
Ui::mode=false;{FocusManager manager;Ui::RpWidget widget;widget.regular=Qt::StrongFocus;manager.registerWidget(&widget);manager.registerWidget(&widget);Ui::mode=true;Ui::mode=false;assert(widget.regular==Qt::StrongFocus);}
{FocusManager manager;Ui::RpWidget widget;manager.registerWidget(&widget);widget.regular=Qt::ClickFocus;Ui::mode=true;assert(widget.regular==Qt::StrongFocus||widget.regular==Qt::FocusPolicy(Qt::ClickFocus|Qt::TabFocus));Ui::mode=false;assert(widget.regular==Qt::ClickFocus);}
{FocusManager manager;Ui::RpWidget widget;manager.registerWidget(&widget);Ui::mode=true;widget.regular=Qt::WheelFocus;Ui::mode=false;assert(widget.regular==Qt::WheelFocus);}
{FocusManager manager;auto a=std::make_unique<Ui::RpWidget>();auto b=std::make_unique<Ui::RpWidget>();manager.registerWidget(a.get());manager.registerWidget(b.get());a.reset();Ui::mode=true;assert(b->regular==Qt::TabFocus);b.reset();Ui::mode=false;}
std::cout<<"Actual FocusManager PASS: "<<cases<<" initial-mode/ordinary-policy/AT-policy combinations, cycles, duplicate interface, owner policy updates, deleted widgets; real rpl, no native Qt focus dispatch\n";
}
''', extra=[arg for p in ('Telegram/lib_rpl', 'Telegram/lib_base', 'Telegram/ThirdParty/GSL/include') for arg in ('-I', str(ROOT / p))])


if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='luxury-focus-manager-') as directory:
        check(Path(directory))
