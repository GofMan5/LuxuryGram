"""Actual complete Selector switchTab and lifecycle/Inner scope/search focus methods.
Mechanical Qt/raster/session/scroll/hook/connection/weak doubles; real callback-boundary assertions.
No native Qt window/focus event loop/reparent/raster/UIA/account execution.
"""
from pathlib import Path
from library_regressions import cpp, function, source


def check(root):
    s = source('Telegram/SourceFiles/chat_helpers/tabbed_selector.cpp')
    search = source('Telegram/SourceFiles/ui/controls/tabbed_search.cpp')
    methods = '\n'.join(function(s, sig) for sig in ('void TabbedSelector::switchTab()', 'void TabbedSelector::beforeHiding()', 'void TabbedSelector::afterShown()', 'void TabbedSelector::Inner::afterShownWithSearchFocus('))
    steal = function(search, 'void SearchWithGroups::stealFocus()')
    cpp(root, 'selector_switch_focus', r'''
#include <cassert>
#include <functional>
#include <iostream>
#include <memory>
#include <utility>
#include <vector>
#define Expects(x) assert(x)
#define Assert(x) assert(x)
using uint64=unsigned long long;template<class T>using Fn=std::function<T>;
namespace Qt{enum FocusReason{OtherFocusReason};}
struct QWidget;
namespace QMetaObject{struct Connection{int id=0;};}
struct QObject{static std::vector<std::pair<int,std::function<void(QWidget*,QWidget*)>>>listeners;template<class A,class B,class T,class F>static QMetaObject::Connection connect(A*,B,T*,F f){int id=int(listeners.size())+1;listeners.push_back({id,f});return{id};}static void disconnect(QMetaObject::Connection c){for(auto&v:listeners)if(v.first==c.id)v.second={};}};
std::vector<std::pair<int,std::function<void(QWidget*,QWidget*)>>>QObject::listeners;
struct QApplication{static QWidget*current;static QWidget*focusWidget(){return current;}void focusChanged(QWidget*,QWidget*){}};QApplication app,*qApp=&app;QWidget*QApplication::current=nullptr;
struct QWidget{std::shared_ptr<bool>alive=std::make_shared<bool>(true);bool visible=true,enabled=true;Fn<void()>onFocus;virtual~QWidget(){*alive=false;}bool hasFocus()const{return QApplication::current==this;}bool isVisible()const{return visible;}bool isEnabled()const{return enabled;}void setFocus(Qt::FocusReason=Qt::OtherFocusReason){auto from=QApplication::current;QApplication::current=this;auto ls=QObject::listeners;for(auto&v:ls)if(v.second)v.second(from,this);if(onFocus)onFocus();}void setParent(QWidget*){}void hide(){visible=false;}void show(){visible=true;}bool isHidden()const{return!visible;}};
namespace base{template<class T>struct Weak{T*p;std::weak_ptr<bool>a;explicit operator bool()const{auto x=a.lock();return x&&*x;}T*operator->()const{return p;}};template<class T>Weak<T>make_weak(T*p){return{p,p->alive};}}
template<class T>struct QPointer{base::Weak<T>weak;explicit QPointer(T*p):weak(base::make_weak(p)){}T*data()const{return weak?weak.p:nullptr;}T*operator->()const{return data();}explicit operator bool()const{return data();}friend bool operator==(T*p,const QPointer&q){return p==q.data();}};
namespace gsl{template<class F>struct Guard{F f;~Guard(){f();}};template<class F>Guard<F>finally(F f){return{f};}}
namespace style{int DevicePixelRatio(){return 1;}}namespace st{int emojiPanRadius=8,emojiPanSlideDuration=1;}namespace anim{int linear=0;}
struct QRect{QRect(int,int,int,int){}};namespace Images{int CornersMask(int){return 0;}}
namespace Ui{bool InFocusChain(QWidget*p){return QApplication::current==p;}}
namespace Ui{class SearchWithGroups{public:QWidget*_field=nullptr,*_focusTakenFrom=nullptr;Fn<bool()>_allowAutoFocus;void stealFocus();};}
namespace ChatHelpers{enum class SelectorTab{Emoji,Stickers,Gifs};
class TabbedSelector:public QWidget{public:
 struct Inner:QWidget{bool _allowSearchAutoFocus=true;int before=0,after=0,hidden=0;Fn<void()>beforeHook,afterHook,hiddenHook;void beforeHiding(){++before;if(beforeHook)beforeHook();}void afterShown(){++after;if(afterHook)afterHook();}void afterShownWithSearchFocus(bool);void hideFinished(){++hidden;if(hiddenHook)hiddenHook();}void refreshRecent(){}void preloadImages(){}};
 struct Tab{Inner*inner;QWidget foot;Inner*widget(){return inner;}bool hasFooter(){return true;}QWidget*footer(){return &foot;}void saveScrollTop(){}void returnWidget(std::unique_ptr<Inner>p){inner=p.release();}};
 struct Slider:QWidget{int active=1,notifies=0;int activeSection(){return active;}void notifySelectionChanged(int){++notifies;}}slider;
 template<class T>struct Ptr{T*p;T*data()const{return p;}T*operator->()const{return p;}explicit operator bool()const{return p;}};
 Ptr<Slider>_tabsSlider{&slider};
 struct Scroll:QWidget{Inner*inner;int y(){return 40;}void scrollToY(int){}template<class T>std::unique_ptr<T>takeWidget(){return std::unique_ptr<T>(std::exchange(inner,nullptr));}}scroll;
 Ptr<Scroll>_scroll{&scroll};
 struct Label{void destroy(){}}_restrictedLabel;
 struct SlideAnimation{enum class Direction{LeftToRight,RightToLeft};template<class...T>void setFinalImages(T...){}void setCornerMasks(int){}void start(){}};
 std::unique_ptr<SlideAnimation>_slideAnimation;struct Animation{bool animating()const{return false;}template<class...T>void start(T...){}}_a_slide;
 uint64 _slideGeneration=0;bool _slideFromTabs=false,_dropDown=false;int _footerTop=0;SelectorTab _currentTabType=SelectorTab::Emoji;
 Fn<void(SelectorTab)>_beforeHidingCallback,_afterShownCallback;QWidget root,external,field;std::vector<Tab>_tabs;int grabs=0,shows=0,updates=0;Fn<void(int)>grabHook;
 TabbedSelector(){for(int i=0;i!=3;++i)_tabs.push_back({new Inner,{}});scroll.inner=_tabs[0].inner;}
 ~TabbedSelector(){for(auto&t:_tabs)delete t.inner;}
 bool tabbed(){return true;}bool full(){return false;}bool hasSectionIcons(){return true;}int indexByType(SelectorTab t){return int(t);}SelectorTab typeByIndex(int i){return SelectorTab(i);}Tab*currentTab(){return &_tabs[int(_currentTabType)];}Tab*getTab(int i){return &_tabs[i];}QWidget*window(){return &root;}int width(){return 300;}int height(){return 400;}
 void checkRestrictedPeer(){}void setWidgetToScrollArea(){scroll.inner=currentTab()->inner;}void update(){++updates;}
 int grabForAnimation(){slider.hide();if(grabHook)grabHook(++grabs);return grabs;}void hideForSliding(){slider.show();}void showAll(){++shows;slider.show();}void switchTab();void beforeHiding();void afterShown();
 struct Settings{void setSelectorTab(SelectorTab){}};struct Session{Settings s;Settings&settings(){return s;}void saveSettingsDelayed(){}}sess;Session&session(){return sess;}
};
''' + methods + r'''}namespace Ui{''' + steal + r'''}
int main(){using namespace ChatHelpers;int cases=0;
for(int cancel=0;cancel!=4;++cancel){TabbedSelector s;s.slider.setFocus();Ui::SearchWithGroups search;search._field=&s.field;search._allowAutoFocus=[&]{return s.currentTab()->inner->_allowSearchAutoFocus;};s._tabs[1].inner->afterHook=[&]{search.stealFocus();};int before=0,after=0;s._beforeHidingCallback=[&](auto){++before;};s._afterShownCallback=[&](auto){++after;};s.grabHook=[&](int n){if(n==1&&cancel){s.external.setFocus();if(cancel==2)s.root.setFocus();if(cancel==3)s.slider.setFocus();}};s.switchTab();assert(s._currentTabType==SelectorTab::Stickers&&s.grabs==2&&s.slider.visible&&s._slideFromTabs&&s._tabs[0].inner->before==1&&s._tabs[0].inner->hidden==1&&before==1);assert(s.slider.hasFocus()==(cancel==0||cancel==3));assert(s.slider.notifies==1);auto target=QApplication::current;s.afterShown();assert(QApplication::current==target&&s._tabs[1].inner->after==1&&after==1&&search._focusTakenFrom==target&&s._tabs[1].inner->_allowSearchAutoFocus);++cases;}
{TabbedSelector s;s.external.setFocus();Ui::SearchWithGroups search;search._field=&s.field;search._allowAutoFocus=[&]{return s.currentTab()->inner->_allowSearchAutoFocus;};s._tabs[1].inner->afterHook=[&]{search.stealFocus();};s.switchTab();assert(!s._slideFromTabs&&s.external.hasFocus());s.afterShown();assert(s.field.hasFocus());++cases;}
{auto s=std::make_unique<TabbedSelector>();s->slider.setFocus();s->_beforeHidingCallback=[&](auto){s.reset();};s->switchTab();assert(!s);++cases;}
{TabbedSelector s;s.slider.setFocus();s._beforeHidingCallback=[&](auto){++s._slideGeneration;};s.switchTab();assert(s.grabs==0&&s._currentTabType==SelectorTab::Emoji);++cases;}
{TabbedSelector s;s.slider.setFocus();s.grabHook=[&](int){++s._slideGeneration;};s.switchTab();assert(s.grabs==1&&s._currentTabType==SelectorTab::Emoji);++cases;}
std::cout<<"Actual complete Selector switchTab park/restore/focus-away-ABA/search/lifecycle/callbackdelete PASS:"<<cases<<" mechanicalQt/weak/connection/raster/session; no nativefocus/reparent/UIA\n";}
'''.replace('return0','return 0'))


if __name__ == '__main__':
    import tempfile
    with tempfile.TemporaryDirectory(prefix='luxury-selector-switch-focus-') as directory:
        check(Path(directory))
