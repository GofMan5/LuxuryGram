"""Actual panel owner callbacks/hide/destructor; mechanical reparent-hidden selector."""
from pathlib import Path
from library_regressions import cpp, function, source


def check(root):
    panel = source('Telegram/SourceFiles/chat_helpers/tabbed_panel.cpp')
    callbacks = panel[panel.index('\t_selector->setAfterShownCallback('):panel.index('\n\t_selector->showRequests(')]
    methods = '\n'.join(function(panel, sig) for sig in (
        'void TabbedPanel::hideFast()',
        'void TabbedPanel::hideFinished()',
        'TabbedPanel::~TabbedPanel()'))
    if 'void TabbedPanel::setAnimationsPaused(bool paused)' in panel:
        methods += '\n' + function(panel, 'void TabbedPanel::setAnimationsPaused(bool paused)')
    cpp(root, 'tabbed_panel', r'''
#include <cassert>
#include <functional>
#include <iostream>
#include <vector>
using SelectorTab=int;
struct QPixmap{};
struct Animation{void stop(){}};
struct Timer{void cancel(){}};
struct Optional{void reset(){}};
struct Stream{std::vector<bool>events;void fire(bool v){events.push_back(v);}void fire_copy(bool v){fire(v);}};
struct Controller{bool paused=false;int acquired=0,released=0,taken=0;void enableGifPauseReason(int){paused=true;++acquired;}void disableGifPauseReason(int){paused=false;++released;}template<class T>void takeTabbedSelectorOwnershipFrom(T*){++taken;}};
struct Selector{bool hidden=false,restricted=false,transferred=false;int finalHides=0,before=0;std::function<void(int)>after,beforeCallback;bool isHidden(){return hidden;}int level(){return 1;}template<class F>void setAfterShownCallback(F f){after=f;}template<class F>void setBeforeHidingCallback(F f){beforeCallback=f;}void beforeHiding(){++before;if(!restricted&&beforeCallback)beforeCallback(0);}void hideFinished(){++finalHides;}};
class TabbedPanel{public:Controller*_regularWindow;Selector*_selector;bool _ownedSelector=false,hidden=false,_hiding=false,_shouldFinishHide=false,_animationsPaused=false;Stream _pauseAnimations;Timer _hideTimer;Animation _a_opacity,_a_show;Optional _showAnimation;QPixmap _cache;TabbedPanel(Controller*c,Selector*s):_regularWindow(c),_selector(s){
''' + callbacks + r'''
}bool isHidden(){return hidden;}void hide(){hidden=true;}void hideFast();void hideFinished();void setAnimationsPaused(bool paused);~TabbedPanel();};
''' + methods + r'''
int main(){int cases=0;for(bool restricted:{false,true})for(bool childHidden:{false,true})for(bool panelHidden:{false,true}){Controller c;Selector s;s.restricted=restricted;{TabbedPanel p(&c,&s);s.after(0);s.after(0);assert(c.paused&&c.acquired==1);s.hidden=childHidden;p.hidden=panelHidden;}assert(!c.paused);assert(c.released==1);++cases;}for(bool restricted:{false,true}){Controller c;Selector s;s.restricted=restricted;{TabbedPanel p(&c,&s);s.after(0);p.hideFinished();assert(!c.paused&&c.released==1);p.hidden=false;s.after(0);assert(c.paused&&c.acquired==2);s.beforeHiding();p.hideFinished();assert(!c.paused&&c.released==2);}assert(c.released==2);++cases;}for(bool childHidden:{false,true}){Selector s;{TabbedPanel p(nullptr,&s);s.after(0);s.hidden=childHidden;}++cases;}std::cout<<"Actual panel owner callbacks/hide/destructor PASS:"<<cases<<" acquiredbit releases on restricted/hidden/transfer-simulated destruction; mechanical Qt/rpl/native reparent and controller, no renderer/full panel/AT\n";}
''')


if __name__ == '__main__':
    import tempfile
    with tempfile.TemporaryDirectory(prefix='luxury-tabbed-panel-') as directory:
        check(Path(directory))
