"""Photo editor menu behavior through actual local classes and menu dispatch.
Extracts production declarations, constructors, setters and owning callbacks;
uses real bundled rpl and explicit Qt/widget/style/action/view doubles. This is
not a native Qt translation-unit, rendering, UIA or account test.
"""
from pathlib import Path
import re
import tempfile
from library_regressions import cpp, function, source, ROOT


def check(root):
    text = source('Telegram/SourceFiles/editor/photo_editor_controls.cpp')
    item_h = source('Telegram/lib_ui/ui/widgets/menu/menu_item_base.h')
    action_h = source('Telegram/lib_ui/ui/widgets/menu/menu_action.h')
    item = source('Telegram/lib_ui/ui/widgets/menu/menu_item_base.cpp')
    action = source('Telegram/lib_ui/ui/widgets/menu/menu_action.cpp')
    menu = source('Telegram/lib_ui/ui/widgets/menu/menu.cpp')
    button_h = source('Telegram/lib_ui/ui/abstract_button.h')
    button = source('Telegram/lib_ui/ui/abstract_button.cpp')
    rp_h = source('Telegram/lib_ui/ui/rp_widget.h')
    rp = source('Telegram/lib_ui/ui/rp_widget.cpp')
    bridge = source('Telegram/lib_ui/ui/accessible/ui_accessible_widget.cpp')
    classes = '\n'.join(function(text, 'class ' + n + ' final') + ';'
                        for n in ('CheckAction', 'RoundCheckAction'))
    methods = ''
    for name in ('CheckAction', 'RoundCheckAction'):
        for match in re.finditer(r'^[^\n]*\b' + name + r'::\w+\(', text, re.M):
            if 'paintEvent' not in match.group():
                if re.search(r'::' + name + r'\(', match.group()):
                    end = match.start() + re.search(r'^}', text[match.start():], re.M).end()
                    methods += text[match.start():end] + '\n'
                else:
                    methods += function(text, match.group()) + '\n'
        methods += 'void ' + name + '::paintEvent(QPaintEvent*){}\n'
    # Actual owner bodies, with only the lambda capture adapted to this fixture.
    keep = function(text, 'keepRatio->setActionTriggered([=]')
    filled = function(text, 'filled->setActionTriggered([=]')
    keep = keep[keep.index('{'):]
    filled = filled[filled.index('{'):]
    actual_base = '\n'.join(function(item, s) for s in (
        'void ItemBase::setClicked(', 'rpl::producer<CallbackData> ItemBase::clicks()',
        'void ItemBase::setPreventClose(', 'bool ItemBase::preventClose() const'))
    actual_action = function(action, 'void Action::handleKeyPress(')
    ctor_start = action.index('Action::Action(')
    ctor_end = ctor_start + re.search(r'^}', action[ctor_start:], re.M).end()
    action_ctor = action[ctor_start:ctor_end]
    virtuals = '\n'.join(re.search(r'(?:\[\[nodiscard\]\] )?virtual [^;\n]+\b' + n + r'\([^;]*;', rp_h).group()
                         for n in ('accessibilityRole', 'accessibilityName', 'accessibilityState',
                                   'accessibilityActionNames', 'accessibilityDoAction'))
    defaults = '\n'.join(function(rp, re.search(r'^[^\n]*RpWidget::' + n + r'\(', rp, re.M).group())
                         for n in ('accessibilityRole', 'accessibilityName', 'accessibilityState',
                                   'accessibilityActionNames', 'accessibilityDoAction'))
    cpp(root, 'editor_menu', r'''
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
#include <rpl/variable.h>
#include <rpl/merge.h>
#include <rpl/map.h>
#include <rpl/filter.h>
#include <rpl/range.h>
namespace base::assertion{void log(const char*,const char*,int){std::abort();}}
#undef Expects
#undef Assert
#define Expects(x) assert(x)
#define Assert(x) assert(x)
using QString=std::string;using QStringList=std::vector<QString>;template<class T>using Fn=std::function<T>;
template<class T>struct not_null{T p;not_null(T v):p(v){assert(v);}T get()const{return p;}T operator->()const{return p;}operator T()const{return p;}};
namespace Qt{enum{NoModifier=0,LeftButton=1,WA_TransparentForMouseEvents=2,OtherFocusReason=3};enum FocusPolicy{NoFocus,ClickFocus};enum{Key_Up=10,Key_Down,Key_Enter,Key_Return,Key_Space,Key_Escape};}
struct QKeyEvent{int k;int key()const{return k;}};struct QPaintEvent{};
namespace QAccessible{enum Role{NoRole,Client,Button,MenuItem,PageTab,ListItem,ButtonMenu};struct State{};}
namespace ranges{using std::ranges::find_if;}
struct QAccessibleActionInterface{static QString pressAction(){return "press";}static QString toggleAction(){return "toggle";}static QString setFocusAction(){return "focus";}};
struct QWidget{std::shared_ptr<bool>alive=std::make_shared<bool>(true);QString name;bool enabled=true,focused=false;int w=300,h=30;virtual~QWidget(){*alive=false;}QString accessibleName(){return name;}void setAccessibleName(QString s){name=s;}void update(){}void resize(int a,int b){w=a;h=b;}int width()const{return w;}int height()const{return h;}int y()const{return 0;}void setAttribute(int){}void setFocus(int=0){focused=true;}};
namespace crl{struct Task{std::weak_ptr<bool>alive;Fn<void()>f;};std::vector<Task>tasks;template<class T,class F>void on_main(T*p,F f){tasks.push_back({p->alive,f});}void drain(){auto pending=std::exchange(tasks,{});for(auto&t:pending){auto a=t.alive.lock();if(a&&*a)t.f();}}}
struct QAction{QString label;bool enabled=true;Fn<void()> callback;explicit QAction(QWidget*){}QAction(QString s,QWidget*):label(s){}QString text()const{return label;}bool isEnabled()const{return enabled;}void trigger(){if(enabled&&callback){auto f=callback;f();}}void changed(){}};
template<class...T>void connect(T...){}
namespace anim{enum class type{normal,instant};constexpr int linear=0;}
namespace style{struct Padding{int left()const{return 30;}int top()const{return 4;}int bottom()const{return 4;}};struct Font{int height=18;};struct ItemStyle{Font data;Font*font=&data;};struct Menu{int widthMin=180;Padding itemPadding;ItemStyle itemStyle;};struct icon{};}
namespace st{constexpr int photoEditorMenuCheck=0,photoEditorMenuCheckLabel=0;struct RoundCheck{int duration=100;}photoEditorMenuRoundCheck;}
namespace rect::m::sum{int h(style::Padding){return 60;}int v(style::Padding){return 8;}}
namespace base{template<class T>using unique_qptr=std::unique_ptr<T>;template<class T,class...A>auto make_unique_q(A&&...a){return std::make_unique<T>(std::forward<A>(a)...);}}
namespace Ui{
''' + function(rp_h, 'struct AccessibilityState') + r''';
class RpWidget:public QWidget{public:rpl::lifetime life;int notifications=0;Fn<void()> onState;
''' + virtuals + r'''
virtual void paintEvent(QPaintEvent*){}bool accessibilitySelectionList()const{return false;}auto&lifetime(){return life;}void accessibilityStateChanged(AccessibilityState s){assert(s.checked);++notifications;if(onState){auto f=onState;f();}}};
''' + defaults + r'''
struct SelectionObject{QWidget*object(){return nullptr;}};template<class T>T qobject_cast(QWidget*p){return static_cast<T>(p);}
QStringList operator+(QStringList a,QStringList b){a.insert(a.end(),b.begin(),b.end());return a;}
struct QAccessibleWidget{int qtActions=0;QStringList actionNames()const{return{};}void doAction(const QString&){++qtActions;}};
namespace base{struct Integration{static Integration&Instance(){static Integration i;return i;}template<class F>void enterFromEventLoop(F f){f();}};}
namespace Accessible{
class Widget:public QAccessibleWidget{public:RpWidget*p;explicit Widget(RpWidget*w):p(w){}RpWidget*rp()const{return p;}SelectionObject*selectedItem(int){return nullptr;}QStringList actionNames()const;void doAction(const QString&);};
''' + function(bridge, 'QStringList Widget::actionNames() const') + '\n' + function(bridge, 'void Widget::doAction(') + r'''
}
class AbstractButton:public RpWidget{public:bool down=false,disabled=false,acceptBoth=false,_pageTab=false,_listItem=false,_menuButton=false;rpl::event_stream<> nativeClicks;
''' + function(button_h, 'QAccessible::Role accessibilityRole() override') + r'''
AccessibilityState accessibilityState()const override;void accessibilityDoAction(const QString&)override;
bool isDown()const{return down;}bool isDisabled()const{return disabled;}void setDisabled(bool v){disabled=v;}void setAcceptBoth(bool v){acceptBoth=v;}void clicked(int,int){nativeClicks.fire({});}auto clicks()const{return nativeClicks.events();}};
''' + function(button, 'AccessibilityState AbstractButton::accessibilityState() const') + '\n' + function(button, 'void AbstractButton::accessibilityDoAction(') + r'''
class RippleButton:public AbstractButton{public:};
struct CheckView{bool value;CheckView(int,bool v,Fn<void()>):value(v){}bool checked()const{return value;}void setChecked(bool v,anim::type){value=v;}struct Size{int height()const{return 18;}};Size getSize()const{return{};}};
struct RoundCheckbox{bool value=false;RoundCheckbox(st::RoundCheck,Fn<void()> ){}bool checked()const{return value;}void setChecked(bool v,anim::type){value=v;}};
namespace Animations{struct Simple{template<class F>void start(F f,double,double,int,int){f();}};}
struct FlatLabel:RpWidget{template<class P>FlatLabel(QWidget*,P,int){}void resizeToWidth(int){}void moveToLeft(int,int){}int heightNoMargins()const{return 20;}};
template<class T,class P>not_null<T*> CreateChild(P*){return new T(static_cast<QWidget*>(nullptr));}
namespace Menu{enum class TriggeredSource{Mouse,Keyboard};struct CallbackData{not_null<QAction*>action;int top;TriggeredSource source;int index;bool selected,preventClose;};class ItemBase;
class Menu:public RpWidget{public:std::vector<not_null<QAction*>>_actions;std::vector<std::unique_ptr<ItemBase>>_actionWidgets;auto widthValue()const{return rpl::single(300);}ItemBase*findSelectedAction()const;void handleKeyPress(not_null<QKeyEvent*>);void setSelected(int,bool);};
class ItemBase:public RippleButton{public:ItemBase(not_null<Menu*>,const style::Menu&){}bool selected=false;int _index=0;TriggeredSource _lastTriggeredSource=TriggeredSource::Mouse;bool _preventClose=false;rpl::event_stream<>_clicks;
''' + function(item_h, 'virtual void handleKeyPress(') + r'''
virtual not_null<QAction*>action()const=0;virtual bool isEnabled()const=0;virtual int contentHeight()const=0;
void setClicked(TriggeredSource source=TriggeredSource::Mouse);rpl::producer<CallbackData>clicks()const;void setPreventClose(bool);bool preventClose()const;
bool isSelected()const{return selected;}void setSelected(bool s){selected=s;}int index()const{return _index;}void enableMouseSelecting(){}void setMinWidth(int w){_minWidthProbe=w;}int _minWidthProbe=0;void setActionTriggered(Fn<void()>f){action()->callback=std::move(f);}};
''' + actual_base + r'''
class Action:public ItemBase{public:not_null<QAction*>_action;const style::Menu&_st;const style::icon*_icon;const style::icon*_iconOver;const int _height;int initialHeight=0;
Action(not_null<Menu*>,const style::Menu&,not_null<QAction*>,const style::icon*,const style::icon*);
void fitToMenuWidth(){initialHeight=contentHeight();resize(300,initialHeight);}void processAction(){}
''' + function(action_h, 'QAccessible::Role accessibilityRole() override') + '\n' + function(action_h, 'QString accessibilityName() override') + r'''
bool isEnabled()const override;not_null<QAction*>action()const override;
void handleKeyPress(not_null<QKeyEvent*>)override;int contentHeight()const override;};
''' + action_ctor + '\n' + function(action, 'int Action::contentHeight() const') + '\n' + function(action, 'bool Action::isEnabled() const') + '\n' + function(action, 'not_null<QAction*> Action::action() const') + '\n' + actual_action + '\n' + function(menu, 'ItemBase *Menu::findSelectedAction() const') + '\n' + function(menu, 'void Menu::handleKeyPress(') + r'''
void Menu::setSelected(int i,bool){for(auto&w:_actionWidgets)w->selected=(w->_index==i);}
}
}
namespace Editor{
''' + classes + '\n' + methods + r'''
struct Owner{bool _keepOriginalRatio=true,_shapesFilled=false;double _originalRatio=1.5;struct Events{int count=0;Fn<void()>callback;void fire_copy(double){++count;if(callback){auto f=callback;f();}}}_aspectRatioChanges,_shapesFillChanges;struct Shape{bool filled=false;void setFilled(bool v){filled=v;}};std::vector<Shape*>storage;auto entries(){return &storage;}
void wire(CheckAction*keepRatio){auto raw=keepRatio;keepRatio->setActionTriggered([=]''' + keep + r''');}
void wire(RoundCheckAction*filled){auto filledRaw=filled;auto entries=&storage;filled->setActionTriggered([=]''' + filled + r''');}
};
}
int main(){
 using namespace Ui::Menu;Menu menu;style::Menu st;Editor::Owner owner;
 auto keep=std::make_unique<Editor::CheckAction>(&menu,st,"Keep original ratio",true);auto k=keep.get();owner.wire(k);assert(k->initialHeight==26&&k->height()==28&&k->_minWidthProbe==st.widthMin&&!k->acceptBoth);
 menu._actions.push_back(k->action());menu._actionWidgets.push_back(std::move(keep));
 auto filled=std::make_unique<Editor::RoundCheckAction>(&menu,st,new QAction("Filled shapes",&menu),false);auto f=filled.get();owner.wire(f);f->_index=1;f->setPreventClose(true);
 menu._actions.push_back(f->action());menu._actionWidgets.push_back(std::move(filled));
 rpl::lifetime wiring;for(auto&w:menu._actionWidgets){w->clicks()|rpl::on_next([&](CallbackData d){assert(d.preventClose);d.action->trigger();},wiring);}
 menu.setSelected(0,false);QKeyEvent enter{Qt::Key_Enter};menu.handleKeyPress(&enter);
 assert(!owner._keepOriginalRatio&&owner._aspectRatioChanges.count==1);
 assert(k->accessibilityState().checkable&&!k->accessibilityState().checked);
 assert(f->accessibilityState().checkable&&!f->accessibilityState().checked);
 assert(k->notifications==0);crl::drain();assert(k->notifications==1);
 k->setChecked(false);crl::drain();assert(k->notifications==1);
 f->setChecked(true);assert(f->notifications==0);crl::drain();assert(f->notifications==1&&f->accessibilityState().checked);
 auto names=k->accessibilityActionNames();assert(std::ranges::find(names,"toggle")!=names.end());
 Ui::Accessible::Widget bridge(k);auto bridgeNames=bridge.actionNames();assert(std::ranges::find(bridgeNames,"toggle")!=bridgeNames.end());const auto bridgeCount=owner._aspectRatioChanges.count;bridge.doAction("toggle");assert(owner._aspectRatioChanges.count==bridgeCount+1&&bridge.qtActions==1);bridge.doAction("press");assert(owner._aspectRatioChanges.count==bridgeCount+2&&bridge.qtActions==2);
 k->accessibilityDoAction("toggle");assert(owner._keepOriginalRatio&&owner._aspectRatioChanges.count==4);crl::drain();
 assert(k->accessibilityRole()==QAccessible::MenuItem&&k->accessibilityName()=="Keep original ratio");
 assert(f->accessibilityRole()==QAccessible::MenuItem&&f->accessibilityName()=="Filled shapes");
 for(int key:{Qt::Key_Enter,Qt::Key_Return,Qt::Key_Space}){
  menu.setSelected(0,false);const auto old=owner._keepOriginalRatio;const auto count=owner._aspectRatioChanges.count;QKeyEvent e{key};menu.handleKeyPress(&e);assert(owner._keepOriginalRatio!=old&&owner._aspectRatioChanges.count==count+1);assert(k->_lastTriggeredSource==TriggeredSource::Keyboard);crl::drain();
 }
 const auto count=owner._aspectRatioChanges.count;QKeyEvent irrelevant{12345};menu.handleKeyPress(&irrelevant);assert(owner._aspectRatioChanges.count==count);
 k->setDisabled(true);k->accessibilityDoAction("toggle");QKeyEvent space{Qt::Key_Space};menu.handleKeyPress(&space);assert(owner._aspectRatioChanges.count==count);k->setDisabled(false);
 k->action()->enabled=false;k->accessibilityDoAction("toggle");menu.handleKeyPress(&space);assert(owner._aspectRatioChanges.count==count);k->action()->enabled=true;
 menu.setSelected(1,false);k->handleKeyPress(&space);assert(owner._aspectRatioChanges.count==count);
 Editor::Owner::Shape shape;owner.storage.push_back(&shape);const auto fills=owner._shapesFillChanges.count;menu.handleKeyPress(&space);assert(owner._shapesFilled&&f->accessibilityState().checked&&shape.filled&&owner._shapesFillChanges.count==fills+1);crl::drain();
 f->accessibilityDoAction("toggle");assert(!owner._shapesFilled&&!shape.filled&&!f->accessibilityState().checked);crl::drain();
 owner._shapesFillChanges.callback=[&]{assert(f->accessibilityState().checked==owner._shapesFilled&&shape.filled==owner._shapesFilled);};f->accessibilityDoAction("toggle");crl::drain();owner._shapesFillChanges.callback={};
 bool nested=false;owner._shapesFillChanges.callback=[&]{if(!std::exchange(nested,true))f->accessibilityDoAction("toggle");};f->accessibilityDoAction("toggle");crl::drain();assert(owner._shapesFilled&&shape.filled&&f->accessibilityState().checked);owner._shapesFillChanges.callback={};
 const auto old=owner._keepOriginalRatio;k->accessibilityDoAction("press");assert(owner._keepOriginalRatio!=old);crl::drain();
 menu.setSelected(0,false);QKeyEvent down{Qt::Key_Down};menu.handleKeyPress(&down);assert(f->isSelected());QKeyEvent up{Qt::Key_Up};menu.handleKeyPress(&up);assert(k->isSelected());
 k->down=true;f->down=true;assert(k->accessibilityState().pressed&&f->accessibilityState().pressed);k->down=false;f->down=false;
 auto retiring=std::make_unique<Editor::RoundCheckAction>(&menu,st,new QAction("Retiring",&menu),false);Editor::Owner retiringOwner;retiringOwner.wire(retiring.get());retiringOwner._shapesFillChanges.callback=[&]{retiring.reset();};auto a=retiring->action();a->trigger();assert(!retiring);crl::drain();
 std::unique_ptr<Editor::CheckAction> ephemeral=std::make_unique<Editor::CheckAction>(&menu,st,"Disposed",false);ephemeral->setChecked(true);ephemeral.reset();crl::drain();
 std::unique_ptr<Editor::RoundCheckAction> self=std::make_unique<Editor::RoundCheckAction>(&menu,st,new QAction("Self",&menu),false);self->onState=[&]{self.reset();};self->setChecked(true);crl::drain();assert(!self);
 std::cout<<"Editor actual menus PASS: Enter/Return/Space/arrows, named checkable MenuItems, press/toggle owner parity, disabled/unselected guards, committed shape state/reentry, queued changed-only notification/deletion; realrpl with Qt/style/view/queue doubles, NOT nativeQt/UIA\n";
}
''', extra=('-I'+str(ROOT/'Telegram/lib_rpl'), '-I'+str(ROOT/'Telegram/lib_base'), '-I'+str(ROOT/'Telegram/ThirdParty/GSL/include')))


if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='luxury-editor-menu-') as directory:
        check(Path(directory))
