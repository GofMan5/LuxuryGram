"""Ghost picker production rows + menu dispatch, with real rpl and Qt doubles.

Extracts the actual row classes (painting only is stubbed), constructors,
role/name/key methods and shared menu/click handlers. No native Qt TU, raster,
UIA/Narrator, real account, settings persistence or network acceptance.
"""
from pathlib import Path
import tempfile

from library_regressions import ROOT, cpp, function, source


def check(root):
    text = source('Telegram/SourceFiles/luxury/ui/settings/settings_luxury.cpp')
    item_h = source('Telegram/lib_ui/ui/widgets/menu/menu_item_base.h')
    item = source('Telegram/lib_ui/ui/widgets/menu/menu_item_base.cpp')
    menu = source('Telegram/lib_ui/ui/widgets/menu/menu.cpp')
    common = source('Telegram/lib_ui/ui/widgets/menu/menu_common.h')
    button = source('Telegram/lib_ui/ui/abstract_button.cpp')
    classes = ''
    for name in ('AccountAction', 'GlobalAction'):
        body = function(text, f'class {name} final') + ';'
        body = body.replace(function(body, 'void paint(Painter &&p)'),
                            'void paint(Painter &&) {}')
        classes += body + '\n'
    # Only the row-local definitions, if moved out of line to follow REVIEW.md.
    methods = '\n'.join(function(text, signature) for name in
                        ('AccountAction', 'GlobalAction') for signature in (
                            f'QAccessible::Role {name}::accessibilityRole()',
                            f'QString {name}::accessibilityName()',
                            f'void {name}::handleKeyPress(')
                        if signature in text)
    cpp(root, 'ghost_picker', r'''
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <ranges>
#include <string>
#include <utility>
#include <vector>
#include <rpl/event_stream.h>
#include <rpl/merge.h>
#include <rpl/filter.h>
#include <rpl/map.h>
namespace base::assertion { void log(const char*,const char*,int){std::abort();} }
using QString=std::string;template<class T>using Fn=std::function<T>;
template<class T>struct not_null{T p;not_null(T v):p(v){assert(v);}T get()const{return p;}T operator->()const{return p;}operator T()const{return p;}};
namespace Qt{enum{NoModifier,LeftButton,Key_Up,Key_Down,Key_Enter,Key_Return,Key_Space,Key_Escape};}
struct QKeyEvent{int k;int key()const{return k;}};
namespace QAccessible{enum Role{Button,MenuItem};}
struct QAccessibleActionInterface{static QString pressAction(){return "press";}};
struct QWidget{virtual ~QWidget()=default;};
namespace base{struct qt_connection{Fn<void()>*callback=nullptr;void reset(){if(callback)*callback={};}};}
struct QAction{QString label;Fn<void()>callback;explicit QAction(QWidget*){}void setText(QString s){label=std::move(s);}QString text()const{return label;}void triggered(){}void trigger(){if(callback){auto copy=callback;copy();}}};
struct QObject{static base::qt_connection connect(QAction*a,void(QAction::*)(),Fn<void()>f){a->callback=std::move(f);return{&a->callback};}};
namespace style{struct Font{int height=18;};struct TextStyle{Font storage;Font*font=&storage;};struct Padding{int right()const{return 12;}};struct Menu{TextStyle itemStyle;Padding itemPadding;int widthMin=120,widthMax=480;};}
namespace st{struct WhoRead{int photoSkip=4,photoSize=32,nameLeft=48;}defaultWhoRead;}
struct Painter{explicit Painter(QWidget*){}};
struct UserData{QString label;QString name()const{return label;}};
namespace ranges{using std::ranges::find_if;}
namespace Ui{
struct PeerUserpicView{};
namespace Text{struct String{QString value;void setText(const style::TextStyle&,QString s){value=std::move(s);}int maxWidth()const{return int(value.size())*8;}};}
class RpWidget:public QWidget{public:rpl::lifetime life;rpl::event_stream<>paints;virtual QAccessible::Role accessibilityRole(){return QAccessible::Button;}virtual QString accessibilityName(){return{};}virtual void accessibilityDoAction(const QString&){}auto&lifetime(){return life;}auto paintRequest()const{return paints.events();}int y()const{return 0;}};
class AbstractButton:public RpWidget{public:bool disabled=false,acceptBoth=false;rpl::event_stream<>nativeClicks;bool isDisabled()const{return disabled;}void setAcceptBoth(bool v){acceptBoth=v;}auto clicks()const{return nativeClicks.events();}void clicked(int,int){nativeClicks.fire({});}void accessibilityDoAction(const QString&)override;};
''' + function(button, 'void AbstractButton::accessibilityDoAction(') + r'''
class RippleButton:public AbstractButton{};
template<class T,class P>not_null<T*>CreateChild(P*p){return new T(p);}
namespace Menu{
''' + function(common, 'enum class TriggeredSource') + ';\n' + function(common, 'struct CallbackData') + r''';
class ItemBase;
class Menu:public RpWidget{public:std::vector<not_null<QAction*>>_actions;std::vector<std::unique_ptr<ItemBase>>_actionWidgets;~Menu();ItemBase*findSelectedAction()const;void handleKeyPress(not_null<QKeyEvent*>);void setSelected(int,bool);};
class ItemBase:public RippleButton{public:ItemBase(not_null<Menu*>,const style::Menu&){}bool selected=false;int _index=0,minWidthProbe=0;TriggeredSource _lastTriggeredSource=TriggeredSource::Mouse;bool _preventClose=false;rpl::event_stream<>_clicks;base::qt_connection _connection;
''' + function(item_h, 'virtual void handleKeyPress(') + r'''
virtual not_null<QAction*>action()const=0;virtual bool isEnabled()const=0;virtual int contentHeight()const=0;bool isSelected()const{return selected;}int index()const{return _index;}void fitToMenuWidth(){}void enableMouseSelecting(){}void setMinWidth(int w){minWidthProbe=w;}void setClicked(TriggeredSource);rpl::producer<CallbackData>clicks()const;void setActionTriggered(Fn<void()>);
};
''' + '\n'.join(function(item, signature) for signature in (
        'void ItemBase::setClicked(', 'rpl::producer<CallbackData> ItemBase::clicks()',
        'void ItemBase::setActionTriggered(')) + '\n' + '\n'.join(
        function(menu, signature) for signature in (
            'ItemBase *Menu::findSelectedAction() const', 'void Menu::handleKeyPress(')) + r'''
Menu::~Menu(){_actionWidgets.clear();for(auto a:_actions)delete a.get();}
void Menu::setSelected(int i,bool){for(auto&w:_actionWidgets)w->selected=(w->_index==i);}
}
}
// Preserve the real owning namespace: no global alias for Menu::TriggeredSource.
namespace Settings{namespace{
''' + classes + methods + r'''
}}
int main(){
 using namespace Ui::Menu;
 for(const auto label:{QString("Global settings"),QString("Общие настройки")}){
  Menu menu;style::Menu st;UserData user{"Account & <name>"};int globalCalls=0,accountCalls=0;
  auto global=std::make_unique<Settings::GlobalAction>(&menu,st,label,true,[&]{++globalCalls;});auto g=global.get();
  auto account=std::make_unique<Settings::AccountAction>(&menu,st,&user,false,[&]{++accountCalls;});auto a=account.get();
  menu._actions={g->action(),a->action()};a->_index=1;menu._actionWidgets.push_back(std::move(global));menu._actionWidgets.push_back(std::move(account));
  for(auto&w:menu._actionWidgets){w->clicks()|rpl::on_next([](CallbackData data){data.action->trigger();},w->lifetime());assert(w->accessibilityRole()==QAccessible::MenuItem&&w->acceptBoth);}
  assert(g->accessibilityName()==label&&g->action()->text()==label);
  assert(a->accessibilityName()==user.name()&&a->action()->text()==user.name());
  for(int key:{Qt::Key_Enter,Qt::Key_Return,Qt::Key_Space}){
   QKeyEvent event{key};menu.setSelected(0,false);auto n=globalCalls;menu.handleKeyPress(&event);assert(globalCalls==n+1&&g->_lastTriggeredSource==TriggeredSource::Keyboard);
   n=accountCalls;menu.setSelected(1,false);menu.handleKeyPress(&event);assert(accountCalls==n+1&&a->_lastTriggeredSource==TriggeredSource::Keyboard);
   n=globalCalls;g->handleKeyPress(&event);assert(globalCalls==n);
  }
  auto total=globalCalls+accountCalls;QKeyEvent irrelevant{12345};menu.handleKeyPress(&irrelevant);assert(total==globalCalls+accountCalls);
  menu.setSelected(-1,false);QKeyEvent enter{Qt::Key_Return};menu.handleKeyPress(&enter);assert(total==globalCalls+accountCalls);
  menu.setSelected(0,false);QKeyEvent down{Qt::Key_Down};menu.handleKeyPress(&down);assert(a->isSelected()&&!g->isSelected());QKeyEvent up{Qt::Key_Up};menu.handleKeyPress(&up);assert(g->isSelected()&&!a->isSelected());
  g->disabled=true;menu.handleKeyPress(&enter);g->accessibilityDoAction("press");assert(total==globalCalls+accountCalls);g->disabled=false;
  g->accessibilityDoAction("press");assert(globalCalls+accountCalls==++total);a->accessibilityDoAction("press");assert(globalCalls+accountCalls==++total);
  g->clicked(Qt::NoModifier,Qt::LeftButton);assert(globalCalls+accountCalls==++total);g->accessibilityDoAction("unrelated");assert(globalCalls+accountCalls==total);
 }
 std::cout<<"Ghost picker extracted production PASS: both constructors/labels/MenuItem, Enter/Return/Space, selected/unselected/no-selection, arrows, irrelevant keys, disabled, inherited AT press and mouse callback parity; real rpl + Qt/style/selection doubles, NOT native Qt/UIA/settings runtime\n";
}
''', extra=('-I'+str(ROOT/'Telegram/lib_rpl'),
            '-I'+str(ROOT/'Telegram/lib_base'),
            '-I'+str(ROOT/'Telegram/ThirdParty/GSL/include')))


if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='luxury-ghost-picker-') as directory:
        check(Path(directory))
