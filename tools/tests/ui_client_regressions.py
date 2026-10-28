"""Offline native-client logic contracts with extracted production C++.

Qt/UIA/network are explicit doubles; selected reactive roots use bundled real rpl.
Not a native build, rendering, or
screen-reader integration check. Debug-only compilation; no profile access.
"""
from pathlib import Path
import re
import tempfile
from library_regressions import cpp, function, source


def filtered_accessibility(root):
    text = source('Telegram/SourceFiles/dialogs/dialogs_inner_widget_accessibility.cpp')
    signature = '[[nodiscard]] bool AccessibleMessage('
    if signature in text:
        predicate = function(text, signature)
    else:
        # Before the fix, every formatter gates only service messages.
        assert 'if (item->isService())' in function(text, '[[nodiscard]] QStringList MessageTailParts(')
        predicate = 'bool AccessibleMessage(not_null<const HistoryItem*> item) { return !item->isService(); }'
    cpp(root, 'accessible_filter', r'''
#include <cassert>
#include <iostream>
template<class T>struct not_null{T p;T get()const{return p;}T operator->()const{return p;}};
struct HistoryItem{bool service=false,outgoing=false,match=false;bool isService()const{return service;}};
namespace FiltersController{bool enabled=true,shown=false;bool filtered(HistoryItem *i){return enabled&&!shown&&!i->outgoing&&i->match;}}
'''+predicate+r'''
int main(){HistoryItem i;i.match=true;assert(!AccessibleMessage({&i}));FiltersController::enabled=false;assert(AccessibleMessage({&i}));FiltersController::enabled=true;FiltersController::shown=true;assert(AccessibleMessage({&i}));FiltersController::shown=false;i.outgoing=true;assert(AccessibleMessage({&i}));i.outgoing=false;i.match=false;assert(AccessibleMessage({&i}));i.service=true;assert(!AccessibleMessage({&i}));std::cout<<"Accessible filter predicate PASS: hidden incoming blocked; disabled/show-hidden/outgoing/visible/service controls\n";}
''')
    tail = function(text, '[[nodiscard]] QStringList MessageTailParts(')
    assert 'if (!AccessibleMessage(item))' in tail
    # Real tail is shared by ordinary rows and searched-message rows.
    assert 'MessageTailParts(item, history, peer)' in function(text, 'QString RowAccessibilityName(')
    assert 'MessageTailParts(item, history, peer)' in function(text, 'QString SearchedMessageAccessibilityName(')
    sub = function(text, 'QString SubItemValue(')
    for name in ('Sender', 'Message', 'Delivery', 'Reactions', 'Time', 'Forward'):
        block = sub.split('case SubItem::'+name+':', 1)[1].split('case SubItem::', 1)[0]
        assert '!AccessibleMessage(chatItem)' in block, name
    print('Accessible filter sibling wiring PASS: tail/row/search and six message columns')


def hidden_all_reorder(root):
    model = source('Telegram/SourceFiles/data/data_chat_filters.cpp')
    move = function(model, 'void ChatFilters::moveAllToFront()')
    view = source('Telegram/SourceFiles/ui/widgets/chat_filters_tabs_strip.cpp')
    reorder = function(view, 'const auto applyReorder = [=](')
    cpp(root, 'hidden_all_reorder', r'''
#include <algorithm>
#include <cassert>
#include <iostream>
#include <ranges>
#include <vector>
using FilterId=int;
struct LuxurySettings{bool hide=false;static LuxurySettings &getInstance(){static LuxurySettings s;return s;}bool hideAllChatsFolder()const{return hide;}};
namespace Data{struct ChatFilter{int value=0;ChatFilter(int v=0):value(v){}int id()const{return value;}};}
using ChatFilter=Data::ChatFilter;
namespace ranges{using std::ranges::find;namespace views{using std::views::all;using std::views::transform;}struct ToVector{};constexpr ToVector to_vector{};template<class R>auto operator|(R &&r,ToVector){using T=std::ranges::range_value_t<R>;return std::vector<T>(r.begin(),r.end());}}
namespace base{template<class T>void reorder(std::vector<T>&v,int a,int b){auto item=v.at(a);v.erase(v.begin()+a);v.insert(v.begin()+b,item);}}
#define Assert(x) assert(x)
struct ChatFilters{std::vector<Data::ChatFilter> _list;std::vector<int> saved;const auto &list()const{return _list;}void moveAllToFront();void saveOrder(const std::vector<int> &v){saved=v;}};
'''+move+r'''
struct User{bool premium=false;bool isPremium()const{return premium;}};
struct DataOwner{ChatFilters filters;auto &chatsFilters(){return filters;}};
struct Session{User u;DataOwner d;auto user(){return &u;}auto &data(){return d;}};
struct State{bool ignoreRefresh=false;};
void apply(Session *session,State *state,int oldPos,int newPos){
'''+reorder+r''';applyReorder(oldPos,newPos);}
int main(){for(bool hide:{false,true})for(bool premium:{false,true}){LuxurySettings::getInstance().hide=hide;Session session;State state;session.u.premium=premium;auto &f=session.d.filters;f._list=hide?std::vector<Data::ChatFilter>{2,3,4}:std::vector<Data::ChatFilter>{0,2,3,4};const auto oldPos=hide?0:1;apply(&session,&state,oldPos,oldPos+1);assert((f.saved==(hide?std::vector<int>{3,2,4}:std::vector<int>{0,3,2,4})));assert(f.list().size()==(hide?3:4));assert(!state.ignoreRefresh);}LuxurySettings::getInstance().hide=false;ChatFilters f;f._list={2,3,0};f.moveAllToFront();assert(f._list[0].id()==0&&f._list[1].id()==2&&f._list.size()==3);f._list={2,3};f.moveAllToFront();assert(f._list[0].id()==0&&f._list.size()==3);LuxurySettings::getInstance().hide=true;f._list={2,3};f.moveAllToFront();assert(f._list.size()==2&&f._list[0].id()==2);std::cout<<"Hidden All production reorder PASS: premium/nonpremium x hidden/visible, save order, visible fallback and existing-All move\n";}
''')
    sidebar = source('Telegram/SourceFiles/window/window_filters_menu.cpp')
    def declarations(block):
        return block[block.index('const auto hasAll ='):block.index(';', block.index('const auto premiumFrom ='))+1]
    horizontal = view[view.index('const auto reorderAll = session->user()->isPremium();'):]
    limits = declarations(horizontal)
    pinned = horizontal[horizontal.index('if (!reorderAll && hasAll)'):horizontal.index('\n\t\t\t}', horizontal.index('if (!reorderAll && hasAll)'))]
    vertical = sidebar[sidebar.index('const auto hasAll ='):]
    vertical_limits = declarations(vertical)
    vertical_pin = vertical[vertical.index('if (!reorderAll && hasAll)'):vertical.index('\n\n', vertical.index('if (!reorderAll && hasAll)'))]
    menu = view[:view.index('void SetupChatsFiltersTabs(')] if 'void SetupChatsFiltersTabs(' in view else view
    menu_limits = declarations(menu)
    reorder_source = source('Telegram/SourceFiles/ui/widgets/chat_filters_tabs_slider_reorder.cpp')
    interval = function(reorder_source, 'bool ChatsFiltersTabsReorder::Interval::isIn(').replace('ChatsFiltersTabsReorder::', 'Reorder::')
    cpp(root, 'folder_pin_limits', r'''
#include <algorithm>
#include <cassert>
#include <iostream>
#include <ranges>
#include <vector>
#include <functional>
using FilterId=int;
namespace Data{struct ChatFilter{int value;int id()const{return value;}};struct PremiumLimits{template<class T>PremiumLimits(T*){}int dialogFiltersCurrent()const;};}
int currentLimit=10;int Data::PremiumLimits::dialogFiltersCurrent()const{return currentLimit;}
namespace ranges{template<class R,class V,class P>bool contains(const R&r,V v,P p){return std::ranges::any_of(r,[&](const auto&i){return std::invoke(p,i)==v;});}}
struct Session{Session &session(){return *this;}};
struct Reorder{struct Interval{int from,length;bool isIn(int)const;};std::vector<Interval> intervals;void addPinnedInterval(int from,int length){intervals.push_back({from,length});}bool pinned(int i)const{for(auto x:intervals)if(x.isIn(i))return true;return false;}};
'''+interval+r'''
struct Slider{int locked=-1;void setLockedFrom(int v){locked=v;}};
struct State{Reorder *reorder=nullptr;};
struct Filters{std::vector<Data::ChatFilter> values;const auto &list(){return values;}};
void run(bool premium,bool all,int count){Session sessionValue;Session *session=&sessionValue,*_session=session;currentLimit=premium?30:10;std::vector<Data::ChatFilter> list;for(int i=0;i<count;++i)list.push_back({all&&i==0?0:i+1});Reorder h,v;State stateValue{&h};auto state=&stateValue;bool reorderAll=premium;
'''+limits+r'''
'''+pinned+r'''
Slider s;auto slider=&s;slider->setLockedFrom((premiumFrom>=list.size())?0:premiumFrom);
Filters filtersValue{list};auto filters=&filtersValue;auto _reorder=&v;{
'''+vertical_limits+r'''
'''+vertical_pin+r'''}
const auto expected=(all&&count>0?1:0)+currentLimit;assert(premiumFrom==expected&&slider->locked==(expected>=count?0:expected));for(int i=0;i<count;++i){const bool pin=(!premium&&all&&i==0)||i>=expected;assert(h.pinned(i)==pin&&v.pinned(i)==pin);} {
'''+menu_limits+r'''
for(int i=0;i<count;++i)assert((i<premiumFrom)==(i<expected));}}
int main(){for(bool p:{false,true})for(bool a:{false,true})for(int n:{0,1,9,10,11,12,30,31,32})run(p,a,n);std::cout<<"Folder production pin/menu/sidebar limits PASS: empty/below/at/above10/30, premium/All combinations; first custom not pinned with hidden All\n";}
''', extra=())


