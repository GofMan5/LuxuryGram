"""Actual TabsStrip declaration/logic and virtual-child actions, explicit Qt doubles.

No native paint/UIA/window/profile. Real bundled rpl event stream for destructive
listeners; production setTabs, selection, keys and stable-identity actions compiled.
"""
from pathlib import Path
from library_regressions import cpp, function, source


def check(root):
    text = source('Telegram/SourceFiles/info/profile/tabs/info_profile_tabs_strip.cpp')
    header = source('Telegram/SourceFiles/info/profile/tabs/info_profile_tabs_strip.h')
    klass = function(header, 'class TabsStrip final') + ';'
    base = source('Telegram/lib_ui/ui/rp_widget.h')
    overrides = ('accessibilityRole', 'accessibilityChildCount', 'accessibilityChildName',
                 'accessibilityChildRole', 'accessibilityChildState', 'accessibilityChildRect',
                 'accessibilityChildSupportsActions', 'accessibilityChildIdentity',
                 'accessibilityChildIndexByIdentity', 'accessibilityChildSetFocus',
                 'accessibilityChildActivate', 'accessibilityOrientation', 'accessibilitySelectionList')
    import re
    declarations = []
    for name in overrides:
        declaration = re.search(r'(?:\[\[nodiscard\]\] )?virtual [^;\n]+\b' + name + r'\([^;]*;', base).group()
        declarations.append(declaration)
    methods = [function(text, name) for name in (
        'TabsStrip::TabsStrip(', 'void TabsStrip::setTabs(', 'void TabsStrip::setActiveTab(',
        'void TabsStrip::setSelected(', 'void TabsStrip::setActive(', 'void TabsStrip::scrollToTab(',
        'QRect TabsStrip::highlightRect(', 'QPoint TabsStrip::contentOrigin(', 'int TabsStrip::resizeGetHeight(')]
    optional = ('void TabsStrip::setFocused(', 'void TabsStrip::activateTab(', 'void TabsStrip::notifyActiveChanged(',
                'void TabsStrip::keyPressEvent(', 'void TabsStrip::focusInEvent(', 'void TabsStrip::focusOutEvent(',
                'void TabsStrip::mouseReleaseEvent(', 'void TabsStrip::stopPressedRipple(')
    methods += [function(text, name) for name in optional if name in text]
    for name in overrides:
        match = re.search(r'[^\n]*TabsStrip::' + name + r'\(', text)
        if match:
            methods.append(function(text, match.group()))
    # Before implementation, Qt QWidget virtual methods fall through to the base
    # double. Missing children retain actual RpWidget defaults, not a fake model.
    core = '\n'.join(methods)
    item_source = source('Telegram/lib_ui/ui/accessible/ui_accessible_item.cpp')
    item_action = '\n'.join(function(item_source, sig) for sig in ('int Item::currentIndex() const', 'QAccessible::Role Item::role() const', 'QAccessible::State Item::state() const', 'void Item::doAction('))
    widget_source = source('Telegram/lib_ui/ui/accessible/ui_accessible_widget.cpp')
    select_action = function(widget_source, '[[nodiscard]] bool IsSelectionItemRole(') + '\n' + function(widget_source, 'bool Widget::select(')
    repo = Path(__file__).resolve().parents[2]
    cpp(root, 'profile_tabs', r'''
#include <algorithm>
#include <array>
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
template<class...T>constexpr bool false_t(T&&...){return false;}
#include <rpl/event_stream.h>
#include <rpl/range.h>
namespace base::assertion{void log(const char*,const char*,int){std::abort();}}
using QString=std::string;QString operator""_q(const char16_t*s,size_t n){QString r;for(size_t i=0;i<n;++i)r+=char(s[i]);return r;}
#define isEmpty empty
#undef Expects
#undef Assert
#define Expects(x) assert(x)
#define Assert(x) assert(x)
using quintptr=uintptr_t;using float64=double;
namespace Qt{enum MouseButton{LeftButton,RightButton};enum ScrollPhase{NoScrollPhase};enum Orientation{Horizontal,Vertical};enum FocusPolicy{NoFocus,StrongFocus};enum FocusReason{MouseFocusReason,TabFocusReason,BacktabFocusReason,OtherFocusReason};enum{Key_Left=1,Key_Right,Key_Home,Key_End,Key_Return,Key_Enter,Key_Space,Key_Tab};enum{NoModifier=0,ShiftModifier=1,ControlModifier=2,AltModifier=4,MetaModifier=8};using KeyboardModifiers=int;}
struct QPoint{int a=0,b=0;QPoint(int x=0,int y=0):a(x),b(y){}int x()const{return a;}int y()const{return b;}friend QPoint operator+(QPoint p,QPoint q){return {p.a+q.a,p.b+q.b};}};
struct QMargins{int a=0,b=0,c=0,d=0;QMargins(int l=0,int t=0,int r=0,int b=0):a(l),b(t),c(r),d(b){}int left()const{return a;}int top()const{return b;}int right()const{return c;}int bottom()const{return d;}};QMargins Margins(int n){return {n,n,n,n};}
struct QRect{int a=0,b=0,c=0,d=0;QRect(int x=0,int y=0,int w=0,int h=0):a(x),b(y),c(w),d(h){}int x()const{return a;}int y()const{return b;}int width()const{return c;}int height()const{return d;}bool isEmpty()const{return c<=0||d<=0;}QRect translated(QPoint p)const{return {a+p.x(),b+p.y(),c,d};}QRect intersected(QRect q)const{int x=std::max(a,q.a),y=std::max(b,q.b);return {x,y,std::max(0,std::min(a+c,q.a+q.c)-x),std::max(0,std::min(b+d,q.b+q.d)-y)};}friend QRect operator-(QRect r,QMargins m){return {r.a+m.a,r.b+m.b,r.c-m.a-m.c,r.d-m.b-m.d};}};struct QRectF{QRect toRect()const{return {};}};struct QImage{};struct QCursor{static QPoint pos(){return {};}};
struct QEvent{virtual ~QEvent()=default;};struct QFocusEvent:QEvent{Qt::FocusReason value;explicit QFocusEvent(Qt::FocusReason r):value(r){}auto reason()const{return value;}};
struct QKeyEvent:QEvent{int value,mods=0;bool repeat=false,accepted=false;QKeyEvent(int k,int m=0,bool r=false):value(k),mods(m),repeat(r){}int key()const{return value;}int modifiers()const{return mods;}bool isAutoRepeat()const{return repeat;}void accept(){accepted=true;}};struct QWheelEvent{};struct QMouseEvent{Qt::MouseButton value=Qt::LeftButton;Qt::MouseButton button()const{return value;}};struct QContextMenuEvent{};struct QPaintEvent{};
struct QAccessibleEvent;
namespace QAccessible{enum Role{NoRole,PageTabList,PageTab,ListItem};enum Event{ObjectReorder};struct State{bool disabled=false,focusable=false,focused=false,selectable=false,selected=false,invisible=false,offscreen=false;};void updateAccessibility(struct ::QAccessibleEvent*);}
struct QAccessibleEvent;
struct QAccessibleActionInterface{static QString setFocusAction(){return "focus";}static QString toggleAction(){return "toggle";}static QString pressAction(){return "press";}static QString showMenuAction(){return "menu";}virtual void doAction(const QString&)=0;};
struct QAccessibleInterface{virtual QAccessible::Role role()const=0;virtual QAccessible::State state()const=0;virtual QAccessibleActionInterface *actionInterface()=0;};
struct TextWithEntities{QString text;};namespace style{struct ProfileTabsStrip{int height=48,skip=0,activeSkip=4,style=0,duration=200;QMargins margin{8,6,8,6},tabPadding{16,0,16,0};};inline int cur_pointer=1,cur_default=0;inline rpl::event_stream<> palette;auto PaletteChanged(){return palette.events();}}
namespace st{int infoProfileTabsShadow=0;}namespace anim{int easeOutQuint=0;}
namespace Ui{class RippleAnimation{public:void lastStop(){} };struct BoxShadow{explicit BoxShadow(int){}};namespace Text{struct MarkedContext{std::function<void()> repaint;};struct String{QString value;void setMarkedText(int,const TextWithEntities&t,int,const MarkedContext&){value=t.text;}int maxWidth()const{return int(value.size())*8;}};}namespace Animations{struct Simple{template<class...T>void start(T...){ }void stop(){} };}}
namespace crl{using time=long long;struct Task{std::weak_ptr<bool> alive;std::function<void()> callback;};std::vector<Task> tasks;template<class T,class F>void on_main(T *owner,F f){tasks.push_back({owner->alive,std::move(f)});}void drain(){auto list=std::exchange(tasks,{});for(auto &t:list)if(auto p=t.alive.lock();p&&*p)t.callback();}}
struct QWidget{virtual ~QWidget()=default;};
namespace Ui{struct RpWidget:QWidget{std::shared_ptr<bool> alive=std::make_shared<bool>(true);rpl::lifetime life;bool focused=false,enabled=true,visible=true;int w=200,updates=0,focusedEvents=0,selectionEvents=0,lastFocused=-1;Qt::FocusPolicy policy=Qt::NoFocus;std::function<void()> onSelection,onReorder,onChildFocus;explicit RpWidget(QWidget*){}~RpWidget(){*alive=false;}bool hasFocus()const{return focused;}bool isEnabled()const{return enabled;}bool isVisible()const{return visible;}int width()const{return w;}QRect rect()const{return {0,0,w,48};}void setObjectName(QString){}void setMouseTracking(bool){}void setCursor(int){}void setNaturalWidth(int){}void setFocusPolicy(Qt::FocusPolicy p){policy=p;}void update(){++updates;}void resizeToWidth(int n){w=n;resizeGetHeight(n);}rpl::lifetime &lifetime(){return life;}void setFocus(Qt::FocusReason r=Qt::OtherFocusReason){focused=true;QFocusEvent e(r);focusInEvent(&e);}void accessibilityChildFocused(int n){++focusedEvents;lastFocused=n;if(onChildFocus)onChildFocus();}void accessibilityChildSelectionChanged(int){++selectionEvents;if(onSelection)onSelection();}
''' + '\n'.join(declarations) + r'''
virtual QAccessible::Role accessibilityChildRoleAt(int)const{return accessibilityChildRole();}virtual void accessibilityChildShowMenu(quintptr){}virtual int resizeGetHeight(int){return 48;}virtual void wheelEvent(QWheelEvent*){}virtual void mouseMoveEvent(QMouseEvent*){}virtual void mousePressEvent(QMouseEvent*){}virtual void mouseReleaseEvent(QMouseEvent*){}virtual void contextMenuEvent(QContextMenuEvent*){}virtual void paintEvent(QPaintEvent*){}virtual bool eventHook(QEvent*){return false;}virtual void keyPressEvent(QKeyEvent*){}virtual void focusInEvent(QFocusEvent*){}virtual void focusOutEvent(QFocusEvent*){}
};}
QAccessible::Role Ui::RpWidget::accessibilityRole(){return QAccessible::NoRole;}int Ui::RpWidget::accessibilityChildCount()const{return -1;}QString Ui::RpWidget::accessibilityChildName(int)const{return {};}QAccessible::Role Ui::RpWidget::accessibilityChildRole()const{return QAccessible::NoRole;}QAccessible::State Ui::RpWidget::accessibilityChildState(int)const{return {};}QRect Ui::RpWidget::accessibilityChildRect(int)const{return {};}bool Ui::RpWidget::accessibilityChildSupportsActions(int)const{return false;}quintptr Ui::RpWidget::accessibilityChildIdentity(int)const{return 0;}int Ui::RpWidget::accessibilityChildIndexByIdentity(quintptr)const{return -1;}void Ui::RpWidget::accessibilityChildSetFocus(quintptr){}void Ui::RpWidget::accessibilityChildActivate(quintptr){}std::optional<Qt::Orientation> Ui::RpWidget::accessibilityOrientation()const{return {};}bool Ui::RpWidget::accessibilitySelectionList()const{return false;}
struct QAccessibleEvent{Ui::RpWidget *owner;QAccessibleEvent(Ui::RpWidget *p,QAccessible::Event):owner(p){}};void QAccessible::updateAccessibility(QAccessibleEvent *e){if(e->owner->onReorder)e->owner->onReorder();}
namespace base{struct Weak{std::weak_ptr<bool> alive;explicit operator bool()const{auto p=alive.lock();return p&&*p;}};template<class T>Weak make_weak(T *p){return {p->alive};}}
namespace ranges{using std::ranges::find;}
constexpr int kMarkupTextOptions=0;
namespace Info::Profile{struct StripTab{QString id;TextWithEntities text;};
#define private public
''' + klass + r'''
#undef private
''' + core + r'''
void TabsStrip::invalidate(){_contentValid=false;update();}void TabsStrip::scrollTo(float64 n){_scrollTo=_scroll=std::clamp(n,0.,double(_scrollMax));}int TabsStrip::scrollValue()const{return int(_scroll);}QRect TabsStrip::islandRect()const{return rect()-_st.margin;}int TabsStrip::islandInteriorWidth()const{return islandRect().width()-2*_st.skip;}QRectF TabsStrip::currentHighlightRect()const{return {};}
void TabsStrip::wheelEvent(QWheelEvent*){}void TabsStrip::mouseMoveEvent(QMouseEvent*){}void TabsStrip::mousePressEvent(QMouseEvent*){}void TabsStrip::contextMenuEvent(QContextMenuEvent*){}void TabsStrip::paintEvent(QPaintEvent*){}bool TabsStrip::eventHook(QEvent*){return false;}
}
using Info::Profile::TabsStrip;using Info::Profile::StripTab;
struct Owner{Ui::RpWidget *value=nullptr;std::weak_ptr<bool> alive;Owner(Ui::RpWidget *p):value(p),alive(p->alive){}Ui::RpWidget *get()const{auto p=alive.lock();return p&&*p?value:nullptr;}};struct Item:QAccessibleInterface,QAccessibleActionInterface{Owner _parent;quintptr _identity=0;mutable int _index=0;Item(Owner parent,quintptr identity):_parent(parent),_identity(identity){}int currentIndex()const;QAccessible::Role role()const override;QAccessible::State state()const override;QAccessibleActionInterface *actionInterface()override{return this;}void doAction(const QString&)override;};
struct Widget{Ui::RpWidget *owner;int indexOfChild(QAccessibleInterface *child){auto item=dynamic_cast<Item*>(child);return item&&item->_parent.get()==owner?item->currentIndex():-1;}bool select(QAccessibleInterface*);};
struct ListOwner:Ui::RpWidget{int focusCalls=0,activationCalls=0;ListOwner():RpWidget(nullptr){}QAccessible::Role accessibilityChildRole()const override{return QAccessible::ListItem;}int accessibilityChildCount()const override{return 1;}quintptr accessibilityChildIdentity(int index)const override{return index==0?1:0;}int accessibilityChildIndexByIdentity(quintptr token)const override{return token==1?0:-1;}void accessibilityChildSetFocus(quintptr)override{++focusCalls;}void accessibilityChildActivate(quintptr)override{++activationCalls;}};
''' + item_action + '\n' + select_action + r'''
std::vector<StripTab> tabs(std::initializer_list<const char*> ids){std::vector<StripTab> r;for(auto id:ids)r.push_back({id,{QString("Tab ")+id}});return r;}
void key(TabsStrip &s,int k,int m=0,bool repeat=false){QKeyEvent e(k,m,repeat);s.keyPressEvent(&e);}
int focused(TabsStrip &s){for(int i=0;i<s.accessibilityChildCount();++i)if(s.accessibilityChildState(i).focused)return i;return -1;}
int main(){style::ProfileTabsStrip st;TabsStrip s(nullptr,st);s.setTabs(tabs({"a","b","long-long-long-long-long"}));s.setActiveTab("a");assert(s.policy==Qt::StrongFocus&&s.accessibilityRole()==QAccessible::PageTabList&&s.accessibilityChildCount()==3&&s.accessibilityChildRole()==QAccessible::PageTab&&s.accessibilitySelectionList()&&s.accessibilityOrientation()==Qt::Horizontal);auto a=s.accessibilityChildIdentity(0),b=s.accessibilityChildIdentity(1),c=s.accessibilityChildIdentity(2);assert(a&&b&&c&&a!=b&&b!=c);assert(s.accessibilityChildName(1)=="Tab b"&&s.accessibilityChildIdentity(-1)==0&&s.accessibilityChildIndexByIdentity(0)==-1&&!s.accessibilityChildSupportsActions(3));s.setFocus(Qt::TabFocusReason);assert(focused(s)==0);int calls=0;QString id;s._activated.events()|rpl::on_next([&](const QString &v){++calls;id=v;s.setActiveTab(v);},s.lifetime());key(s,Qt::Key_Right);assert(focused(s)==1&&s._active==0&&!calls);key(s,Qt::Key_End);assert(focused(s)==2&&!calls&&s._scroll>0&&!s.accessibilityChildRect(2).isEmpty());key(s,Qt::Key_Left,Qt::ControlModifier);assert(focused(s)==2);key(s,Qt::Key_Return,0,true);assert(!calls);key(s,Qt::Key_Return);assert(calls==1&&id=="long-long-long-long-long"&&s._active==2);key(s,Qt::Key_Home);assert(focused(s)==0&&s._active==2);key(s,Qt::Key_Space);assert(calls==2&&id=="a");s.setTabs(tabs({"b","a","long-long-long-long-long"}));assert(s.accessibilityChildIdentity(0)==b&&s.accessibilityChildIdentity(1)==a&&focused(s)==1&&s._active==1);Item item{{&s},b};item.doAction(QAccessibleActionInterface::setFocusAction());assert(focused(s)==1&&calls==2);crl::drain();assert(focused(s)==0&&s._active==1&&calls==2);item.doAction(QAccessibleActionInterface::toggleAction());crl::drain();assert(calls==3&&id=="b"&&s._active==0);item.doAction(QAccessibleActionInterface::pressAction());s.setTabs(tabs({"a"}));crl::drain();assert(calls==3&&s.accessibilityChildIndexByIdentity(b)==-1);s.setTabs(tabs({"a","b"}));assert(s.accessibilityChildIdentity(1)!=b);s.enabled=false;auto fresh=s.accessibilityChildIdentity(1);s.accessibilityChildSetFocus(fresh);s.accessibilityChildActivate(fresh);crl::drain();key(s,Qt::Key_End);key(s,Qt::Key_Space);assert(calls==3&&s.accessibilityChildState(1).disabled);s.enabled=true;s.visible=false;s.accessibilityChildActivate(fresh);crl::drain();assert(calls==3);s.visible=true;s.setFocus(Qt::TabFocusReason);s._focused=0;ListOwner list;Item listItem{{&list},1};listItem.doAction(QAccessibleActionInterface::toggleAction());crl::drain();assert(list.focusCalls==1&&!list.activationCalls&&calls==3);s.focused=false;s._focused=0;s.accessibilityChildSetFocus(fresh);crl::drain();assert(focused(s)==1&&calls==3&&s._active==-1);s.setActiveTab("a");s.setFocus(Qt::BacktabFocusReason);assert(focused(s)==0);s.setTabs({});assert(s.policy==Qt::NoFocus&&s.accessibilityChildCount()==0);key(s,Qt::Key_End);assert(calls==3);
// Actual mouse release resolves the pressed token after synchronous focus reentry.
TabsStrip mouse(nullptr,st);mouse.setTabs(tabs({"first","second"}));mouse.setActiveTab("first");mouse.setFocus();QString mouseId;int mouseCalls=0;mouse._activated.events()|rpl::on_next([&](const QString &value){mouseId=value;++mouseCalls;},mouse.lifetime());QMouseEvent release;
bool reorderedMouse=false;mouse.onChildFocus=[&]{if(reorderedMouse)return;reorderedMouse=true;mouse.setTabs(tabs({"second","first"}));};mouse._pressed=mouse._selected=1;mouse.mouseReleaseEvent(&release);assert(mouseCalls==1&&mouseId=="second"&&mouse._pressed==-1);
bool removedMouse=false;mouse.onChildFocus=[&]{if(removedMouse)return;removedMouse=true;mouse.setTabs(tabs({"second"}));};mouse._pressed=mouse._selected=1;mouse.mouseReleaseEvent(&release);assert(mouseCalls==1);
mouse._pressed=mouse._selected=0;mouse._dragx=4;mouse.mouseReleaseEvent(&release);assert(mouseCalls==1&&mouse._dragx==0);release.value=Qt::RightButton;mouse._pressed=mouse._selected=0;mouse.mouseReleaseEvent(&release);assert(mouseCalls==1&&mouse._pressed==0);
auto mouseDying=std::make_unique<TabsStrip>(nullptr,st);mouseDying->setTabs(tabs({"first","second"}));mouseDying->setFocus();mouseDying->onChildFocus=[&]{mouseDying.reset();};mouseDying->_pressed=mouseDying->_selected=1;release.value=Qt::LeftButton;mouseDying->mouseReleaseEvent(&release);assert(!mouseDying);
// Actual cached Item index/role and Widget::select dispatch; wrapper membership doubled.
TabsStrip selection(nullptr,st);selection.setTabs(tabs({"first","second"}));auto selectionToken=selection.accessibilityChildIdentity(1);Item selectionItem{{&selection},selectionToken};assert(selectionItem.currentIndex()==1&&selectionItem.role()==QAccessible::PageTab);selection.setTabs(tabs({"second","first"}));assert(selectionItem.currentIndex()==0&&selectionItem._index==0);Widget wrapper{&selection};int selectCalls=0;selection._activated.events()|rpl::on_next([&](const QString &value){assert(value=="second");++selectCalls;selection.setActiveTab(value);},selection.lifetime());assert(wrapper.select(&selectionItem)&&!selectCalls);crl::drain();assert(selectCalls==1&&selection._active==0);selection.enabled=false;assert(!wrapper.select(&selectionItem));selection.enabled=true;assert(!wrapper.select(nullptr)&&!wrapper.select(&listItem));selection.setTabs(tabs({"first"}));assert(selectionItem.currentIndex()==-1&&!wrapper.select(&selectionItem));selection.setTabs(tabs({"first","second"}));assert(selection.accessibilityChildIdentity(1)!=selectionToken&&selectionItem.currentIndex()==-1);selectionItem.doAction(QAccessibleActionInterface::toggleAction());crl::drain();assert(selectCalls==1);selection.setFocus();selection.setTabs({});assert(selection.hasFocus()&&focused(selection)==-1);selection.setTabs(tabs({"new"}));assert(focused(selection)==0);
// Real rpl callback removes the stream owner, with id copied before listeners.
auto dying=std::make_unique<TabsStrip>(nullptr,st);dying->setTabs(tabs({"gone"}));rpl::lifetime external;dying->_activated.events()|rpl::on_next([&](const QString &v){assert(v=="gone");dying.reset();assert(v=="gone");},external);auto pointer=dying.get();pointer->accessibilityChildActivate(pointer->accessibilityChildIdentity(0));crl::drain();assert(!dying);
auto notification=std::make_unique<TabsStrip>(nullptr,st);notification->setTabs(tabs({"first","second"}));notification->setActiveTab("first");crl::drain();bool ownerSwitched=false;notification->onSelection=[&]{assert(ownerSwitched);notification.reset();};notification->setActiveTab("second");ownerSwitched=true;assert(notification);crl::drain();assert(!notification);
auto focusNotification=std::make_unique<TabsStrip>(nullptr,st);focusNotification->setTabs(tabs({"first","second"}));focusNotification->setFocus();focusNotification->onChildFocus=[&]{focusNotification.reset();};focusNotification->accessibilityChildSetFocus(focusNotification->accessibilityChildIdentity(1));crl::drain();assert(!focusNotification);
auto reorderNotification=std::make_unique<TabsStrip>(nullptr,st);reorderNotification->onReorder=[&]{reorderNotification.reset();};reorderNotification->setTabs(tabs({"first"}));assert(reorderNotification);crl::drain();assert(!reorderNotification);
auto queued=std::make_unique<TabsStrip>(nullptr,st);queued->setTabs(tabs({"queued"}));queued->accessibilityChildSetFocus(queued->accessibilityChildIdentity(0));queued.reset();crl::drain();
std::cout<<"TabsStrip actual header/base virtuals, model/key/focus/selection/identity actions + real-rpl PASS: manual navigation, live reorder/removal/re-add, disabled/hidden, queued destruction and copied activation id\n";}
''', extra=['-I', str(repo / 'Telegram/lib_rpl'), '-I', str(repo / 'Telegram/lib_base'), '-I', str(repo / 'Telegram/ThirdParty/GSL/include')])
    paint = function(text, 'void TabsStrip::paintEvent(')
    assert 'st::buttonFocusInset' in paint and '_keyboardFocus' in paint and 'st::lineWidth' in paint
    selection = function(source('Telegram/lib_ui/ui/accessible/ui_accessible_widget.cpp'), 'bool Widget::select(')
    assert 'pressAction()' in selection
    print('Actual mouse release + Item index/role/state + Widget::select dispatch PASS; wrapper membership/Qt events doubled')
    print('Tab focus paint source wiring PASS; Qt raster/RTL/Windows bridge not run')
    assert 'accessibilityFocusPolicy()' not in klass  # Ordinary focus must not opt into FocusManager.
    policy = function(source('Telegram/lib_ui/ui/rp_widget.cpp'), 'Qt::FocusPolicy RpWidget::accessibilityFocusPolicy()')
    assert 'PageTabList' not in policy
    print('Inherited PageTabList accessibility focus-manager opt-out source contract PASS; native focus mode switching unverified')


if __name__ == '__main__':
    import tempfile
    with tempfile.TemporaryDirectory(prefix='luxury-profile-tabs-') as directory:
        check(Path(directory))
