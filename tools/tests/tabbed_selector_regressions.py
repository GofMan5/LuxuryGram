"""Actual selector restriction methods, tab-reset slice and swipe init path.

Mechanical Qt/tab/peer/gesture doubles, source policy tables not full Telegram.
No native animation/touch/font/AT/account execution.
"""
from pathlib import Path
from library_regressions import cpp, function, source


def check(root, case='all'):
    selector = source('Telegram/SourceFiles/chat_helpers/tabbed_selector.cpp')
    methods = '\n'.join(function(selector, sig) for sig in (
        'void TabbedSelector::checkRestrictedPeer()',
        'bool TabbedSelector::isRestrictedView()',
        'void TabbedSelector::showAll()',
        'void TabbedSelector::beforeHiding()',
        'void TabbedSelector::afterShown()',
        'void TabbedSelector::reinstallSwipe(not_null<Inner*> widget)'))
    switch = function(selector, 'void TabbedSelector::switchTab()')
    reset = switch[switch.index('\t_currentTabType = newTabType;'):switch.index('\n\tcurrentTab()->widget()->refreshRecent();')]
    cpp(root, 'tabbed_selector', r'''
#include <algorithm>
#include <cassert>
#include <cmath>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#define Expects(x) assert(x)
#define Assert(x) assert(x)
using QString=std::string;
struct QColor{int c=0;};
struct Color{QColor c;};
template<class T>using not_null=T;
template<class T>using Fn=std::function<T>;
struct TextWithEntities{QString text;};
struct QPoint{int x()const{return 0;}int y()const{return 0;}QPoint toPoint()const{return {};}};
struct QWheelEvent{QPoint globalPosition(){return {};}};
namespace Qt{enum Direction{LeftToRight,RightToLeft};}
namespace rpl{template<class T>T single(T t){return t;}}
namespace tr{TextWithEntities link(const QString &s){return{s};}}
namespace st{int stickersRestrictedLabel=0;Color color;Color *historyForwardChooseBg=&color,*historyForwardChooseFg=&color;}
namespace ChatRestriction{enum Value{SendStickers,SendGifs,SendInline,SendOther};}
struct Channel{};
struct PeerData{Channel channel;Channel*asChannel(){return &channel;}};
namespace Data{struct SendError{QString text;int boostsToLift=0;explicit operator bool()const{return!text.empty();}};SendError error[4];SendError RestrictionError(PeerData*,ChatRestriction::Value v){return error[v];}}
struct Window{int focus=0,boost=0;Channel*resolved=nullptr;void setFocus(){++focus;}void resolveBoostState(Channel*c,int b){boost=b;resolved=c;}};
struct Show{Window window;Window*resolveWindow(){return &window;}};
struct Widget{bool hidden=false;int shown=0,hides=0,before=0,after=0;void show(){hidden=false;++shown;}void hide(){hidden=true;++hides;}bool isHidden()const{return hidden;}void setVisible(bool v){hidden=!v;}void beforeHiding(){++before;}void afterShown(){++after;}};
struct Label:Widget{TextWithEntities content;Fn<bool()> click;template<class F>void setClickHandlerFilter(F f){click=f;}};
struct LabelPtr{std::unique_ptr<Label> value;int creates=0,destroys=0;template<class T>void create(T*,TextWithEntities content,int){value=std::make_unique<Label>();value->content=std::move(content);++creates;}void destroy(){if(value)++destroys;value.reset();}Label*operator->()const{return value.get();}explicit operator bool()const{return bool(value);}friend bool operator!=(const LabelPtr&p,std::nullptr_t){return bool(p);}};
namespace base{struct Weak{explicit operator bool()const{return true;}};template<class T>Weak make_weak(T*){return {};}}
namespace Ui{bool focusChain=false;template<class T>bool InFocusChain(T*){return focusChain;}QPoint ScrollDelta(QWheelEvent*){return{};}namespace Controls{struct SwipeContextData{int translation=0;};struct SwipeHandlerInitData{Qt::Direction direction;QPoint cursorPosition;};struct SwipeHandlerFinishData{Fn<void()> callback;};struct Lifetime{bool alive=false;void destroy(){alive=false;}explicit operator bool()const{return alive;}};struct SwipeBackData{Fn<void(SwipeContextData)>callback;Lifetime lifetime;};template<class...T>SwipeBackData SetupSwipeBack(T...){return{[](SwipeContextData){},{true}};}template<class F>SwipeHandlerFinishData DefaultSwipeBackHandlerFinishData(F f){return{f};}struct Descriptor{void*widget;void*scroll;Fn<void(SwipeContextData)>update;Fn<SwipeHandlerFinishData(SwipeHandlerInitData)>init;std::nullptr_t dontStart;Fn<bool(QWheelEvent*)>skipWheelEvent;Lifetime*onLifetime;};Descriptor latest;void SetupSwipeHandler(Descriptor d){latest=std::move(d);}}}
namespace ChatHelpers{enum class SelectorTab{Emoji,Stickers,Gifs,Masks};class TabbedSelector{public:enum class Mode{Full,EmojiStatus,MediaEditor};struct Inner:Widget{bool consumes=false;void afterShownWithSearchFocus(bool){afterShown();}bool canConsumeHorizontalScroll(QPoint,int){return consumes;}QPoint mapFromGlobal(QPoint p){return p;}};struct Tab{Inner inner;Widget foot;Inner*widget(){return &inner;}Widget*footer(){return &foot;}bool hasFooter()const{return true;}};struct Animation{bool active=false;bool animating()const{return active;}};struct Scroll:Widget{Scroll*data(){return this;}};struct Slider:Widget{int active=0,count=3,events=0;int activeSection(){return active;}int sectionsCount(){return count;}void setActiveSection(int i){active=i;++events;}void notifySelectionChanged(int){}};template<class T>struct Ptr{T*p;T*operator->()const{return p;}T*data()const{return p;}explicit operator bool()const{return p;}};
PeerData peer;PeerData*_currentPeer=&peer;SelectorTab _currentTabType=SelectorTab::Emoji;Mode _mode=Mode::Full;QString _restrictedLabelKey;PeerData*_restrictedPeer=nullptr;int _restrictedBoostsToLift=0;LabelPtr _restrictedLabel;std::shared_ptr<Show>_show=std::make_shared<Show>();Scroll scroll;Ptr<Scroll>_scroll{&scroll};Widget bottom,top;Slider slider;Ptr<Widget>_bottomShadow{&bottom},_topShadow{&top};Ptr<Slider>_tabsSlider{&slider};std::vector<Tab>_tabs=std::vector<Tab>(3);Animation _a_slide;unsigned long long _slideGeneration=0;bool _slideFromTabs=false;Fn<void(SelectorTab)>_beforeHidingCallback,_afterShownCallback;Ui::Controls::Lifetime _swipeLifetime;Ui::Controls::SwipeBackData _swipeBackData;int geometry=0,updates=0;Tab*currentTab(){return &_tabs[int(_currentTabType)];}void updateRestrictedLabelGeometry(){++geometry;}void update(){++updates;}Window*window(){return &_show->window;}void checkRestrictedPeer();bool isRestrictedView();void showAll();void beforeHiding();void afterShown();void reinstallSwipe(not_null<Inner*>widget);void switchReset(SelectorTab newTabType){const auto wasIndex=int(_currentTabType);
''' + reset + r'''
}};
''' + methods + r'''}
int main(int argc,char**argv){using namespace ChatHelpers;std::string mode=argc>1?argv[1]:"all";int cases=0;if(mode=="all"||mode=="restriction"){for(auto first:{SelectorTab::Emoji,SelectorTab::Stickers,SelectorTab::Gifs})for(auto next:{SelectorTab::Emoji,SelectorTab::Stickers,SelectorTab::Gifs})if(first!=next){TabbedSelector s;for(auto&e:Data::error)e={"same boost group",3};s._currentTabType=first;s.showAll();assert(s._restrictedLabel&&s.scroll.hidden);s.switchReset(next);s.showAll();assert(s._restrictedLabel);assert(s.scroll.hidden&&s.currentTab()->foot.hidden);assert(s._restrictedLabel->content.text=="same boost group");assert(s._restrictedLabel->click()==false&&s._show->window.boost==3);++cases;Data::error[ChatRestriction::SendStickers]={};s.switchReset(SelectorTab::Stickers);s.showAll();assert(!s._restrictedLabel&&!s.scroll.hidden&&!s.currentTab()->foot.hidden);s._currentPeer=nullptr;s.checkRestrictedPeer();assert(s._restrictedLabelKey.empty());++cases;}TabbedSelector s;for(auto&e:Data::error)e={"same boost group",3};s.showAll();auto unchanged=s._restrictedLabel.creates;s.showAll();assert(s._restrictedLabel.creates==unchanged);PeerData nextPeer;s._currentPeer=&nextPeer;s.checkRestrictedPeer();s._restrictedLabel->click();assert(s.window()->resolved==nextPeer.asChannel()&&s.window()->boost==3);for(auto&e:Data::error)e={"same boost group",5};s.checkRestrictedPeer();s._restrictedLabel->click();assert(s.window()->resolved==nextPeer.asChannel()&&s.window()->boost==5);s._currentPeer=nullptr;s.checkRestrictedPeer();assert(!s._restrictedLabel&&!s.scroll.hidden);cases+=3;}
if(mode=="all"||mode=="pause"){for(bool restricted:{false,true}){TabbedSelector s;for(auto&e:Data::error)e=restricted?Data::SendError{"restricted",0}:Data::SendError{};bool paused=false;int starts=0,ends=0;s._afterShownCallback=[&](auto){paused=true;++starts;};s._beforeHidingCallback=[&](auto){paused=false;++ends;};s.afterShown();assert(paused&&starts==1);auto before=s.currentTab()->inner.before;Ui::focusChain=true;s.beforeHiding();assert(!paused&&ends==1);assert(s.currentTab()->inner.before==before+!restricted);assert(s.window()->focus==1);Ui::focusChain=false;s._beforeHidingCallback={};s._afterShownCallback={};s.beforeHiding();++cases;}}
if(mode=="all"||mode=="swipe"){for(int owned:{2,3})for(int visible=1;visible<=owned;++visible)for(int active=0;active<visible;++active)for(bool left:{false,true})for(bool consumes:{false,true}){TabbedSelector s;s._tabs.resize(owned);s.slider.count=visible;s.slider.active=active;s.currentTab()->inner.consumes=consumes;s.reinstallSwipe(s.currentTab()->widget());auto result=Ui::Controls::latest.init({left?Qt::RightToLeft:Qt::LeftToRight,{}});bool allowed=!consumes&&((left&&active>0)||(!left&&active<visible-1));assert(bool(result.callback)==allowed);if(result.callback){auto invoke=result.callback;invoke();assert(s.slider.active>=0&&s.slider.active<visible&&s.slider.events==1);s.slider.active=active;s.slider.events=0;if(!left){s.slider.count=active+1;invoke();assert(s.slider.active==active&&s.slider.events==0);}}++cases;}}
std::cout<<"Actual selector restriction/switch-reset/owner-callback/swipe-init PASS:"<<cases<<" mechanical peers/glyphs/tabs/gestures, no native event/animation/touch/pausepanel/AT/backend authorization\n";}
''')


if __name__ == '__main__':
    import argparse
    import tempfile
    parser = argparse.ArgumentParser()
    parser.add_argument('--case', default='all', choices=('all', 'restriction', 'pause', 'swipe'))
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='luxury-tabbed-selector-') as directory:
        # cpp helper runs without arguments. Select mode mechanically in main.
        if args.case != 'all':
            import library_regressions
            original = library_regressions.run
            def selected_run(argv, cwd):
                return original(argv + [args.case] if len(argv) == 1 else argv, cwd)
            library_regressions.run = selected_run
        check(Path(directory))