def navigation_activation(root):
    text = source('Telegram/SourceFiles/settings/settings_key_navigation.cpp')
    activate = function(text, 'void KeyNavigation::activate(')
    cpp(root, 'settings_activate', r'''
#include <cassert>
#include <iostream>
#include <memory>
namespace Qt{using KeyboardModifiers=int;constexpr int LeftButton=1,Key_Space=32;}
struct QEvent{enum Type{KeyPress,KeyRelease};};
struct QKeyEvent{QEvent::Type type;int code,mods;QKeyEvent(QEvent::Type t,int c,int m):type(t),code(c),mods(m){}};
template<class T>struct not_null{T p;T get()const{return p;}T operator->()const{return p;}operator T()const{return p;}};
namespace Ui{struct AbstractButton{virtual ~AbstractButton()=default;std::shared_ptr<bool> alive=std::make_shared<bool>(true);bool held=false,disabled=false,deleted=false;bool isDisabled()const{return disabled;}int releases=0,clicks=0,lastModifiers=-1;virtual void event(QKeyEvent *e){assert(*alive);if(e->type==QEvent::KeyPress){held=!disabled;if(deleted)*alive=false;}else if(held&&!disabled){++releases;++clicks;lastModifiers=e->mods;held=false;}}void clicked(int,int){assert(*alive);++clicks;}};struct Checkbox:AbstractButton{void event(QKeyEvent *e)override{assert(*alive);if(e->type==QEvent::KeyPress){held=!disabled;if(deleted)*alive=false;}else if(held&&!disabled){++releases;lastModifiers=e->mods;held=false;}}bool checked()const{return false;}void setChecked(bool){++releases;if(deleted)*alive=false;}};struct Radiobutton:Checkbox{};}
namespace base{struct Weak{std::shared_ptr<bool> alive;explicit operator bool()const{return *alive;}};Weak make_weak(Ui::AbstractButton *b){return {b->alive};}}
struct QCoreApplication{static void sendEvent(Ui::AbstractButton *b,QKeyEvent *e){b->event(e);}};
struct KeyNavigation{void activate(not_null<Ui::AbstractButton*>,Qt::KeyboardModifiers);};
'''+activate+r'''
int main(){KeyNavigation n;Ui::Checkbox c;n.activate({&c},4);assert(c.releases==1&&c.clicks==1&&c.lastModifiers==4);Ui::AbstractButton b;b.disabled=true;n.activate({&b},0);assert(!b.clicks);Ui::Checkbox dying;dying.deleted=true;n.activate({&dying},0);assert(!*dying.alive&&dying.clicks==0);std::cout<<"Settings activation production dispatch PASS: widget key semantics, modifiers, disabled and receiver destruction\n";}
''')
    row = source('Telegram/SourceFiles/settings/detailed_settings_button.cpp')
    listing = function(text, 'auto KeyNavigation::list() const')
    assert 'dynamic_cast<DetailedSettingsButton*>(widget)' in listing
    assert 'setIsListItem(true)' not in row  # Preserve button/switch Tab focus policy.
    role = function(row, 'QAccessible::Role DetailedSettingsButton::accessibilityRole()')
    assert 'QT_ACCESSIBLE_SWITCH_ROLE' in role and 'QAccessible::Role::Button' in role
    print('Described settings row navigation/role wiring PASS (source contract, not Qt integration)')


def nested_ghost_activation(root):
    text = source('Telegram/SourceFiles/luxury/ui/settings/settings_luxury_utils.cpp')
    cls = function(text, 'class NestedCheckbox final') + ';'
    callback = function(text, 'checkbox->setActivationCallback([=](Qt::KeyboardModifiers modifiers)')
    check_source = source('Telegram/lib_ui/ui/widgets/checkbox.cpp')
    keys = '\n'.join(function(check_source, signature) for signature in (
        'bool Checkbox::isSubmitEvent(', 'void Checkbox::keyPressEvent(',
        'void Checkbox::keyReleaseEvent(', 'void Checkbox::focusOutEvent(')
    )
    cpp(root, 'nested_ghost', r'''
#include <cassert>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
template<class T>using Fn=std::function<T>;
using QString=std::string;using QStringList=std::vector<QString>;
namespace Qt{using KeyboardModifiers=int;constexpr int NoModifier=0,ShiftModifier=4,Key_Space=32,Key_Return=13,Key_Enter=10;}
struct QKeyEvent{int mods=0,code=32;bool repeat=false;int modifiers()const{return mods;}int key()const{return code;}bool isAutoRepeat()const{return repeat;}void accept(){}};struct QFocusEvent{};
template<class T>using not_null=T;
struct QAccessibleActionInterface{static QString pressAction(){return "press";}static QString toggleAction(){return "toggle";}};
namespace tr{constexpr int now=0;QString luxury_GhostModeOptionLocked(int){return "Locked; Shift+Space or Toggle Option Lock to unlock";}QString luxury_GhostModeOptionShiftDescription(int){return "Shift+Space to lock/unlock";}QString luxury_GhostModeToggleOptionLock(int){return "Toggle Option Lock";}}
namespace anim{namespace type{constexpr int normal=0;}}
namespace Ui{struct AccessibilityState{bool checkable=false,checked=false,readOnly=false;};struct RippleButton{void keyPressEvent(QKeyEvent*){}void keyReleaseEvent(QKeyEvent*){}void focusOutEvent(QFocusEvent*){}};struct Checkbox:RippleButton{bool value=false,disabled=false,alive=true,_keyboardPressed=false;int changes=0,stateChanges=0;std::function<void()> notify;virtual ~Checkbox()=default;virtual QString accessibilityDescription(){return {};}virtual AccessibilityState accessibilityState()const{return {};}virtual QStringList accessibilityActionNames(){return {};}virtual void accessibilityDoAction(const QString&){};bool isDisabled()const{return disabled;}bool checked()const{return value;}void setChecked(bool v){value=v;++changes;if(notify)notify();}void setGraphicsEffect(void*){}void accessibilityDescriptionChanged(){}void accessibilityStateChanged(AccessibilityState){++stateChanges;}void sendPress(QKeyEvent *e){keyPressEvent(e);}void sendRelease(QKeyEvent *e){keyReleaseEvent(e);}void blur(){QFocusEvent e;focusOutEvent(&e);}protected:virtual void handlePress(){}bool isSubmitEvent(not_null<QKeyEvent*>)const;virtual void keyPressEvent(QKeyEvent*);virtual void keyReleaseEvent(QKeyEvent*);void focusOutEvent(QFocusEvent*);};}
struct QGraphicsOpacityEffect{template<class T>QGraphicsOpacityEffect(T*){}void setOpacity(double){}};
namespace base{template<class T>T take(T &v){T out=v;v={};return out;}struct Weak{const bool* alive;explicit operator bool()const{return *alive;}};Weak make_weak(Ui::Checkbox *c){return {&c->alive};}}
'''+keys.replace('Checkbox::', 'Ui::Checkbox::')+cls+r'''
struct CheckView{NestedCheckbox *check;bool checked()const{return check->checked();}void setChecked(bool v,int){check->setChecked(v);}};
struct NestedEntry{Fn<bool()> getter,lockGetter;Fn<void(bool)> setter,lockSetter;};
struct State{std::vector<NestedEntry> checkboxes;struct{int events=0;void fire(std::nullptr_t){++events;}}lockChanges;};
void bind(State *cState,int idx,NestedCheckbox *checkbox){const auto &entry=cState->checkboxes[idx];CheckView view{checkbox};auto checkView=&view;
'''+callback+r''',entry.lockGetter);}
int main(){State state;NestedCheckbox a,b,c;std::vector<NestedCheckbox*> checks{&a,&b,&c};bool locks[3]={false,false,false};int saves=0;for(int i=0;i<3;++i){state.checkboxes.push_back({[&,i]{return checks[i]->checked();},[&,i]{return locks[i];},[&,i](bool v){checks[i]->setChecked(v);},[&,i](bool v){locks[i]=v;++saves;}});}for(int i=0;i<3;++i)bind(&state,i,checks[i]);a.activate(Qt::NoModifier);assert(a.checked());a.activate(Qt::ShiftModifier);assert(locks[0]&&a.accessibilityState().readOnly);const int count=a.changes;a.activate(Qt::NoModifier);a.accessibilityDoAction("press");a.accessibilityDoAction("toggle");assert(a.changes==count);a.accessibilityDoAction("Toggle Option Lock");assert(!locks[0]&&a.checked());a.accessibilityDoAction("press");assert(!a.checked());a.activate(Qt::ShiftModifier);b.activate(Qt::ShiftModifier);c.activate(Qt::ShiftModifier);assert(locks[0]&&locks[1]&&!locks[2]);assert(saves==4&&state.lockChanges.events==4);c.disabled=true;c.activate(Qt::NoModifier);c.accessibilityDoAction("Toggle Option Lock");assert(!c.checked()&&!locks[2]);c.disabled=false;QKeyEvent e;e.mods=Qt::ShiftModifier;a.sendRelease(&e);assert(locks[0]);a.sendPress(&e);a.sendRelease(&e);assert(!locks[0]);a.sendRelease(&e);assert(!locks[0]);e.mods=0;a.sendPress(&e);a.sendRelease(&e);assert(a.checked());e.mods=Qt::ShiftModifier;a.sendPress(&e);a.blur();a.sendRelease(&e);assert(!locks[0]);e.repeat=true;a.sendPress(&e);a.sendRelease(&e);assert(!locks[0]);e.repeat=false;a.disabled=true;a.sendPress(&e);a.sendRelease(&e);assert(!locks[0]);a.disabled=false;a.accessibilityDoAction("toggle");assert(!a.checked());a.activate(Qt::ShiftModifier);assert(locks[0]);
assert(a.accessibilityActionNames()[0]=="Toggle Option Lock");assert(a.accessibilityDescription().find("Locked")!=std::string::npos);std::cout<<"Nested Ghost production class+lambda PASS: mouse/keyboard dispatcher target, AT ordinary/explicit-lock, native-extracted key release, lock-all denial, focus/repeat/disabled, semantics\n";}
''')
    assert 'checkbox->activate(button->clickModifiers());' in text
    assert 'checkbox->setChecked(!checkbox->checked());' in callback
    key = function(text, 'void keyReleaseEvent(QKeyEvent *e) override')
    assert '_activationModifiers = e->modifiers();' in key
    assert 'Ui::Checkbox::keyReleaseEvent(e);' in key
    print('Nested Ghost key-release/overlay wiring PASS (source contract, native AT action discovery unverified)')


