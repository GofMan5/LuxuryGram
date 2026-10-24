"""Actual local Selector TabsSlider declaration/methods, base RpWidget signatures.
Real bundled rpl, explicit widget/geometry/Qt queue/weak/AT doubles, no native paint/UIA.
Before implementation fallback extracts incumbent DiscreteSlider and RpWidget defaults.
"""
from pathlib import Path
import re
from library_regressions import cpp, function, source


def check(root):
    text = source('Telegram/SourceFiles/chat_helpers/tabbed_selector.cpp')
    base = source('Telegram/lib_ui/ui/rp_widget.h')
    names = ('accessibilityRole', 'accessibilityChildCount', 'accessibilityChildRole',
             'accessibilityChildName', 'accessibilityChildState', 'accessibilityChildRect',
             'accessibilityChildSupportsActions', 'accessibilityChildIdentity',
             'accessibilityChildIndexByIdentity', 'accessibilityChildSetFocus',
             'accessibilityChildActivate', 'accessibilityOrientation', 'accessibilitySelectionList')
    decls = [re.search(r'(?:\[\[nodiscard\]\] )?virtual [^;\n]+\b' + n + r'\([^;]*;', base).group() for n in names]
    defaults = source('Telegram/lib_ui/ui/rp_widget.cpp')
    default_methods = '\n'.join(function(defaults, re.search(r'[^\n]*RpWidget::' + n + r'\(', defaults).group()) for n in names if n not in ('accessibilityChildCount', 'accessibilityOrientation', 'accessibilitySelectionList'))
    actual = 'class TabbedSelector::TabsSlider' in text
    klass = function(text, 'class TabbedSelector::TabsSlider') + ';' if actual else r'''
class TabbedSelector::TabsSlider:public Ui::SettingsSlider{public:
 TabsSlider(TabbedSelector*p,const style::SettingsSlider&s):SettingsSlider(p,s){}
 void setTabs(const std::vector<SelectorTab>&,const std::vector<QString>&labels){setSections(labels);}
 void notifySelectionChanged(int){}
};'''
    methods = ''
    if actual:
        for match in re.finditer(r'^[^\n]*TabbedSelector::TabsSlider::[\w]+\(', text, re.M):
            methods += function(text, match.group()) + '\n'
    slider = source('Telegram/SourceFiles/ui/widgets/discrete_sliders.cpp')
    activation = '\n'.join(function(slider, sig) for sig in ('void DiscreteSlider::setActiveSection(int index)', 'void DiscreteSlider::activateCallback()', 'void DiscreteSlider::setActiveSectionFast(int index)', 'void DiscreteSlider::finishAnimating()'))
    cpp(root, 'selector_tabs', r'''
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <utility>
#include <vector>
#include <rpl/event_stream.h>
namespace base::assertion{void log(const char*,const char*,int){std::abort();}}
#undef Expects
#undef Assert
#define Expects(x) assert(x)
#define Assert(x) assert(x)
template<class T>using not_null=T;template<class T>using Fn=std::function<T>;
using QString=std::string;using quintptr=uintptr_t;
namespace Qt{enum FocusPolicy{NoFocus,TabFocus};enum FocusReason{OtherFocusReason,MouseFocusReason,TabFocusReason,BacktabFocusReason};enum Orientation{Horizontal,Vertical};enum{Key_Left=1,Key_Right,Key_Home,Key_End,Key_Enter,Key_Return,Key_Space,Key_Tab};enum{NoModifier=0,ShiftModifier=1,ControlModifier=2,AltModifier=4,MetaModifier=8,PreciseTimer=0};enum{NoBrush=0};}
struct QPoint{int a=0,b=0;int x()const{return a;}};struct QMargins{QMargins(int,int,int,int){}};
struct QRect{int a=0,b=0,c=0,d=0;QRect(int x=0,int y=0,int w=0,int h=0):a(x),b(y),c(w),d(h){}bool isEmpty()const{return c<=0||d<=0;}int x()const{return a;}int width()const{return c;}QRect intersected(QRect q)const{int x=std::max(a,q.a),y=std::max(b,q.b);return{x,y,std::max(0,std::min(a+c,q.a+q.c)-x),std::max(0,std::min(b+d,q.b+q.d)-y)};}QRect marginsRemoved(QMargins)const{return*this;}};
struct QEvent{};struct QKeyEvent{int value,mods=0;bool repeat=false,accepted=false;QKeyEvent(int v,int m=0,bool r=false):value(v),mods(m),repeat(r){}int key()const{return value;}int modifiers()const{return mods;}bool isAutoRepeat()const{return repeat;}void accept(){accepted=true;}};
struct QMouseEvent{QPoint p;QPoint pos()const{return p;}};struct QFocusEvent{Qt::FocusReason r;auto reason()const{return r;}};struct QPaintEvent{};struct QPainter{template<class T>QPainter(T*){}template<class T>void setPen(T){}template<class T>void setBrush(T){}void drawRect(QRect){}};struct QPen{template<class...T>QPen(T...){}};
struct Color{int c=0;};namespace st{int lineWidth=1,buttonFocusInset=2;Color fg;Color*windowFg=&fg;}
namespace QAccessible{enum Role{NoRole,PageTabList,PageTab};enum Event{ObjectReorder};struct State{bool disabled=false,focusable=false,selectable=false,selected=false,focused=false,invisible=false,offscreen=false;};}
struct QAccessibleEvent;namespace QAccessible{void updateAccessibility(QAccessibleEvent*);}
struct QWidget;namespace QApplication{QWidget*current=nullptr;QWidget*focusWidget(){return current;}}
struct QWidget{std::shared_ptr<bool>alive=std::make_shared<bool>(true);bool visible=true,enabled=true,focused=false;Qt::FocusPolicy policy=Qt::NoFocus;int updates=0,focusEvents=0,selectionEvents=0;std::function<void()>onFocus,onSelection,onReorder;virtual~QWidget(){*alive=false;}bool hasFocus()const{return focused;}bool isVisible()const{return visible;}bool isEnabled()const{return enabled;}void setFocusPolicy(Qt::FocusPolicy p){policy=p;}void setFocus(Qt::FocusReason=Qt::OtherFocusReason){focused=true;QApplication::current=this;if(onFocus)onFocus();}void update(){++updates;}int width()const{return 300;}int height()const{return 40;}QRect rect()const{return{0,0,width(),height()};}QRect myrtlrect(QRect r)const;};
namespace style{bool rtl=false;bool RightToLeft(){return rtl;}struct SettingsSlider{};}
QRect QWidget::myrtlrect(QRect r)const{return style::rtl?QRect(width()-r.a-r.c,r.b,r.c,r.d):r;}
namespace base{struct Weak{std::weak_ptr<bool>a;explicit operator bool()const{auto p=a.lock();return p&&*p;}};template<class T>Weak make_weak(T*p){return{p->alive};}}
template<class T>struct QPointer{T*p;std::weak_ptr<bool>a;explicit QPointer(T*v):p(v),a(v?v->alive:std::weak_ptr<bool>{}){}T*data()const{auto v=a.lock();return v&&*v?p:nullptr;}};
namespace crl{using time=long long;time now(){return 1000;}struct Task{base::Weak weak;std::function<void()>f;};std::vector<Task>tasks;template<class T,class F>void on_main(T*p,F f){tasks.push_back({base::make_weak(p),f});}void drain(){auto ts=std::exchange(tasks,{});for(auto&t:ts)if(t.weak)t.f();}}
namespace ranges{using std::ranges::find;}
namespace Ui{class RpWidget:public QWidget{public:RpWidget(QWidget*){}rpl::lifetime life;auto&lifetime(){return life;}
''' + '\n'.join(decls) + r'''
virtual Qt::FocusPolicy accessibilityFocusPolicy(){return Qt::NoFocus;}
virtual void mousePressEvent(QMouseEvent*){}virtual void mouseReleaseEvent(QMouseEvent*){}virtual void keyPressEvent(QKeyEvent*){}virtual void keyReleaseEvent(QKeyEvent*){}virtual void focusInEvent(QFocusEvent*){}virtual void focusOutEvent(QFocusEvent*){}virtual void paintEvent(QPaintEvent*){}
void accessibilityChildFocused(int){++focusEvents;if(onFocus)onFocus();}void accessibilityChildSelectionChanged(int){++selectionEvents;if(onSelection)onSelection();}
};
''' + default_methods + r'''
int RpWidget::accessibilityChildCount()const{return 0;}std::optional<Qt::Orientation>RpWidget::accessibilityOrientation()const{return{};}bool RpWidget::accessibilitySelectionList()const{return false;}
class DiscreteSlider:public RpWidget{public:DiscreteSlider(QWidget*p):RpWidget(p){}struct Ripple{void lastStop(){}};struct Section{int left=0,width=0;Ripple*ripple=nullptr;};std::vector<Section>sections;int _activeIndex=0,selected=0,_timerId=-1;int _pressed=-1;bool _selectOnPress=true;crl::time _callbackAfterMs=0;rpl::event_stream<int>_sectionActivated;struct Animation{void stop(){}}_a_left,_a_width;
void startRipple(int){}void mousePressEvent(QMouseEvent*)override;void mouseReleaseEvent(QMouseEvent*)override;int getIndexFromPosition(QPoint);void setActiveSection(int);void activateCallback();void setActiveSectionFast(int);void finishAnimating();int activeSection()const{return _activeIndex;}auto sectionActivated(){return _sectionActivated.events();}void setSelectedSection(int i){selected=i;}void killTimer(int){}int startTimer(crl::time,int){return 1;}int sectionsCount()const{return int(sections.size());}void setSections(const std::vector<QString>&v){sections.clear();for(int i=0;i<int(v.size());++i)sections.push_back({i*100,100});}void enumerateSections(Fn<bool(const Section&)>f)const{for(auto&s:sections)if(!f(s))break;}
};
''' + activation + '\n' + '\n'.join(function(slider, sig).replace('_sections', 'sections') for sig in ('void DiscreteSlider::mousePressEvent(QMouseEvent *e)', 'void DiscreteSlider::mouseReleaseEvent(QMouseEvent *e)', 'int DiscreteSlider::getIndexFromPosition(QPoint pos)')) + r'''
class SettingsSlider:public DiscreteSlider{public:SettingsSlider(QWidget*p,const style::SettingsSlider&):DiscreteSlider(p){}void paintEvent(QPaintEvent*)override{}};
}
struct QAccessibleEvent{QWidget*p;QAccessibleEvent(QWidget*w,QAccessible::Event):p(w){}};void QAccessible::updateAccessibility(QAccessibleEvent*e){if(e->p->onReorder)e->p->onReorder();}
namespace ChatHelpers{enum class SelectorTab{Emoji,Stickers,Gifs,Masks};
class TabbedSelector:public QWidget{public:class TabsSlider;int committed=0;SelectorTab _currentTabType=SelectorTab::Emoji;unsigned long long _slideGeneration=0;int indexByType(SelectorTab t)const{return int(t);}};
#define private public
#define protected public
''' + klass + '\n' + methods + r'''
#undef protected
#undef private
}
int main(){using namespace ChatHelpers;style::SettingsSlider st;TabbedSelector owner;auto s=std::make_unique<TabbedSelector::TabsSlider>(&owner,st);s->setTabs({SelectorTab::Emoji,SelectorTab::Stickers,SelectorTab::Gifs},{"Emoji","Stickers","GIFs"});
assert(s->policy==Qt::TabFocus&&"incumbent tab header is unreachable through ordinary Tab");assert(s->accessibilityRole()==QAccessible::PageTabList&&s->accessibilityChildCount()==3);assert(s->accessibilityFocusPolicy()==Qt::NoFocus);int events=0,last=-1;auto life=rpl::lifetime();s->sectionActivated()|rpl::on_next([&](int i){++events;last=i;},life);s->focused=true;QApplication::current=s.get();QFocusEvent focus{Qt::TabFocusReason};s->focusInEvent(&focus);assert(s->focusEvents==0&&"native focus-in needs no duplicate child announcement");
QKeyEvent right(Qt::Key_Right);s->keyPressEvent(&right);assert(right.accepted&&events==0&&s->accessibilityChildState(1).focused&&s->accessibilityChildState(0).selected);
QKeyEvent enter(Qt::Key_Return);s->keyPressEvent(&enter);assert(events==1&&last==1&&s->activeSection()==1&&s->accessibilityChildState(0).selected&&!s->accessibilityChildState(1).selected);QKeyEvent repeat(Qt::Key_Return,0,true);s->keyPressEvent(&repeat);assert(repeat.accepted&&events==1);QKeyEvent release(Qt::Key_Return);s->keyReleaseEvent(&release);assert(release.accepted&&events==1);
owner._currentTabType=SelectorTab::Stickers;s->notifySelectionChanged(0);crl::drain();assert(s->accessibilityChildState(1).selected&&!s->accessibilityChildState(0).selected);assert(s->accessibilityChildName(1)=="Stickers");auto a=s->accessibilityChildIdentity(0),b=s->accessibilityChildIdentity(1),c=s->accessibilityChildIdentity(2);assert(a&&b&&c&&a!=b&&b!=c);
s->setTabs({SelectorTab::Gifs,SelectorTab::Emoji},{"GIF","Emoji long"});crl::drain();assert(s->accessibilityChildIndexByIdentity(c)==0&&s->accessibilityChildIndexByIdentity(a)==1&&s->accessibilityChildIndexByIdentity(b)==-1);s->setTabs({SelectorTab::Gifs,SelectorTab::Emoji,SelectorTab::Stickers},{"GIF","Emoji","Stickers"});crl::drain();assert(s->accessibilityChildIdentity(2)!=b&&s->accessibilityChildIndexByIdentity(b)==-1);
s->accessibilityChildActivate(b);crl::drain();assert(events==1);auto fresh=s->accessibilityChildIdentity(2);s->accessibilityChildActivate(fresh);s->setTabs({SelectorTab::Emoji},{"Emoji"});crl::drain();assert(events==1);
for(int mod:{Qt::ControlModifier,Qt::AltModifier,Qt::MetaModifier}){QKeyEvent key(Qt::Key_Space,mod);s->keyPressEvent(&key);assert(!key.accepted&&events==1);}s->enabled=false;QKeyEvent disabled(Qt::Key_Space);s->keyPressEvent(&disabled);assert(events==1&&!disabled.accepted&&s->accessibilityChildState(0).disabled);s->enabled=true;s->visible=false;QKeyEvent hidden(Qt::Key_Space);s->keyPressEvent(&hidden);assert(events==1&&!hidden.accepted);s->visible=true;
s->setTabs({SelectorTab::Emoji,SelectorTab::Stickers,SelectorTab::Gifs},{"E","S","G"});crl::drain();owner._currentTabType=SelectorTab::Emoji;s->focusInEvent(&focus);style::rtl=true;QKeyEvent left(Qt::Key_Left);s->keyPressEvent(&left);assert(s->accessibilityChildState(1).focused&&s->accessibilityChildRect(0).x()==200);style::rtl=false;QKeyEvent end(Qt::Key_End);s->keyPressEvent(&end);assert(s->accessibilityChildState(2).focused);QKeyEvent home(Qt::Key_Home);s->keyPressEvent(&home);assert(s->accessibilityChildState(0).focused);
auto id=s->accessibilityChildIdentity(1);QWidget external;s->onFocus=[&]{s->focused=false;QApplication::current=&external;};s->accessibilityChildActivate(id);crl::drain();assert(!s->hasFocus()&&QApplication::current==&external&&events==1&&"child focus observer intent must not be stolen back");s->onFocus={};s->focused=true;QApplication::current=s.get();owner._slideGeneration=0;s->_focused=0;s->onFocus=[&]{++owner._slideGeneration;};s->accessibilityChildActivate(id);crl::drain();assert(events==1&&"new owner epoch cancels original child activation");s->onFocus={};
s->accessibilityChildActivate(id);++owner._slideGeneration;crl::drain();assert(events==1&&"queued action cannot cross owner lifecycle epoch");s->_focused=0;s->onFocus=[&]{s->enabled=false;};s->accessibilityChildSetFocus(id);crl::drain();assert(events==1&&!s->enabled);s->onFocus={};s->enabled=true;s->_focused=0;QMouseEvent mouse{{150,0}};s->mousePressEvent(&mouse);s->mouseReleaseEvent(&mouse);assert(events==2&&last==1&&s->_focused==1&&!s->_keyboardFocus);QKeyEvent pointerEnter(Qt::Key_Return);s->keyPressEvent(&pointerEnter);assert(events==3&&last==1);
s->accessibilityChildActivate(s->accessibilityChildIdentity(1));s.reset();crl::drain();assert(events==3);
std::cout<<"Actual local Selector TabsSlider/manualkeys/committedselection/tokens/AT actions PASS; realrpl, explicitwidget/queue/weak/geometry/Qt/AT doubles; no nativepaint/focus/UIA\n";}
'''.replace('return1','return 1'), extra=[arg for p in ('Telegram/lib_rpl', 'Telegram/lib_base', 'Telegram/ThirdParty/GSL/include') for arg in ('-I', str(Path(__file__).resolve().parents[2] / p))])


if __name__ == '__main__':
    import tempfile
    with tempfile.TemporaryDirectory(prefix='luxury-selector-tabs-') as directory:
        check(Path(directory))
