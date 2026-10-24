"""Actual Selector paint-queued completion and owner lifecycle methods.
Mechanical widget/visibility/animation/queue/API doubles; no native Qt painting/focus/UIA.
"""
from pathlib import Path
from library_regressions import cpp, function, source


def check(root, mode=None):
    text = source('Telegram/SourceFiles/chat_helpers/tabbed_selector.cpp')
    signatures = ('void TabbedSelector::paintEvent(',
                  'void TabbedSelector::beforeHiding()',
                  'void TabbedSelector::afterShown()',
                  'void TabbedSelector::hideFinished()',
                  'void TabbedSelector::showStarted()')
    methods = '\n'.join(function(text, sig) for sig in signatures)
    if 'bool TabbedSelector::eventHook(' in text:
        methods += '\n' + function(text, 'bool TabbedSelector::eventHook(')
    cpp(root, 'selector_completion', r'''
#include <cassert>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <utility>
#include <vector>
using uint64=unsigned long long;
struct QPaintEvent{};struct QPainter{template<class T>QPainter(T*){}};
struct QEvent{enum Type{Hide,ParentChange,Other};Type kind;Type type()const{return kind;}};
struct Widget{std::shared_ptr<bool>alive=std::make_shared<bool>(true);bool visible=true;virtual~Widget(){*alive=false;}bool isVisible()const{return visible;}virtual bool eventHook(QEvent*){return false;}};
namespace Ui{using RpWidget=Widget;bool chain=false;bool InFocusChain(Widget*){return chain;}}
namespace base{struct Weak{std::weak_ptr<bool>alive;explicit operator bool()const{auto p=alive.lock();return p&&*p;}};template<class T>Weak make_weak(T*p){return {p->alive};}}
std::vector<std::function<void()>>queue;
template<class T,class F>void InvokeQueued(T*p,F f){auto weak=base::make_weak(p);queue.push_back([weak,f]{if(weak)f();});}
void drain(){auto q=std::exchange(queue,{});for(auto&f:q)f();}
namespace ChatHelpers{using RpWidget=Ui::RpWidget;enum class SelectorTab{Emoji,Stickers,Gifs};
class TabbedSelector:public Ui::RpWidget{public:
 struct Inner{int before=0,after=0,panel=0;void beforeHiding(){++before;}void afterShown(){++after;}void afterShownWithSearchFocus(bool){afterShown();}void panelHideFinished(){++panel;}void refreshRecent(){}void preloadImages(){}};
 struct Tab{Inner inner;Inner*widget(){return &inner;}};
 struct Scroll{bool hidden=false;bool isHidden()const{return hidden;}}scroll;Scroll*_scroll=&scroll;
 struct Animation{bool active=false;bool animating()const{return active;}void stop(){active=false;}}_a_slide;
 struct API{void updateStickers(){}void updateMasks(){}void updateCustomEmoji(){}void updateSavedGifs(){}};
 struct Session{API value;API&api(){return value;}}sess;
 struct Window{void setFocus(){}}win;
 struct Event{int count=0;void fire(int){++count;}}_slideFinished;
 std::unique_ptr<int>_slideAnimation;uint64 _slideGeneration=0;bool _slideFromTabs=false;SelectorTab _currentTabType=SelectorTab::Emoji;
 std::vector<Tab>_tabs=std::vector<Tab>(3);int shown=0,paints=0,callbacks=0;
 std::function<void(SelectorTab)>_beforeHidingCallback,_afterShownCallback;
 Tab*currentTab(){return &_tabs[int(_currentTabType)];}bool hasStickersTab(){return true;}bool hasMasksTab(){return false;}bool hasEmojiTab(){return true;}bool hasGifsTab(){return true;}Session&session(){return sess;}Window*window(){return &win;}
 void showAll(){++shown;}void paintSlideFrame(QPainter&){++paints;}void paintContent(QPainter&){++paints;}
 void paintEvent(QPaintEvent*);void beforeHiding();void afterShown();void hideFinished();void showStarted();
 bool eventHook(QEvent*e)override{''' + (r'''return hook(e);}bool hook(QEvent*e);''' if 'bool TabbedSelector::eventHook(' in text else r'''return Ui::RpWidget::eventHook(e);}''') + r'''
 void schedule(){_slideAnimation=std::make_unique<int>(1);_a_slide.active=false;QPaintEvent e;paintEvent(&e);assert(!_slideAnimation&&!queue.empty());}
};
''' + (methods.replace('bool TabbedSelector::eventHook(', 'bool TabbedSelector::hook(') if 'bool TabbedSelector::eventHook(' in text else methods) + r'''
}
int main(){using namespace ChatHelpers;int cases=0;
''' + (r'''
{TabbedSelector s;s._afterShownCallback=[&](auto){++s.callbacks;};s.schedule();drain();assert(s.currentTab()->inner.after==1&&s.callbacks==1&&s._slideFinished.count==1);++cases;}
''' if mode in (None, 1) else '') + (r'''
for(int boundary=0;boundary!=5;++boundary){TabbedSelector s;s.schedule();
switch(boundary){case 0:s.beforeHiding();s.hideFinished();s.showStarted();break;case 1:s.hideFinished();break;case 2:s.showStarted();break;case 3:{QEvent e{QEvent::Hide};s.eventHook(&e);}break;case 4:{QEvent e{QEvent::ParentChange};s.eventHook(&e);}break;}
drain();assert((s.currentTab()->inner.after==0&&s._slideFinished.count==0)&&"stale finish may not reopen/announce after owner lifecycle boundary");++cases;}
''' if mode in (None, 0) else '') + (r'''
{TabbedSelector s;s.schedule();s._slideAnimation=std::make_unique<int>(2);drain();assert(s.currentTab()->inner.after==0&&s._slideFinished.count==0);++cases;}
{TabbedSelector s;s.schedule();s.beforeHiding();s.schedule();drain();assert(s.currentTab()->inner.after==1&&s._slideFinished.count==1);++cases;}
{TabbedSelector s;s._afterShownCallback=[&](auto){s.beforeHiding();s._slideAnimation=std::make_unique<int>(2);};s.schedule();drain();assert(s.currentTab()->inner.after==1&&s._slideFinished.count==0);++cases;}
{TabbedSelector s;s.schedule();s.visible=false;drain();assert(s.currentTab()->inner.after==0&&s._slideFinished.count==0);++cases;}
{TabbedSelector s;s._afterShownCallback=[&](auto){s.beforeHiding();};s.schedule();drain();assert(s.currentTab()->inner.after==1&&s._slideFinished.count==0);++cases;}
{auto s=std::make_unique<TabbedSelector>();s->_afterShownCallback=[&](auto){s.reset();};s->schedule();drain();assert(!s);++cases;}
{auto s=std::make_unique<TabbedSelector>();s->schedule();s.reset();drain();assert(!s);++cases;}
''' if mode in (None, 2) else '') + r'''
std::cout<<"Actual Selector paint queued finish/lifecycle PASS:"<<cases<<" mechanical Qt/queue/visibility/weak/animation/API; no native paint/Qt lifetime/focus/UIA proof\n";}
''')


if __name__ == '__main__':
    import sys, tempfile
    with tempfile.TemporaryDirectory(prefix='luxury-selector-completion-') as directory:
        check(Path(directory), int(sys.argv[1]) if len(sys.argv) > 1 else None)