def disabled_slider(root):
    text = source('Telegram/SourceFiles/ui/widgets/continuous_sliders.cpp')
    methods = '\n'.join(function(text, s) for s in (
        'void ContinuousSlider::setDisabled(', 'void ContinuousSlider::setMoveByWheel(',
        'void ContinuousSlider::mouseMoveEvent(', 'void ContinuousSlider::mousePressEvent(',
        'void ContinuousSlider::mouseReleaseEvent(', 'void ContinuousSlider::wheelEvent(',
        'void ContinuousSlider::keyPressEvent(', 'void ContinuousSlider::updateDownValueFromPos(')
    )
    cpp(root, 'disabled_slider', r'''
#include <algorithm>
#include <cassert>
#include <functional>
#include <iostream>
#include <memory>
#include <utility>
using float64=double;template<class T>using Fn=std::function<T>;
#define Expects(x) assert(x)
namespace style{constexpr int cur_default=0,cur_pointer=1;}
namespace Qt{constexpr int Key_Right=1,Key_Up=2,Key_Left=3,Key_Down=4,Key_PageUp=5,Key_PageDown=6,Key_Home=7,Key_End=8;}
struct QPoint{int X=0,Y=0;int x()const{return X;}int y()const{return Y;}};
struct QMouseEvent{QPoint p{70,0};QPoint pos()const{return p;}void ignore(){}};
struct QWheelEvent{static constexpr int DefaultDeltasPerStep=120;QPoint delta{0,120};QPoint angleDelta()const{return delta;}void ignore(){}};
struct QKeyEvent{int code=Qt::Key_Right;int key()const{return code;}void ignore(){}};
namespace Platform{bool IsMac(){return false;}}
struct ContinuousSlider;
namespace base{struct Timer{Fn<void()> cb;bool active=false;Timer(Fn<void()> f):cb(std::move(f)){}void callOnce(int){active=true;}void cancel(){active=false;}void flush(){if(std::exchange(active,false))cb();}};struct Weak{const bool *alive;explicit operator bool()const{return *alive;}};Weak make_weak(ContinuousSlider*);}
constexpr int kByWheelFinishedTimeout=1000;
struct ContinuousSlider{bool _disabled=false,_mouseDown=false,enabled=true,alive=true;double _value=.5,_downValue=0.;std::unique_ptr<base::Timer> _byWheelFinished;Fn<double(double)> _adjustCallback;Fn<void(double)> _changeProgressCallback,_changeFinishedCallback;int updates=0,announced=0;bool isEnabled()const{return enabled;}void setEnabled(bool v){enabled=v;}void setCursor(int){}void update(){assert(alive);++updates;}void accessibilityValueChanged(){assert(alive);++announced;}bool moveByWheel()const{return _byWheelFinished!=nullptr;}double getCurrentValue()const{return _mouseDown?_downValue:_value;}double computeValue(QPoint p)const{return p.x()/100.;}void setValue(double v){assert(alive);_value=v;}void setDisabled(bool);void setMoveByWheel(bool);void mouseMoveEvent(QMouseEvent*);void mousePressEvent(QMouseEvent*);void mouseReleaseEvent(QMouseEvent*);void wheelEvent(QWheelEvent*);void keyPressEvent(QKeyEvent*);void updateDownValueFromPos(const QPoint&);};
base::Weak base::make_weak(ContinuousSlider *s){return {&s->alive};}
'''+methods+r'''
int main(){ContinuousSlider s;int progress=0,finished=0;s._changeProgressCallback=[&](double){++progress;};s._changeFinishedCallback=[&](double){++finished;};s.setMoveByWheel(true);QMouseEvent m;QWheelEvent w;QKeyEvent k;s.setDisabled(true);s.mousePressEvent(&m);s.mouseMoveEvent(&m);s.mouseReleaseEvent(&m);s.wheelEvent(&w);s.keyPressEvent(&k);assert(progress==0&&finished==0&&s._value==.5&&!s._mouseDown);assert(!s.enabled);s.setDisabled(false);s.mousePressEvent(&m);assert(s._mouseDown&&progress==1);s.setDisabled(true);assert(!s._mouseDown);s.setDisabled(false);s.mouseReleaseEvent(&m);assert(finished==0&&s._value==.5);s.wheelEvent(&w);assert(s._byWheelFinished->active);s.setDisabled(true);assert(!s._byWheelFinished->active);s._byWheelFinished->flush();assert(finished==0);s.setDisabled(false);s.keyPressEvent(&k);assert(finished==1);s._changeProgressCallback=[&](double){s.setDisabled(true);};s.keyPressEvent(&k);assert(finished==1);s.setDisabled(false);s._changeProgressCallback=[&](double){s.setMoveByWheel(false);};s.wheelEvent(&w);assert(!s._byWheelFinished);s.setMoveByWheel(true);s._changeProgressCallback=[&](double){s.setDisabled(true);};s.wheelEvent(&w);assert(!s._byWheelFinished->active);s.setDisabled(false);s._changeProgressCallback=[&](double){s.alive=false;};s.keyPressEvent(&k);assert(finished==1);std::cout<<"ContinuousSlider production handlers PASS: disabled mouse/wheel/key, drag and delayed-wheel cancellation, callback disable/remove/destroy, native enabled state\n";}
''')


def playback_labels(root):
    text = source('Telegram/SourceFiles/media/view/media_view_playback_controls.cpp')
    methods = '\n'.join(function(text, s) for s in (
        'void PlaybackControls::updatePlayPauseResumeState(',
        'void PlaybackControls::setInFullScreen(',
        'void PlaybackControls::updateVolumeToggleIcon(')
    )
    cpp(root, 'viewer_labels', r'''
#include <cassert>
#include <iostream>
#include <string>
using QString=std::string;
namespace tr{constexpr int now=0;QString lng_shortcuts_media_pause(int){return "Pause";}QString lng_shortcuts_media_play(int){return "Play";}QString lng_call_unmute_audio(int){return "Unmute";}QString lng_call_mute_audio(int){return "Mute";}QString luxury_PlayerEnterFullscreen(int){return "Enter fullscreen";}QString luxury_PlayerExitFullscreen(int){return "Exit fullscreen";}}
namespace st{int mediaviewPauseIcon=0,mediaviewPauseIconOver=0,mediaviewFullScreenOutIcon=0,mediaviewFullScreenOutIconOver=0,mediaviewVolumeIcon1=0,mediaviewVolumeIcon1Over=0,mediaviewVolumeIcon2=0,mediaviewVolumeIcon2Over=0;}
namespace Player{struct TrackState{bool state=false;};}bool ShowPauseIcon(bool v){return v;}
struct Button{QString name;void setAccessibleName(QString v){name=v;}void setIconOverride(const int*,const int*){}};
struct Delegate{double volume=0.;double playbackControlsCurrentVolume(){return volume;}};
struct PlaybackControls{Delegate owner,*_delegate=&owner;Button play,full,volume;Button *_playPauseResume=&play,*_fullScreenToggle=&full,*_volumeToggle=&volume;bool _showPause=false,_inFullScreen=false;int _seekPositionMs=-1;void updatePlayPauseResumeState(const Player::TrackState&);void setInFullScreen(bool);void updateVolumeToggleIcon();};
'''+methods+r'''
int main(){PlaybackControls c;c.updatePlayPauseResumeState({false});assert(c.play.name=="Play");c.updatePlayPauseResumeState({true});assert(c.play.name=="Pause");c._seekPositionMs=0;c.updatePlayPauseResumeState({false});assert(c.play.name=="Pause");c._seekPositionMs=-1;c.updatePlayPauseResumeState({false});assert(c.play.name=="Play");c.setInFullScreen(false);assert(c.full.name=="Enter fullscreen");c.setInFullScreen(true);assert(c.full.name=="Exit fullscreen");c.setInFullScreen(false);assert(c.full.name=="Enter fullscreen");c.updateVolumeToggleIcon();assert(c.volume.name=="Unmute");c.owner.volume=.5;c.updateVolumeToggleIcon();assert(c.volume.name=="Mute");std::cout<<"Viewer production labels PASS: initial unchanged states, play/pause/seek, fullscreen in/out, mute/unmute\n";}
''')
    ctor = text.split('PlaybackControls::PlaybackControls(', 1)[1].split('void PlaybackControls::handleSeekProgress(', 1)[0]
    for control in ('_playPauseResume', '_volumeController', '_playbackSlider', '_pictureInPicture', '_fullScreenToggle'):
        assert control+'->setAccessibleName(' in ctor, control
    assert 'if (_speedToggle)' in ctor
    print('Viewer constructor semantic-name/optional speed wiring PASS (source only; no native AT enumeration)')
    refresh = function(text, 'void PlaybackControls::updateSpeedToggleQuality(')
    optional = text.split(', _speedToggle(', 1)[1].split('\n, _fullScreenToggle(', 1)[0]
    cpp(root, 'viewer_optional_quality', r'''
#include <cassert>
#include <iostream>
#include <memory>
#include <vector>
namespace Media{using VideoQuality=int;}
namespace st{constexpr int mediaviewSpeedButton=0;}
namespace Player{struct SettingsButton{int quality=-1,sets=0;template<class T>SettingsButton(T*,int){}void setQuality(int v){quality=v;++sets;}};}
template<class T>struct object_ptr{std::unique_ptr<T> ptr;object_ptr(std::nullptr_t){}template<class P>object_ptr(P*p,int style):ptr(std::make_unique<T>(p,style)){}explicit operator bool()const{return ptr!=nullptr;}T*operator->()const{assert(ptr);return ptr.get();}};
struct Controller{std::vector<int> qualities;int calls=0;void setQualities(const std::vector<int>&v){qualities=v;++calls;}};
struct Delegate{std::vector<int> qualities;int current=720;auto playbackControlsQualities(){return qualities;}int playbackControlsCurrentQuality(){return current;}};
struct PlaybackControls{Delegate owner,*_delegate=&owner;bool _speedControllable;std::vector<int> _qualitiesList;object_ptr<Player::SettingsButton>_speedToggle;std::unique_ptr<Controller>_speedController;PlaybackControls(bool speed,bool quality):_speedControllable(speed),_qualitiesList(quality?std::vector<int>{720}:std::vector<int>{}),_speedToggle(
'''+optional+r'''{owner.qualities=_qualitiesList;if(_speedToggle)_speedController=std::make_unique<Controller>();}void updateSpeedToggleQuality();};
'''+refresh+r'''
int main(){for(bool speed:{false,true})for(bool quality:{false,true}){PlaybackControls c(speed,quality);assert(bool(c._speedToggle)==(speed||quality));c.updateSpeedToggleQuality();if(c._speedToggle)assert(c._speedToggle->quality==720&&c._speedController->calls==0);c.owner.qualities={360,1080};c.owner.current=1080;c.updateSpeedToggleQuality();assert(c._qualitiesList==c.owner.qualities);if(c._speedToggle)assert(c._speedToggle->quality==1080&&c._speedController->calls==1);c.updateSpeedToggleQuality();if(c._speedToggle)assert(c._speedController->calls==1);}std::cout<<"Viewer production optional initializer+quality refresh PASS: speed/quality four combinations, absent toggle on later quality refresh, unchanged/changed controller\n";}
''')


