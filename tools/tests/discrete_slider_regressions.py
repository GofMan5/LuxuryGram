"""Actual DiscreteSlider activation/Fast callbacks with real rpl and an explicit weak-lifetime double.
Selected/animation/timer doubles; destroyed-listener check instruments post-death writes.
No native QObject/QWidget/Qt event loop or production-owner reachability assertion.
"""
from pathlib import Path
from library_regressions import cpp, function, source


def check(root, mode=None):
    text = source('Telegram/SourceFiles/ui/widgets/discrete_sliders.cpp')
    methods = '\n'.join(function(text, sig) for sig in (
        'void DiscreteSlider::setActiveSection(int index)',
        'void DiscreteSlider::activateCallback()',
        'void DiscreteSlider::setActiveSectionFast(int index)',
        'void DiscreteSlider::finishAnimating()'))
    cpp(root, 'discrete_slider', r'''
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <utility>
#include <vector>
#include "rpl/event_stream.h"
namespace base{struct has_weak_ptr{std::shared_ptr<bool> alive=std::make_shared<bool>(true);~has_weak_ptr(){*alive=false;}};struct Weak{std::weak_ptr<bool> alive;explicit operator bool()const{auto p=alive.lock();return p&&*p;}};template<class T>Weak make_weak(T*p){return {p->alive};}}
namespace base::assertion{void log(const char*,const char*,int){std::abort();}}
namespace crl{using time=long long;time clock=1000;time now(){return clock;}}
namespace Qt{constexpr auto PreciseTimer=0;}
int postWrites=0,finishWrites=0;bool exists=true;
class DiscreteSlider:public base::has_weak_ptr{public:
 int _activeIndex=0,_selected=0,_timerId=-1;crl::time _callbackAfterMs=0;
 int killed=0,started=0;rpl::event_stream<int>_sectionActivated;struct Animation{void stop(){assert(exists&&"finish animation after destructive callback");++finishWrites;}};Animation _a_left,_a_width;
 ~DiscreteSlider(){exists=false;}
 void setActiveSection(int);void activateCallback();void setActiveSectionFast(int);void finishAnimating();
 void setSelectedSection(int index){assert(exists&&"selected write after destructive callback");_selected=index;++postWrites;}void killTimer(int){++killed;}int startTimer(crl::time,int){++started;return 1;}void update(){assert(exists);}
};
''' + methods + r'''
int main(){int cases=0;
''' + (r'''
for(bool fast:{false,true}){exists=true;DiscreteSlider slider;rpl::lifetime life;std::vector<int>events;
slider._sectionActivated.events()|rpl::on_next([&](int index){events.push_back(index);},life);
if(fast)slider.setActiveSectionFast(1);else slider.setActiveSection(1);
assert(slider._activeIndex==1&&slider._selected==1);assert((events==std::vector<int>{1}));++cases;}
''' if mode in (None, 1) else '') + (r'''
for(bool fast:{false,true}){exists=true;DiscreteSlider slider;rpl::lifetime life;std::vector<int>events;
slider._sectionActivated.events()|rpl::on_next([&](int index){events.push_back(index);if(index==1)slider.setActiveSection(2);},life);
if(fast)slider.setActiveSectionFast(1);else slider.setActiveSection(1);
assert((slider._activeIndex==2&&slider._selected==2)&&"outer activation must not undo synchronous nested section");assert((events==std::vector<int>{1,2}));++cases;}
''' if mode in (None, 0) else '') + (r'''
for(bool fast:{false,true}){exists=true;auto slider=std::make_unique<DiscreteSlider>();rpl::lifetime life;
slider->_sectionActivated.events()|rpl::on_next([&](int){slider.reset();},life);
const auto raw=slider.get();if(fast)raw->setActiveSectionFast(1);else raw->setActiveSection(1);
assert(!slider&&!exists);++cases;}
''' if mode in (None, 2) else '') + r'''
{exists=true;DiscreteSlider slider;rpl::lifetime life;std::vector<int>events;slider._callbackAfterMs=1100;slider._timerId=7;
slider._sectionActivated.events()|rpl::on_next([&](int index){events.push_back(index);},life);
slider.setActiveSection(1);assert(events.empty()&&slider._timerId==1&&slider.killed==1&&slider.started==1&&slider._selected==1);
crl::clock=1100;slider.activateCallback();assert((events==std::vector<int>{1})&&slider._timerId==-1&&slider.killed==2);++cases;}
{exists=true;crl::clock=1000;DiscreteSlider slider;rpl::lifetime life;std::vector<int>events;slider._callbackAfterMs=1100;
slider._sectionActivated.events()|rpl::on_next([&](int index){events.push_back(index);},life);
slider.setActiveSectionFast(1);assert((events==std::vector<int>{1})&&slider._selected==1&&slider._callbackAfterMs==0&&slider._timerId==-1);++cases;}
std::cout<<"Actual DiscreteSlider activation/Fast/finish PASS:"<<cases<<" normal/nested/destructive controls; real-rpl, mechanical weak/selection/animation/timer, no native QObject/Qt/AT owner proof\n";}
''', extra=[arg for p in ('Telegram/lib_rpl', 'Telegram/lib_base', 'Telegram/ThirdParty/GSL/include') for arg in ('-I', str(Path(__file__).resolve().parents[2] / p))])


if __name__ == '__main__':
    import sys, tempfile
    with tempfile.TemporaryDirectory(prefix='luxury-discrete-slider-') as directory:
        check(Path(directory), int(sys.argv[1]) if len(sys.argv) > 1 else None)
