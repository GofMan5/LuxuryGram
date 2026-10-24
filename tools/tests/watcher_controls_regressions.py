"""Actual watcher choice state and owner loops; explicit widget/AT/queue doubles, no native Qt."""
from pathlib import Path
import tempfile
from library_regressions import cpp, function, source


def check(root, masks_only=False):
    text = source('Telegram/SourceFiles/luxury/ui/watcher/watcher_components.cpp')
    klass = function(text, 'class ChoiceButton final') + ';'
    methods = '\n'.join(function(text, signature) for signature in (
        'ChoiceButton::ChoiceButton(', 'void ChoiceButton::setSelected(',
        'void SegmentControl::setIndex(', 'void TabsBar::setIndex('))
    state = function(source('Telegram/lib_ui/ui/rp_widget.h'), 'struct AccessibilityState') + ';'
    cpp(root, 'watcher_choices', r'''
#include <array>
#include <cassert>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>
using QString=std::string;template<class T>using Fn=std::function<T>;
namespace Qt{enum FocusPolicy{NoFocus,StrongFocus};}
struct QPaintEvent{};struct QFocusEvent{};
namespace QAccessible{enum Role{Button,PageTab,Link};struct State{};}
struct QWidget{std::shared_ptr<bool>alive=std::make_shared<bool>(true);virtual~QWidget(){*alive=false;}};
namespace crl{struct Task{std::weak_ptr<bool>alive;Fn<void()>f;};std::vector<Task>tasks;template<class T,class F>void on_main(T*p,F f){tasks.push_back({p->alive,f});}void drain(){auto work=std::exchange(tasks,{});for(auto&t:work)if(auto a=t.alive.lock();a&&*a)t.f();}}
namespace st{constexpr int luxuryWatcherControl=0;}
namespace Ui{
''' + state + r'''
struct AbstractButton:QWidget{bool down=false,tab=false;virtual QAccessible::Role accessibilityRole(){return tab?QAccessible::PageTab:QAccessible::Button;}virtual AccessibilityState accessibilityState()const{return{};}bool isDown()const{return down;}};
struct LinkButton:AbstractButton{int updates=0,notifications=0;QString name;bool requireMasks=true;Fn<void()>onState;LinkButton(QWidget*,QString s,int):name(s){}virtual void paintEvent(QPaintEvent*){}virtual void focusInEvent(QFocusEvent*){}virtual void focusOutEvent(QFocusEvent*){}void setFocusPolicy(Qt::FocusPolicy){}void setToolTip(const QString&){}void update(){assert(*alive);++updates;}void accessibilityStateChanged(AccessibilityState s){if(requireMasks)assert(s.checked&&s.selected&&!s.pressed&&!s.checkable);++notifications;if(onState){auto f=onState;f();}}};
}
namespace LuxuryUi{
''' + klass + r'''
void ChoiceButton::paintEvent(QPaintEvent*){}
struct SegmentControl{std::array<ChoiceButton*,3>_buttons;bool _lastEnabled=true;void setIndex(int);};
struct TabsBar{std::array<ChoiceButton*,3>_buttons;void setIndex(int);};
''' + methods + r'''
}
int main(){using namespace LuxuryUi;
constexpr bool masksOnly=MASKS_ONLY;
if(masksOnly){ChoiceButton b(nullptr,"Choice");b.setSelected(true);crl::drain();assert(b.notifications==1&&b.accessibilityState().checked);b.setSelected(false);crl::drain();assert(b.notifications==2&&!b.accessibilityState().checked);std::cout<<"Watcher actual choice change masks PASS in both directions\n";return 0;}
ChoiceButton first(nullptr,"All"),second(nullptr,"Sessions"),third(nullptr,"Events");for(auto*p:{&first,&second,&third})p->requireMasks=false;
SegmentControl segment{{&first,&second,&third}};segment.setIndex(0);assert(first.notifications==0);crl::drain();assert(first.notifications==1);
for(auto*p:{&first,&second,&third}){p->requireMasks=true;p->onState=[&]{assert(!first.accessibilityState().selected&&second.accessibilityState().selected&&!third.accessibilityState().selected);};}
segment.setIndex(1);assert(first.notifications==1&&second.notifications==0);crl::drain();assert(first.notifications==2&&second.notifications==1);segment.setIndex(1);crl::drain();assert(first.notifications==2&&second.notifications==1);
segment._lastEnabled=false;segment.setIndex(2);segment.setIndex(-1);segment.setIndex(3);crl::drain();assert(second.accessibilityState().selected);
for(auto*p:{&first,&second,&third})p->onState={};TabsBar tabs{{&first,&second,&third}};tabs.setIndex(2);assert(third.accessibilityState().selected&&!second.accessibilityState().selected);crl::drain();assert(second.notifications==2&&third.notifications==1);
auto b=std::make_unique<ChoiceButton>(nullptr,"Destroy");b->onState=[&]{b.reset();};b->setSelected(true);assert(b);crl::drain();assert(!b);
b=std::make_unique<ChoiceButton>(nullptr,"Queued");b->setSelected(true);b.reset();crl::drain();
std::cout<<"Watcher actual choice+segment/tab loops PASS: changed-field masks on select/deselect, equal no-op, owner state committed before AT, bounds/disabled-last, notification deletion and queued teardown; no native UIA/render\n";
}
'''.replace('MASKS_ONLY', str(masks_only).lower()))


if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument('--masks-only', action='store_true')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='luxury-watcher-choices-') as directory:
        check(Path(directory), args.masks_only)