def checkbox_notification_lifetime(root):
    text = source('Telegram/lib_ui/ui/widgets/checkbox.cpp')
    setter = function(text, 'void Checkbox::setChecked(')
    action = function(text, 'void Checkbox::accessibilityDoAction(')
    cpp(root, 'checkbox_notification', r'''
#include <cassert>
#include <functional>
#include <iostream>
#include <string>
using QString=std::string;
namespace anim{namespace type{constexpr int normal=0;}}
struct QAccessibleActionInterface{static QString pressAction(){return "press";}static QString toggleAction(){return "toggle";}};
struct Checkbox;
namespace base{struct Weak{const bool *alive;explicit operator bool()const{return *alive;}};Weak make_weak(Checkbox*);}
struct View{bool value=false;std::function<void()> notify;bool checked()const{return value;}void setChecked(bool v,int){value=v;if(notify)notify();}};
struct Checkbox{View view,*_check=&view;bool alive=true,disabled=false;int states=0,events=0,presses=0;bool destroyOnState=false;enum class NotifyAboutChange{Notify,DontNotify};struct Events{Checkbox *owner;void fire_copy(bool){assert(owner->alive);++owner->events;}}_checkedChanges{this};struct State{bool checked=false;};void accessibilityStateChanged(State){assert(alive);++states;if(destroyOnState)alive=false;}bool isDisabled()const{return disabled;}void handlePress(){++presses;}void setChecked(bool,NotifyAboutChange=NotifyAboutChange::Notify);void accessibilityDoAction(const QString&);};
base::Weak base::make_weak(Checkbox*c){return {&c->alive};}
'''+setter+action+r'''
int main(){Checkbox c;c.setChecked(true);assert(c.states==1&&c.events==1);c.setChecked(true);assert(c.states==1&&c.events==1);c.setChecked(false,Checkbox::NotifyAboutChange::DontNotify);assert(c.states==2&&c.events==1);Checkbox dying;dying.view.notify=[&]{dying.alive=false;};dying.setChecked(true);assert(!dying.alive&&dying.states==0&&dying.events==0);Checkbox at;at.destroyOnState=true;at.setChecked(true);assert(!at.alive&&at.events==0);c.accessibilityDoAction("press");c.accessibilityDoAction("toggle");assert(c.presses==2);c.disabled=true;c.accessibilityDoAction("toggle");assert(c.presses==2);std::cout<<"Checkbox production setter/actions PASS: same-value/DontNotify, deletion by view/AT callback, native toggle alias and disabled guard\n";}
''')


def compact_player_rtl(root):
    text = source('Telegram/SourceFiles/media/player/media_player_widget.cpp')
    geometry = function(text, 'void Widget::updateControlsWrapGeometry(')
    hit = function(text, 'void Widget::updateOverLabelsState(QPoint pos)')
    mirror = function(source('Telegram/lib_ui/ui/style/style_core_direction.h'), 'inline QRect rtlrect(const QRect &r, int outerw)')
    cpp(root, 'player_rtl', r'''
#include <cassert>
#include <iostream>
struct QPoint{int X=0,Y=0;int x()const{return X;}int y()const{return Y;}};
struct QRect{int X=0,Y=0,W=0,H=0;QRect()=default;QRect(int x,int y,int w,int h):X(x),Y(y),W(w),H(h){}int x()const{return X;}int y()const{return Y;}int width()const{return W;}int height()const{return H;}bool contains(QPoint p)const{return p.x()>=X&&p.x()<X+W&&p.y()>=Y&&p.y()<Y+H;}};
namespace style{bool rtl=false;bool RightToLeft(){return rtl;}
'''+mirror+r'''}
namespace st{struct{int width()const{return 18;}}mediaPlayerControlsFade;constexpr int mediaPlayerCloseRight=0,mediaPlayerPlayTop=0;struct{int fullWidth=8;}mediaPlayerPlayback;}
struct Control{int w=0;int width()const{return w;}int height()const{return 30;}};
struct Wrap{int parentWidth=0,w=0,h=0,x=0;void resize(int W,int H){w=W;h=H;}void moveToRight(int right,int){x=style::rtl?right:parentWidth-right-w;}void move(int X,int){x=X;}};
struct Widget{int outer=700,timeInset=102,nameLeft=84;bool over=false;Control close{39},expand{24},time{28},repeat{30};Wrap wrap;Control *_close=&close,*_expand=&expand,*_timeLabel=&time,*_repeatToggle=&repeat;Wrap *_rightControls=&wrap;Widget(){wrap.parentWidth=outer;}int width()const{return outer;}int height()const{return 35;}Wrap*rightControls(){return &wrap;}int getTimeRight()const{return timeInset;}int getNameLeft()const{return nameLeft;}QRect myrtlrect(int x,int y,int w,int h){return style::rtlrect(QRect(x,y,w,h),outer);}void updateOverLabelsState(bool v){over=v;}void updateOverLabelsState(QPoint);void updateControlsWrapGeometry();};
'''+geometry+hit+r'''
int main(){for(int width:{459,460,700})for(int inset:{42,102,132})for(int name:{43,84}){Widget w;w.outer=w.wrap.parentWidth=width;w.timeInset=inset;w.nameLeft=name;style::rtl=false;w.updateControlsWrapGeometry();const auto ltr=w.wrap.x;const auto size=w.wrap.w;assert(ltr==width-63-size);QPoint p{(name+width-63-inset)/2,12};w.updateOverLabelsState(p);assert(w.over);style::rtl=true;w.updateControlsWrapGeometry();assert(w.wrap.x==63&&w.wrap.x==width-ltr-size);w.updateOverLabelsState({width-1-p.x(),p.y()});assert(w.over);w.updateOverLabelsState({name-1,32});assert(!w.over);}std::cout<<"Compact-player production geometry/hitbox PASS: LTR unchanged and RTL mirror at459/460/700 with song/voice/speed/transport metric controls (not rendering)\n";}
''')
    paint = function(text, 'void Widget::setupRightControls(')
    assert 'p.translate(raw->width(), 0);' in paint and 'p.scale(-1., 1.);' in paint
    assert 'clip = style::rtlrect(clip, raw->width());' in paint
    print('Player fade-direction/dirty-clip wiring PASS (source only, raster/DPI unverified)')


def contact_name_order(root):
    text = source('Telegram/SourceFiles/boxes/add_contact_box.cpp')
    methods = '\n'.join(function(text, signature) for signature in (
        'void AddContactBox::submit()', 'void AddContactBox::retry()',
        'void AddContactBox::setInnerFocus()'))
    cpp(root, 'contact_name_order', r'''
#include <cassert>
#include <iostream>
#include <string>
struct QString:std::string{using std::string::string;using std::string::operator=;bool isEmpty()const{return empty();}};
struct Input{static inline Input *focused=nullptr;bool enabled=true;QString text;bool hasFocus()const{return focused==this;}void setFocus(){focused=this;}void setFocusFast(){setFocus();}bool isEnabled()const{return enabled;}void setDisabled(bool v){enabled=!v;}void setText(QString v){text=v;}const auto &getLastText()const{return text;}void clearText(){text.clear();}void finishAnimating(){}};
struct AddContactBox{Input first,last,phone;Input *_first=&first,*_last=&last,*_phone=&phone;bool _invertOrder=false,_retrying=true;int _addRequest=42,_contactId=71,saves=0;void save(){++saves;}void showChildren(){}void updateButtons(){}void update(){}void submit();void retry();void setInnerFocus();};
''' + methods + r'''
int main(){for(bool inverted:{false,true})for(bool phoneEnabled:{false,true}){AddContactBox b;b._invertOrder=inverted;b.phone.enabled=phoneEnabled;auto visualFirst=inverted?&b.last:&b.first;auto visualSecond=inverted?&b.first:&b.last;visualFirst->setFocus();b.submit();assert(visualSecond->hasFocus()&&b.saves==0);b.submit();if(phoneEnabled){assert(b.phone.hasFocus()&&b.saves==0);b.submit();}assert(b.saves==1);Input outside;outside.setFocus();b.submit();assert(b.saves==1);b.retry();assert(visualFirst->hasFocus()&&b.phone.enabled&&!b._retrying&&!b._addRequest&&!b._contactId);b.setInnerFocus();assert(visualFirst->hasFocus());b.first.text="Ada";b.phone.setDisabled(false);b.setInnerFocus();assert(b.phone.hasFocus());b.phone.setDisabled(true);b.setInnerFocus();assert(visualFirst->hasFocus());}std::cout<<"Contact production submit/retry/focus PASS: both name orders, editable/prefilled phone, outside focus; save stub only, no contacts/network\n";}
''')
    prepare = function(text, 'void AddContactBox::prepare()')
    assert 'setTabOrder(_last, _first);' in prepare
    assert '_first->submits(' in prepare and '_last->submits(' in prepare
    assert '&Ui::PhoneInput::submitted' in prepare
    save = function(text, 'void AddContactBox::save()')
    assert 'if (_addRequest)' in save and 'IsValidPhone(phone)' in save
    assert 'firstName.isEmpty() && lastName.isEmpty()' in save
    print('Contact validation and all submit wiring source contracts PASS; native focus/network behavior unverified')


def statistics_checkbox_semantics(root):
    text = source('Telegram/SourceFiles/statistics/widgets/chart_lines_filter_widget.cpp')
    cls = text[text.index('class ChartLinesFilterWidget::FlatCheckbox'):text.index('ChartLinesFilterWidget::FlatCheckbox::FlatCheckbox(')]
    ctor = text[text.index('ChartLinesFilterWidget::FlatCheckbox::FlatCheckbox('):text.index('void ChartLinesFilterWidget::FlatCheckbox::setOuterWidth(')]
    methods = '\n'.join(function(text, s) for s in (
        'void ChartLinesFilterWidget::FlatCheckbox::setChecked(',
        'bool ChartLinesFilterWidget::FlatCheckbox::checked()'))
    callback = function(text, 'button->setClickedCallback(').removeprefix('button->setClickedCallback(')
    # Keep the actual owning callback and last-visible-series guard, not a test copy.
    callback = callback.replace('[=, raw = button.get()]', '[this, id, raw = button]')
    cpp(root, 'statistics_checkbox', r'''
#include <algorithm>
#include <cassert>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
using QString=std::string;struct QColor{int c=0;};struct QPaintEvent{};template<class T>struct not_null{T p;T get()const{return p;}operator T()const{return p;}};
namespace Qt{constexpr int NoModifier=0,LeftButton=1;}
struct QAccessible{enum Role{Button,CheckBox};};struct QAccessibleActionInterface{static QString pressAction(){return "press";}static QString toggleAction(){return "toggle";}};
namespace Ui{struct AccessibilityState{bool checkable=false,checked=false,extSelectable=false,multiSelectable=false,pressed=false;};struct RpWidget{bool alive=true;};struct AbstractButton:RpWidget{QString name;bool disabled=false,down=false;int changed=0,updates=0;std::function<void()> callback,onState;AbstractButton(RpWidget*){}virtual ~AbstractButton()=default;virtual QAccessible::Role accessibilityRole(){return QAccessible::Button;}virtual AccessibilityState accessibilityState()const{return {.pressed=down};}virtual void accessibilityDoAction(const QString &s){if(s==QAccessibleActionInterface::pressAction()&&!disabled)clicked(0,1);}virtual void paintEvent(QPaintEvent*){}bool isDown()const{return down;}void setAccessibleName(QString s){name=s;}void resize(int,int){}void update(){assert(alive);++updates;}void accessibilityStateChanged(AccessibilityState s){assert(s.checked);++changed;if(onState)onState();}void clicked(int,int){if(!disabled&&callback)callback();}};namespace Text{struct String{QString text;String(int,QString s):text(s){}int minHeight()const{return 10;}int maxWidth()const{return int(text.size());}};}namespace Animations{struct Simple{int shift=0;void stop(){}template<class F>void start(F,double,double,int){} };}}
namespace st{struct Color{QColor c;};inline Color fg,bg;inline Color *premiumButtonFg=&fg,*boxBg=&bg;constexpr int statisticsDetailsPopupStyle=0,statisticsChartFlatCheckboxMargins=0,statisticsChartFlatCheckboxCheckWidth=1,statisticsChartFlatCheckboxShrinkkWidth=1,shakeDuration=100;}
namespace rect::m::sum{int v(int){return 2;}int h(int){return 2;}}
namespace base{struct Weak{const bool *alive;explicit operator bool()const{return *alive;}};template<class T>Weak make_weak(T *p){return {&p->alive};}}
struct ChartLinesFilterWidget:Ui::RpWidget{class FlatCheckbox;struct Entry{int id;bool enabled;};struct Changes{int count=0;void fire(Entry){++count;}}_buttonEnabledChanges;std::vector<std::unique_ptr<FlatCheckbox>> _buttons;int shakes=0;void wire(FlatCheckbox *button,int id);};
''' + cls + ctor + methods + r'''
void ChartLinesFilterWidget::FlatCheckbox::paintEvent(QPaintEvent*){}
void ChartLinesFilterWidget::FlatCheckbox::shake(){}
void ChartLinesFilterWidget::wire(FlatCheckbox *button,int id){button->callback=''' + callback + r''';}
int main(){ChartLinesFilterWidget w;for(int i=0;i<2;++i){w._buttons.push_back(std::make_unique<ChartLinesFilterWidget::FlatCheckbox>(not_null<Ui::RpWidget*>{&w},i?"Comments":"Views",QColor{}));w.wire(w._buttons.back().get(),i);}auto a=w._buttons[0].get();auto b=w._buttons[1].get();assert(a->name=="Views"&&a->accessibilityRole()==QAccessible::CheckBox);assert(a->accessibilityState().checkable&&a->accessibilityState().checked);a->accessibilityDoAction("toggle");assert(!a->checked()&&w._buttonEnabledChanges.count==1&&a->changed==1);b->accessibilityDoAction("toggle");assert(b->checked()&&w._buttonEnabledChanges.count==1);a->accessibilityDoAction("press");assert(a->checked()&&w._buttonEnabledChanges.count==2);a->disabled=true;a->accessibilityDoAction("toggle");assert(a->checked()&&w._buttonEnabledChanges.count==2);a->setChecked(true,false);assert(a->changed==2);a->disabled=false;a->onState=[&]{w.alive=false;};a->accessibilityDoAction("toggle");assert(w._buttonEnabledChanges.count==2);std::cout<<"Statistics actual checkbox semantics/click guard PASS: name/role/checked, press+toggle, disabled, last visible series, owner destruction notification\n";}
''')
    paint = function(text, 'void ChartLinesFilterWidget::FlatCheckbox::paintEvent(')
    assert 'paintKeyboardFocus(' in paint
    print('Statistics shared focus-paint source contract PASS; native AT/focus/contrast unverified')


def passport_row_semantics(root):
    text = source('Telegram/SourceFiles/passport/ui/passport_form_row.cpp')
    methods = function(text, 'void FormRow::updateContent(')
    header = source('Telegram/SourceFiles/passport/ui/passport_form_row.h')
    base = source('Telegram/lib_ui/ui/rp_widget.h')
    declarations = []
    base_declarations = []
    for method in ('accessibilityName', 'accessibilityDescription', 'accessibilityValue'):
        declaration = re.search(r'QString ' + method + r'\(\)(?: const)? override;', header)
        assert declaration, method
        declarations.append(declaration.group())
        base_declaration = re.search(r'virtual QString ' + method + r'\(\)(?: const)?;', base)
        assert base_declaration, method
        base_declarations.append(base_declaration.group().removesuffix(';') + '{return {};}')
        signature = declaration.group().removesuffix(' override;').replace('QString ', 'QString FormRow::', 1)
        methods += '\n' + function(text, signature)
    semantic_base = 'struct SemanticBase{virtual ~SemanticBase()=default;' + ''.join(base_declarations) + '};'
    cpp(root, 'passport_row', r'''
#include <cassert>
#include <functional>
#include <iostream>
#include <string>
struct QString:std::string{using std::string::string;using std::string::operator=;QString(std::string v):std::string(v){}bool isEmpty()const{return empty();}friend QString operator+(const QString &a,char b){return QString(std::string(a)+b);}friend QString operator+(const QString &a,const QString &b){return QString(std::string(a)+std::string(b));}};
namespace Qt{constexpr int LayoutDirectionAuto=0;}namespace anim{enum class type{instant,normal};}constexpr int TextParseMultiline=1;struct TextParseOptions{int a,b,c,d;};int NameTextOptions(){return 0;}namespace st{constexpr int semiboldTextStyle=0,defaultTextStyle=0,fadeWrapDuration=100;}
namespace tr{constexpr int now=0;QString luxury_PassportRowReady(int){return "Ready";}QString luxury_PassportRowMissing(int){return "Missing information";}QString luxury_PassportRowError(int){return "Needs attention";}}
struct Text{QString text;template<class T>void setText(int,const QString &s,T){text=s;}QString toString()const{return text;}};struct Animation{void stop(){}template<class F>void start(F,double,double,int){}};
''' + semantic_base + r'''
struct FormRow:SemanticBase{Text _title,_description;Animation _errorAnimation;bool _ready=false,_error=false,alive=true;int names=0,descriptions=0,values=0;std::function<void()> onName,onDescription;int width()const{return 300;}void resizeToWidth(int){}void update(){assert(alive);}void accessibilityNameChanged(){assert(alive);++names;if(onName)onName();}void accessibilityDescriptionChanged(){assert(alive);++descriptions;if(onDescription)onDescription();}void accessibilityValueChanged(){assert(alive);++values;}void updateContent(const QString&,const QString&,bool,bool,anim::type);''' + ''.join(declarations) + r'''};
namespace base{struct Weak{const bool *alive;explicit operator bool()const{return *alive;}};Weak make_weak(FormRow *r){return {&r->alive};}}
''' + methods + r'''
int main(){FormRow r;for(bool ready:{false,true})for(bool error:{false,true}){r.updateContent("Identity", "Upload a document", ready,error,anim::type::instant);assert(r.accessibilityName()=="Identity");assert(r.accessibilityValue()==(error?"Needs attention":ready?"Ready":"Missing information"));assert(r.accessibilityDescription().find("Upload a document")!=std::string::npos&&r.accessibilityDescription().find(r.accessibilityValue())!=std::string::npos);assert(r._ready==(ready&&!error));}assert(r.names==4&&r.descriptions==4&&r.values==4);r.updateContent("Address","",false,false,anim::type::normal);assert(r.accessibilityName()=="Address"&&r.accessibilityDescription()=="Missing information");FormRow dying;dying.onName=[&]{dying.alive=false;};dying.updateContent("Phone","",false,true,anim::type::normal);assert(dying.names==1&&!dying.descriptions&&!dying.values);FormRow dyingLater;dyingLater.onDescription=[&]{dyingLater.alive=false;};dyingLater.updateContent("Email","",true,false,anim::type::normal);assert(dyingLater.names==1&&dyingLater.descriptions==1&&!dyingLater.values);std::cout<<"Passport actual content/name/description/status hooks PASS: ready/missing/error priority, refresh, notification receiver destruction; no documents/auth/network\n";}
''')
    assert 'paintKeyboardFocus(p);' in function(text, 'void FormRow::paintEvent(')
    print('Passport actual base/header virtual signatures and shared focus-paint wiring PASS; native AT/build unverified')


def shipping_option_semantics(root):
    text = source('Telegram/SourceFiles/payments/ui/payments_panel.cpp')
    choose = function(text, 'void Panel::chooseShippingOption(')
    row = choose[choose.index('const auto label ='):choose.index('button->geometryValue(')]
    cpp(root, 'shipping_option', r'''
#include <cassert>
#include <functional>
#include <iostream>
#include <memory>
#include <numeric>
#include <string>
#include <vector>
using QString=std::string;using int64=long long;template<class T>struct not_null{T p;T get()const{return p;}};
struct LabeledPrice{int64 price=0;};struct ShippingOption{QString title;std::vector<LabeledPrice> prices;};struct ShippingOptions{QString currency;};struct Group{int value=-1;void setValue(int v){value=v;}};
struct AbstractButton{QString name,description;std::function<void()> cb;AbstractButton(void *parent=nullptr){}void setAccessibleName(QString s){name=s;}void setAccessibleDescription(QString s){description=s;}void setClickedCallback(std::function<void()> f){cb=std::move(f);}};
struct FlatLabel{QString text;FlatLabel(void*,QString s,int):text(s){}};std::vector<std::unique_ptr<FlatLabel>> labels;std::vector<std::unique_ptr<AbstractButton>> areas;
template<class T,class...A>T *CreateChild(A...a){auto p=std::make_unique<T>(a...);auto r=p.get();if constexpr(std::is_same_v<T,FlatLabel>)labels.push_back(std::move(p));else areas.push_back(std::move(p));return r;}
namespace st{constexpr int paymentsShippingLabel=0,paymentsShippingPrice=0;}
namespace ranges{template<class R,class T,class F,class P>T accumulate(const R &r,T v,F f,P p){for(const auto &i:r)v=f(v,i.*p);return v;}}
QString FillAmountAndCurrency(int64 n,const QString &currency){return std::to_string(n)+" "+currency;}
void check(const ShippingOption &option,const ShippingOptions &options,int index){not_null<void*> layout{nullptr};auto group=std::make_shared<Group>();AbstractButton radio;auto button=&radio;
''' + row + r'''
const auto expected=FillAmountAndCurrency(std::accumulate(option.prices.begin(),option.prices.end(),int64(0),[](int64 v,const LabeledPrice &p){return v+p.price;}),options.currency);assert(button->name==option.title&&area->name==option.title);assert(button->description==expected&&area->description==expected&&price->text==expected&&label->text==option.title);assert(group->value==-1);area->cb();assert(group->value==index);}
int main(){check({"Courier",{{1250},{250},{-50}}},{"USD"},3);check({"Pickup",{}},{"EUR"},0);check({"A long localized delivery title",{{10000}}},{"JPY"},2);std::cout<<"Shipping actual row title/price accessible setters and selection callback PASS: summed/zero/discount/multiple currencies, no checkout/payment\n";}
''')
    assert 'group->setChangedCallback(' in choose
    assert '_delegate->panelChangeShippingOption(' in choose
    assert 'options.list[index].id' in choose
    assert 'options.selectedId' in choose and '&ShippingOption::id' in choose
    print('Shipping initial/changed selection source contracts retained; duplicate accessible widgets not consolidated, native AT unverified')


def other_account_counter_reactivity(root):
    text = source('Telegram/SourceFiles/window/window_main_menu.cpp')
    current = function(text, 'OthersUnreadState OtherAccountsUnreadStateCurrent(')
    producer = function(text, 'rpl::producer<OthersUnreadState> OtherAccountsUnreadState(')
    sidebar = function(source('Telegram/SourceFiles/window/window_filters_menu.cpp'), '[=](const OthersUnreadState &state)')
    dialogs = function(source('Telegram/SourceFiles/dialogs/dialogs_widget.cpp'), '[=](const Window::OthersUnreadState &state)')
    setter = function(source('Telegram/SourceFiles/luxury/luxury_settings.cpp'), 'void LuxurySettings::setHideNotificationCounters(')
    repo = Path(__file__).resolve().parents[2]
    cpp(root, 'other_account_counters', r'''
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <utility>
template<class...T>constexpr bool false_t(T&&...){return false;}
#include <rpl/event_stream.h>
#include <rpl/variable.h>
#include <rpl/range.h>
#include <rpl/then.h>
#include <rpl/map.h>
#include <rpl/merge.h>
namespace base::assertion{void log(const char*,const char*,int){std::abort();}}
template<class T>struct not_null{T p;operator T()const{return p;}T operator->()const{return p;}};
struct SessionData{int count=6;bool muted=false;int unreadWithMentionsBadge()const{return count;}bool unreadWithMentionsBadgeMuted()const{return muted;}};
struct Session{SessionData state;const SessionData &data()const{return state;}};
namespace Main{struct Account{std::unique_ptr<Session> session=std::make_unique<Session>();Session *maybeSession()const{return session.get();}};}
struct Domain{std::map<int,std::shared_ptr<Main::Account>> list;const auto &accounts()const{return list;}};
namespace Core{struct Application{Domain owner;rpl::event_stream<> unread;const Domain &domain()const{return owner;}auto unreadBadgeChanges(){return unread.events();}};Application &App(){static Application app;return app;}}
struct LuxurySettings{rpl::variable<bool> _hideNotificationCounters=false;static LuxurySettings &getInstance(){static LuxurySettings s;return s;}bool hideNotificationCounters()const{return _hideNotificationCounters.current();}auto hideNotificationCountersChanges()const{return _hideNotificationCounters.changes();}void setHideNotificationCounters(bool);void save(){}};
''' + setter + r'''
namespace Window{struct OthersUnreadState{int count=0;bool allMuted=true;};
''' + current + '\n' + producer + r'''
}
using Window::OtherAccountsUnreadState;using Window::OthersUnreadState;
namespace st{int windowFiltersMainMenuUnread=1,windowFiltersMainMenuUnreadMuted=2,dialogsMenuToggleUnread=3,dialogsMenuToggleUnreadMuted=4;}
struct Holder{rpl::lifetime life;rpl::lifetime &lifetime(){return life;}};struct Icon{const int *value=nullptr;int updates=0;void setIconOverride(const int *a,const int *b){assert(a==b);value=a;++updates;}};
struct Sidebar{Holder _outer;Icon _menu;void bind(Main::Account *a){OtherAccountsUnreadState({a})|rpl::on_next(''' + sidebar + r''',_outer.lifetime());}};
struct Dialogs{struct Toggle:Icon,Holder{};struct{std::unique_ptr<Toggle> toggle=std::make_unique<Toggle>();}_mainMenu;void bind(Main::Account *a){OtherAccountsUnreadState({a})|rpl::on_next(''' + dialogs + r''',_mainMenu.toggle->lifetime());}};
int main(){auto &app=Core::App();auto &settings=LuxurySettings::getInstance();for(bool muted:{false,true})for(bool initiallyHidden:{false,true}){app.owner.list.clear();auto me=std::make_shared<Main::Account>();auto other=std::make_shared<Main::Account>();me->session->state.count=500;other->session->state.muted=muted;app.owner.list[0]=me;app.owner.list[1]=other;settings.setHideNotificationCounters(initiallyHidden);Sidebar side;Dialogs dialog;side.bind(me.get());dialog.bind(me.get());auto check=[&](bool hidden){assert(side._menu.value==(hidden?nullptr:muted?&st::windowFiltersMainMenuUnreadMuted:&st::windowFiltersMainMenuUnread));assert(dialog._mainMenu.toggle->value==(hidden?nullptr:muted?&st::dialogsMenuToggleUnreadMuted:&st::dialogsMenuToggleUnread));assert(Window::OtherAccountsUnreadStateCurrent({me.get()}).count==6);};check(initiallyHidden);for(bool hidden:{true,false,true,false}){settings.setHideNotificationCounters(hidden);check(hidden);}settings.setHideNotificationCounters(true);Sidebar fresh;fresh.bind(me.get());assert(!fresh._menu.value);settings.setHideNotificationCounters(false);check(false);auto old=side._menu.updates;side._outer.life.destroy();dialog._mainMenu.toggle->life.destroy();auto dialogOld=dialog._mainMenu.toggle->updates;settings.setHideNotificationCounters(true);app.unread.fire({});assert(side._menu.updates==old&&dialog._mainMenu.toggle->updates==dialogOld);settings.setHideNotificationCounters(false);other->session->state.count=0;app.unread.fire({});assert(!fresh._menu.value);other->session->state.count=3;app.unread.fire({});assert(fresh._menu.value);app.owner.list.erase(1);app.unread.fire({});assert(!fresh._menu.value);}std::cout<<"Other-account actual producer+two icon callbacks real-rpl PASS: live setting toggle without unread, initial/muted/zero/removal/disposed lifetime, raw counts retained\n";}
''', extra=['-I', str(repo / 'Telegram/lib_rpl'), '-I', str(repo / 'Telegram/lib_base'), '-I', str(repo / 'Telegram/ThirdParty/GSL/include')])


def shared_gift_preference(root):
    text = source('Telegram/SourceFiles/history/view/controls/history_view_compose_controls.cpp')
    visibility = function(text, 'void ComposeControls::updateControlsVisibility()')
    prefix = visibility[visibility.index('const auto &settings'):visibility.index('\n\tif (_botCommandStart)')]
    gift = function(visibility, 'if (_giftToUser)')
    field = function(text, 'void ComposeControls::fieldChanged(')
    desired = field[field.index('(giftToUserVisible != ('):field.index('\n\t\t|| (silentVisible')]
    geometry = function(text, 'void ComposeControls::updateControlsGeometry(')
    own_hidden = geometry[geometry.index('const auto giftToUser ='):geometry.index('// The attach button and the replace-media button')]
    width_term = re.search(r'- \(giftToUser \? _giftToUser->width\(\) : 0\)', geometry).group()
    settings_start = text.index('\trpl::merge(\n\t\tLuxurySettings::getInstance().showAttachButtonInMessageFieldChanges()')
    settings_end = text.index('}, _wrap->lifetime());', settings_start) + len('}, _wrap->lifetime());')
    subscription = text[settings_start:settings_end]
    methods = sorted(set(re.findall(r'LuxurySettings::getInstance\(\)\.(\w+Changes)\(', subscription)))
    setters = function(source('Telegram/SourceFiles/luxury/luxury_settings.cpp'), 'void LuxurySettings::setShowGiftButtonInMessageField(')
    eligibility = function(text, 'void ComposeControls::refreshSendGiftToggle()')
    has = eligibility[eligibility.index('const auto has ='):eligibility.index('\n\tif (!_giftToUser')]
    other_methods = ''.join('auto ' + m + '()const{return unused.events();}' for m in methods if m != 'showGiftButtonInMessageFieldChanges')
    repo = Path(__file__).resolve().parents[2]
    cpp(root, 'shared_gift', r'''
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <utility>
template<class...T>constexpr bool false_t(T&&...){return false;}
#include <rpl/event_stream.h>
#include <rpl/variable.h>
#include <rpl/merge.h>
#include <rpl/map.h>
namespace base::assertion{void log(const char*,const char*,int){std::abort();}}
struct LuxurySettings{rpl::variable<bool> _showGiftButtonInMessageField=true;rpl::event_stream<> unused;static LuxurySettings &getInstance(){static LuxurySettings s;return s;}bool showGiftButtonInMessageField()const{return _showGiftButtonInMessageField.current();}auto showGiftButtonInMessageFieldChanges()const{return _showGiftButtonInMessageField.changes();}''' + other_methods + r'''void setShowGiftButtonInMessageField(bool);void save(){}};
''' + setters + r'''
struct DataStub{rpl::event_stream<> updates;DataStub &aiComposeTones(){return *this;}auto updated(){return updates.events();}};
namespace Api{struct DisallowedGiftType{static constexpr int Premium=1,Unlimited=2,Limited=4,Unique=8,SendHide=16;};}
struct User{int flags=0;bool self=false,bot=false,service=false,birthdayToday=true;int disallowedGiftTypes()const{return flags;}bool isServiceUser()const{return service;}bool isSelf()const{return self;}bool isBot()const{return bot;}bool birthday()const{return birthdayToday;}};
namespace Data{bool IsBirthdayToday(bool b){return b;}}
struct Session{User sender;DataStub dataState;User *user(){return &sender;}DataStub &data(){return dataState;}};
struct Toggle{bool hidden=true;int calls=0;bool isHidden()const{return hidden;}void setVisible(bool v){hidden=!v;++calls;}int width()const{return 36;}};
struct Wrap{rpl::lifetime life;rpl::lifetime &lifetime(){return life;}int size()const{return 300;}};
struct ComposeControls{enum class Mode{Normal,Scheduled};Mode _mode=Mode::Normal;Wrap wrap;Wrap *_wrap=&wrap;Toggle button;Toggle *_giftToUser=nullptr;Session owner;bool _regularWindow=true;rpl::variable<bool> _writeRestriction=false;bool extra=false,editing=false,over=false;int geometryCalls=0;
Session &session(){return owner;}bool hideExtraButtons()const{return extra;}bool isEditingMessage()const{return editing;}bool textExceedsMaxSize()const{return over;}
void updateControlsVisibility(){''' + prefix + '\n' + gift + r'''}
void updateSendButtonType(){}void orderControls(){}void updateControlsGeometry(int){++geometryCalls;}void bind(){''' + subscription + r'''}
bool needsRefresh(){const auto giftToUserVisible=_giftToUser&&!_giftToUser->isHidden();const auto hideExtra=hideExtraButtons()||isEditingMessage()||textExceedsMaxSize();return ''' + desired + r''';}
int fieldWidth()const{''' + own_hidden + r'''return 300 ''' + width_term + r''';}
bool eligible(User *user){using Type=Api::DisallowedGiftType;const auto disallowed=user?user->disallowedGiftTypes():0;const auto all=Type::Premium|Type::Unlimited|Type::Limited|Type::Unique;''' + has + r'''return has;}
void setHistory(User *user){_giftToUser=eligible(user)?&button:nullptr;updateControlsVisibility();updateControlsGeometry(_wrap->size());}}
;
int main(){auto &s=LuxurySettings::getInstance();User user;for(bool initial:{false,true}){s.setShowGiftButtonInMessageField(initial);ComposeControls c;c.bind();c.setHistory(&user);assert(c._giftToUser&&c.button.hidden==!initial);assert(c.fieldWidth()==(initial?264:300)&&!c.needsRefresh());for(bool pref:{false,true,false,true}){s.setShowGiftButtonInMessageField(pref);assert(c.button.hidden==!pref&&c.fieldWidth()==(pref?264:300)&&!c.needsRefresh());}for(int suppress=0;suppress!=3;++suppress){c.extra=suppress==0;c.editing=suppress==1;c.over=suppress==2;c.updateControlsVisibility();assert(c.button.hidden&&c.fieldWidth()==300&&!c.needsRefresh());s.setShowGiftButtonInMessageField(false);s.setShowGiftButtonInMessageField(true);assert(c.button.hidden&&!c.needsRefresh());}c.extra=c.editing=c.over=false;c._mode=ComposeControls::Mode::Scheduled;c.updateControlsVisibility();assert(c.button.hidden&&!c.needsRefresh());c._mode=ComposeControls::Mode::Normal;c.updateControlsVisibility();assert(!c.button.hidden);c._wrap->life.destroy();auto calls=c.geometryCalls;s.setShowGiftButtonInMessageField(false);assert(c.geometryCalls==calls);}for(int gate=0;gate!=7;++gate){ComposeControls c;User blocked;User *p=&blocked;if(gate==0)c._regularWindow=false;else if(gate==1)p=nullptr;else if(gate==2)c._writeRestriction=true;else if(gate==3)blocked.self=true;else if(gate==4)blocked.bot=true;else if(gate==5)blocked.service=true;else blocked.flags=15;c.setHistory(p);assert(!c._giftToUser&&c.fieldWidth()==300);}ComposeControls c;user.birthdayToday=false;assert(!c.eligible(&user));user.flags=Api::DisallowedGiftType::SendHide;assert(c.eligible(&user));user.flags=0;c.owner.sender.flags=Api::DisallowedGiftType::SendHide;assert(c.eligible(&user));std::cout<<"Shared gift actual visibility/field predicate/eligibility/width term and real-rpl settings subscription PASS: initial/live preference, typing without no-op refresh, suppressors, scheduled/user/window gates and disposed lifetime\n";}
''', extra=['-I', str(repo / 'Telegram/lib_rpl'), '-I', str(repo / 'Telegram/lib_base'), '-I', str(repo / 'Telegram/ThirdParty/GSL/include')])
    history = function(text, 'void ComposeControls::setHistory(')
    assert history.index('updateControlsVisibility();') < history.index('updateControlsGeometry(_wrap->size());')
    print('Shared compose path only; Qt widget/parent/field layout and real gifting are not executed')


def retained_message_status(root):
    text = source('Telegram/SourceFiles/history/history_inner_widget_accessibility.cpp')
    name = function(text, 'QString MessageAccessibilityName(')
    cell = function(text, 'QString MessageSubItemValue(')
    helper_signature = '[[nodiscard]] QString MessageDeliveryAccessibilityText('
    helper = function(text, helper_signature) if helper_signature in text else ''
    name_gate = name[name.index('const auto item ='):name.index('QStringList lines;')]
    status = name[name.index('QStringList statusParts;'):name.index('if (item->Has<HistoryMessageEdited>())')]
    cell_gate = cell[cell.index('const auto data ='):cell.index('switch (item)')]
    delivery = cell.split('case MessageSubItem::Delivery:', 1)[1].split('case MessageSubItem::Edited:', 1)[0]
    cpp(root, 'retained_message_status', r'''
#include <cassert>
#include <iostream>
#include <string>
#include <vector>
struct QString:std::string{using std::string::string;using std::string::operator=;QString(std::string s):std::string(s){}bool isEmpty()const{return empty();}};QString operator""_q(const char16_t *s,size_t n){QString r;for(size_t i=0;i<n;++i)r+=char(s[i]);return r;}struct QStringList:std::vector<QString>{using std::vector<QString>::vector;QString join(const QString &separator)const{QString r;for(const auto &s:*this){if(!r.empty())r+=separator;r+=s;}return r;}};
template<class T>struct not_null{T p;not_null(T v):p(v){}T operator->()const{return p;}};
namespace tr{constexpr int now=0;QString lng_sr_chat_sending(int){return "Sending";}QString lng_sr_chat_failed(int){return "Failed";}QString lng_sr_chat_sent(int){return "Sent";}QString lng_sr_chat_received(int){return "Received";}QString luxury_WatcherMessageDeleted(int){return "Message deleted";}QString luxury_MessageSelfDestructingOpened(int){return "Self-destructing media opened";}}
struct HistoryItem{bool outgoing=false,sending=false,failed=false,deleted=false,ttl=false,mediaUnread=true,service=false;int futureDeadline=0;QString message="Expired photo";bool out()const{return outgoing;}bool isSending()const{return sending;}bool hasFailed()const{return failed;}bool isDeleted()const{return deleted;}bool isBurnt()const{return ttl&&!mediaUnread;}bool isService()const{return service;}struct Text{QString text;};Text notificationText()const{return {message};}};
struct Element{HistoryItem *item;HistoryItem *data()const{return item;}};struct History{};enum class MessageSubItem{Message,Delivery};
''' + helper + r'''
QString RowName(not_null<const Element*> view,not_null<History*> history){''' + name_gate + status + r'''return statusParts.join(u" "_q);}
QString CellValue(not_null<const Element*> view,not_null<History*> history,MessageSubItem item){''' + cell_gate + r'''if(item==MessageSubItem::Delivery){''' + delivery + r'''}return {};}
int main(){History history;HistoryItem i;Element view{&i};for(bool out:{false,true})for(bool sending:{false,true})for(bool failed:{false,true})for(bool deleted:{false,true})for(bool ttl:{false,true})for(bool unread:{false,true}){i.outgoing=out;i.sending=sending;i.failed=failed;i.deleted=deleted;i.ttl=ttl;i.mediaUnread=unread;i.futureDeadline=ttl&&!unread?999:0;auto expected=out?(sending?"Sending":failed?"Failed":"Sent"):"Received";QString text=expected;if(deleted)text+=" Message deleted";if(ttl&&!unread)text+=" Self-destructing media opened";auto row=RowName(&view,&history);auto delivery=CellValue(&view,&history,MessageSubItem::Delivery);assert(row==text&&delivery==text);assert(row.find("Expired")==QString::npos);}i.service=true;i.deleted=true;i.ttl=true;i.mediaUnread=false;assert(RowName(&view,&history)=="Expired photo");assert(CellValue(&view,&history,MessageSubItem::Message)=="Expired photo");assert(CellValue(&view,&history,MessageSubItem::Delivery).empty());i.message="";assert(RowName(&view,&history).empty());std::cout<<"Retained message actual delivery formatter+row/cell/service gates PASS:64 flag cases/128 row-cell checks, future TTL opened not expired, service text unchanged\n";}
''')
    assert name.index('item->isService()') < name.index('MessageDeliveryAccessibilityText(item)')
    assert cell.index('data->isService()') < cell.index('MessageDeliveryAccessibilityText(data)')
    state = source('Telegram/SourceFiles/history/history_item.cpp')
    assert 'return media() && media()->ttlSeconds() && !hasUnreadMediaFlag();' in function(state, 'bool HistoryItem::isBurnt() const')
    transition = function(state, 'void HistoryItem::markMediaAndMentionRead()')
    assert transition.index('armMediaDestroy(base::unixtime::now() + *seconds);') < transition.index('if (isBurnt())')
    for path in ('Telegram/SourceFiles/history/history_inner_widget.cpp', 'Telegram/SourceFiles/history/view/history_view_list_widget.cpp'):
        caller = source(path)
        assert 'MessageAccessibilityName(' in caller and 'MessageSubItemValue(' in caller
    print('Shared legacy/new-list source wiring and TTL meaning retained; native event announcement and real message retention unverified')


from profile_tabs_regressions import check as profile_tabs
from stories_volume_regressions import check as stories_volume
from icon_picker_regressions import check as icon_picker
from bottom_info_regressions import check as bottom_info
from settings_search_regressions import check as settings_search
from collapsible_header_regressions import check as collapsible_header
from tabbed_selector_regressions import check as tabbed_selector
from tabbed_panel_regressions import check as tabbed_panel
from calls_volume_regressions import check as calls_volume
from calls_volume_state_regressions import check as calls_volume_state
from discrete_slider_regressions import check as discrete_slider
from selector_completion_regressions import check as selector_completion
from selector_tabs_regressions import check as selector_tabs
from selector_focus_regressions import check as selector_focus
from selector_switch_focus_regressions import check as selector_switch_focus
from media_geometry_regressions import check as media_geometry
from editor_menu_regressions import check as editor_menu
from main_menu_regressions import check as main_menu
from composer_names_regressions import check as composer_names
from background_preview_names_regressions import check as background_preview_names
from saved_music_scroll_regressions import check as saved_music_scroll
from premium_preview_names_regressions import check as premium_preview_names
from proxy_row_names_regressions import check as proxy_row_names
from itunes_cover_pick_regressions import check as itunes_cover_pick
from context_menu_actions_regressions import check as context_menu_actions
from forward_menu_regressions import check as forward_menu
from own_session_window_regressions import check as own_session_window
from message_history_viewer_regressions import check as message_history_viewer
from history_filters_refresh_regressions import check as history_filters_refresh
from opt_audit_regressions import check as opt_audit
from send_credits_close_regressions import check as send_credits_close
from luxury_worker_nospam_regressions import check as luxury_worker_nospam
from gift_code_close_regressions import check as gift_code_close
from context_menu_flood_regressions import check as context_menu_flood
from history_compose_search_accessibility_regressions import check as compose_search_names
from age_verify_close_regressions import check as age_verify_close
from self_forwards_tagger_names_regressions import check as self_forwards_tagger_names
from unique_close_regressions import check as unique_close
from choose_theme_close_regressions import check as choose_theme_close
from collectible_close_regressions import check as collectible_close
from unique_close_more_regressions import check as unique_close_more
from credits_subscription_close_regressions import check as credits_subscription_close
from topbar_back_close_regressions import check as topbar_back_close
from emoji_stake_close_regressions import check as emoji_stake_close
from filter_link_close_regressions import check as filter_link_close
from giveaway_close_regressions import check as giveaway_close
from restore_windows_close_regressions import check as restore_windows_close
from iv_embed_close_regressions import check as iv_embed_close
from calls_tooltip_close_regressions import check as calls_tooltip_close
from luxury_worker_nospam_regressions import check as luxury_worker_nospam
from emoji_interaction_ghost_regressions import check as emoji_interaction_ghost
from saved_sublist_ghost_regressions import check as saved_sublist_ghost
from ghost_schedule_templates_regressions import check as ghost_schedule_templates
from watcher_ru_gender_regressions import check as watcher_ru_gender
from about_update_row_regressions import check as about_update_row
from shadow_ban_row_regressions import check as shadow_ban_row
from ayugram_issue_ports_regressions import check as ayugram_issue_ports
from filters_publish_toast_regressions import check as filters_publish_toast
from startup_splash_regressions import check as startup_splash
from watcher_clear_box_regressions import check as watcher_clear_box
from reset_settings_confirm_regressions import check as reset_settings_confirm
from import_filters_submit_regressions import check as import_filters_submit
from saved_music_palette_regressions import check as saved_music_palette
from plugin_requirements_regressions import check as plugin_requirements
from edited_mark_language_regressions import check as edited_mark_language
from details_date_template_regressions import check as details_date_template
from select_between_extend_regressions import check as select_between_extend
from plugin_core_regressions import check as plugin_core
from plugin_js_regressions import check as plugin_js
from plugin_ui_regressions import check as plugin_ui
from plugin_tl_regressions import check as plugin_tl
from plugin_host_regressions import check as plugin_host
from plugin_lifecycle_regressions import check as plugin_lifecycle
from plugin_dx_regressions import check as plugin_dx
from threads_core_regressions import check as threads_core

CHECKS = {'filter': filtered_accessibility, 'folders': hidden_all_reorder, 'navigation': navigation_activation, 'ghost': nested_ghost_activation, 'slider': disabled_slider, 'viewer': playback_labels, 'checkbox': checkbox_notification_lifetime, 'player': compact_player_rtl, 'contact': contact_name_order, 'statistics': statistics_checkbox_semantics, 'passport': passport_row_semantics, 'shipping': shipping_option_semantics, 'counters': other_account_counter_reactivity, 'gift': shared_gift_preference, 'message_status': retained_message_status, 'profile_tabs': profile_tabs, 'stories_volume': stories_volume, 'icon_picker': icon_picker, 'bottom_info': bottom_info, 'settings_search': settings_search, 'collapsible_header': collapsible_header, 'tabbed_selector': tabbed_selector, 'tabbed_panel': tabbed_panel, 'calls_volume': calls_volume, 'calls_volume_state': calls_volume_state, 'discrete_slider': discrete_slider, 'selector_completion': selector_completion, 'selector_tabs': selector_tabs, 'selector_focus': selector_focus, 'selector_switch_focus': selector_switch_focus, 'media_geometry': media_geometry, 'editor_menu': editor_menu, 'main_menu': main_menu, 'composer_names': composer_names, 'background_preview_names': background_preview_names, 'saved_music_scroll': saved_music_scroll, 'premium_preview_names': premium_preview_names, 'proxy_row_names': proxy_row_names, 'send_credits_close': send_credits_close, 'gift_code_close': gift_code_close, 'context_menu_flood': context_menu_flood, 'compose_search_names': compose_search_names, 'age_verify_close': age_verify_close, 'self_forwards_tagger_names': self_forwards_tagger_names, 'unique_close': unique_close, 'choose_theme_close': choose_theme_close, 'collectible_close': collectible_close, 'unique_close_more': unique_close_more, 'credits_subscription_close': credits_subscription_close, 'topbar_back_close': topbar_back_close, 'emoji_stake_close': emoji_stake_close, 'filter_link_close': filter_link_close, 'giveaway_close': giveaway_close, 'restore_windows_close': restore_windows_close, 'iv_embed_close': iv_embed_close, 'calls_tooltip_close': calls_tooltip_close, 'luxury_worker_nospam': luxury_worker_nospam, 'emoji_interaction_ghost': emoji_interaction_ghost, 'saved_sublist_ghost': saved_sublist_ghost, 'ghost_schedule_templates': ghost_schedule_templates, 'watcher_ru_gender': watcher_ru_gender, 'about_update_row': about_update_row, 'shadow_ban_row': shadow_ban_row, 'ayugram_issue_ports': ayugram_issue_ports, 'filters_publish_toast': filters_publish_toast, 'startup_splash': startup_splash, 'watcher_clear_box': watcher_clear_box, 'reset_settings_confirm': reset_settings_confirm, 'import_filters_submit': import_filters_submit, 'saved_music_palette': saved_music_palette, 'plugin_requirements': plugin_requirements, 'edited_mark_language': edited_mark_language, 'details_date_template': details_date_template, 'itunes_cover_pick': itunes_cover_pick, 'context_menu_actions': context_menu_actions, 'own_session_window': own_session_window, 'message_history_viewer': message_history_viewer, 'history_filters_refresh': history_filters_refresh, 'opt_audit': opt_audit, 'forward_menu': forward_menu, 'select_between_extend': select_between_extend, 'plugin_core': plugin_core, 'plugin_js': plugin_js, 'plugin_ui': plugin_ui, 'plugin_tl': plugin_tl, 'plugin_host': plugin_host, 'plugin_lifecycle': plugin_lifecycle, 'plugin_dx': plugin_dx, 'threads_core': threads_core}
if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument('--case', choices=tuple(CHECKS))
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='luxury-ui-client-') as directory:
        root = Path(directory)
        for name, check in CHECKS.items():
            if args.case is None or name == args.case:
                check(root)
    print('Client UI extracted checks PASS; current-source native QA remains separate.')
