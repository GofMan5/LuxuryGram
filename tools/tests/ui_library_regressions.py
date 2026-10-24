"""Compile extracted shared button methods with state/Qt doubles.
Not native Qt focus, paint, accessibility, lifetime or layout integration QA.
"""
from pathlib import Path
import os
import tempfile
from library_regressions import ROOT, CXX, cpp, function, source
from focus_manager_regressions import check as focus_manager


def buttons(root):
    text = source('Telegram/lib_ui/ui/abstract_button.cpp')
    signatures = ('bool AbstractButton::isSubmitEvent(', 'void AbstractButton::keyPressEvent(',
                  'void AbstractButton::keyReleaseEvent(', 'void AbstractButton::focusInEvent(',
                  'void AbstractButton::focusOutEvent(', 'void AbstractButton::clicked(',
                  'bool AbstractButton::setDown(', 'void AbstractButton::setDisabled(',
                  'void AbstractButton::clearState(', 'void AbstractButton::setOver(')
    methods = '\n'.join(function(text, s) for s in signatures)
    cpp(root, 'buttons', r'''
#include <cassert>
#include <iostream>
#include <functional>
#include "base/flags.h"
namespace Qt{using KeyboardModifiers=int;using MouseButton=int;constexpr int LeftButton=1,RightButton=2,Key_Space=32,Key_Return=13,Key_Enter=10,MouseFocusReason=0,TabFocusReason=1;}
template<class T>using not_null=T;
struct QKeyEvent{int code=32;bool repeat=false,accepted=false;int key(){return code;}bool isAutoRepeat(){return repeat;}int modifiers(){return 0;}void accept(){accepted=true;}};
struct QFocusEvent{int why=Qt::TabFocusReason;int reason(){return why;}};
struct RpWidget{void keyPressEvent(QKeyEvent*){}void keyReleaseEvent(QKeyEvent*){}void focusInEvent(QFocusEvent*){}void focusOutEvent(QFocusEvent*){}};
struct AbstractButton;
struct QAccessibleStateChangeEvent;
struct QAccessible{struct State{bool disabled=false,pressed=false;};static void updateAccessibility(QAccessibleStateChangeEvent*);};
struct QAccessibleStateChangeEvent{AbstractButton *button;QAccessible::State state;QAccessibleStateChangeEvent(AbstractButton*b,QAccessible::State s):button(b),state(s){}};
namespace base{struct Weak{const bool *alive;explicit operator bool()const{return *alive;}};Weak make_weak(AbstractButton*);}
struct Integration{static Integration &Instance(){static Integration i;return i;}void unregisterLeaveSubscription(AbstractButton*){}void registerLeaveSubscription(AbstractButton*){}};
struct AbstractButton:RpWidget{
enum class StateFlag{None=0,Over=1,Down=2,Disabled=4};friend constexpr bool is_flag_type(StateFlag){return true;}using State=base::flags<StateFlag>;
enum class StateChangeSource{ByPress,ByUser,ByHover};
State _state=StateFlag::None;bool _acceptBoth=false,_triggerOnPress=false,_keyboardFocus=false,alive=true;
bool destroyOnState=false,disableOnState=false,checkbox=false;int stateCalls=0,pressedEvents=0,cursorCalls=0,updates=0,toggles=0;
int _modifiers=0;std::function<void()> _clickedCallback;struct{int count=0;void fire_copy(int){++count;}}_clicks;
bool isDown()const{return _state&StateFlag::Down;}bool isOver()const{return _state&StateFlag::Over;}bool isDisabled()const{return _state&StateFlag::Disabled;}
struct Changes{bool pressed=false;};void accessibilityStateChanged(Changes){assert(alive);++pressedEvents;}
void update(){assert(alive);++updates;}void updateCursor(){assert(alive);++cursorCalls;}
void onStateChanged(State was,StateChangeSource){assert(alive);++stateCalls;if(checkbox&&!isDisabled()&&(was&StateFlag::Over)&&isOver()&&(was&StateFlag::Down)&&!isDown())++toggles;if(disableOnState){_state|=StateFlag::Disabled;_state&=~State(StateFlag::Down);}if(destroyOnState)alive=false;}
void setOver(bool over,StateChangeSource);
bool isSubmitEvent(not_null<QKeyEvent*>)const;void keyPressEvent(QKeyEvent*);void keyReleaseEvent(QKeyEvent*);void focusInEvent(QFocusEvent*);void focusOutEvent(QFocusEvent*);
void clicked(Qt::KeyboardModifiers,Qt::MouseButton);bool setDown(bool,StateChangeSource,Qt::KeyboardModifiers,Qt::MouseButton);void setDisabled(bool);void clearState();
};
base::Weak base::make_weak(AbstractButton*b){return {&b->alive};}
void QAccessible::updateAccessibility(QAccessibleStateChangeEvent*e){assert(e->button->alive);assert(e->state.disabled);++e->button->pressedEvents;}
'''+methods+r'''
int main(){
for(bool hover:{false,true})for(int key:{32,13,10}){
AbstractButton b;if(hover)b._state|=AbstractButton::StateFlag::Over;QKeyEvent e;e.code=key;b.keyPressEvent(&e);assert(b.isDown());b.keyReleaseEvent(&e);assert(b._clicks.count==1&&!b.isDown());
b.setDisabled(true);b.clicked(0,1);b.keyPressEvent(&e);b.keyReleaseEvent(&e);assert(!b.isDown()&&b._clicks.count==1);b.clearState();assert(b.isDisabled());
}
{AbstractButton b;b._state=AbstractButton::StateFlag::Over;b.setDown(true,AbstractButton::StateChangeSource::ByPress,0,1);b.setDisabled(true);assert(!b.isDown());QKeyEvent e;b.keyReleaseEvent(&e);assert(b._clicks.count==0);b.setDisabled(false);b.keyPressEvent(&e);b.keyReleaseEvent(&e);assert(b._clicks.count==1);}
{AbstractButton b;b._state=AbstractButton::StateFlag::Over;b.checkbox=true;QKeyEvent e;b.keyPressEvent(&e);QFocusEvent f;b.focusOutEvent(&f);b.keyReleaseEvent(&e);assert(!b.isDown()&&b._clicks.count==0&&b.toggles==0);}
{AbstractButton b;b.destroyOnState=true;b._acceptBoth=true;b._triggerOnPress=true;b.setDown(true,AbstractButton::StateChangeSource::ByPress,0,2);assert(!b.alive&&b._clicks.count==0);}
{AbstractButton b;b.destroyOnState=true;b.setOver(true,AbstractButton::StateChangeSource::ByHover);assert(!b.alive);}
{AbstractButton b;b.disableOnState=true;QKeyEvent e;b.keyPressEvent(&e);b.keyReleaseEvent(&e);assert(b.isDisabled()&&b._clicks.count==0);}
{AbstractButton b;int callbacks=0;b._clickedCallback=[&]{++callbacks;b.alive=false;};b.clicked(0,1);assert(callbacks==1&&b._clicks.count==0);}
{AbstractButton b;QFocusEvent f;f.why=0;b.focusInEvent(&f);assert(!b._keyboardFocus);f.why=1;b.focusInEvent(&f);assert(b._keyboardFocus);b.focusOutEvent(&f);assert(!b._keyboardFocus);}
std::cout<<"Shared button extracted state PASS: healthy keys, disabled/direct/hide, held-disable/focus cancel, callback invalidation, keyboard/mouse focus\n";
}
''', ['-I', str(ROOT/'Telegram/lib_base')])
    check = source('Telegram/lib_ui/ui/widgets/checkbox.cpp')
    checkbox_methods = '\n'.join(function(check, s) for s in (
        'void Checkbox::keyPressEvent(', 'void Checkbox::keyReleaseEvent(',
        'void Checkbox::focusOutEvent(', 'void Checkbox::clearState(',
        'void Checkbox::onStateChanged('))
    cpp(root, 'checkbox', r'''#include <cassert>
#include <iostream>
#include <utility>
namespace base{template<class T>T take(T&v){return std::exchange(v,T{});}}
namespace style{constexpr int cur_default=0,cur_pointer=1;}
struct QKeyEvent{bool accepted=false;void accept(){accepted=true;}};struct QFocusEvent{};struct QPixmap{};
struct RippleButton{void keyPressEvent(QKeyEvent*){}void keyReleaseEvent(QKeyEvent*){}void focusOutEvent(QFocusEvent*){}void clearState(){}void onStateChanged(int,int){}};
struct Checkbox: RippleButton{using State=int;using StateChangeSource=int;struct StateFlag{static constexpr int Disabled=4,Over=1,Down=2;};
State current=0;bool disabled=false,submit=true,_keyboardPressed=false;int calls=0;QPixmap _checkCache;
bool isSubmitEvent(QKeyEvent*){return submit;}bool isDisabled(){return disabled;}void handlePress(){++calls;}State state(){return current;}void setCursor(int){}void finishAnimating(){}QPixmap grabCheckCache(){return {};}
void keyPressEvent(QKeyEvent*);void keyReleaseEvent(QKeyEvent*);void focusOutEvent(QFocusEvent*);void clearState();void onStateChanged(State,StateChangeSource);};
'''+checkbox_methods+r'''
int main(){Checkbox c;QKeyEvent e;QFocusEvent f;
c.keyReleaseEvent(&e);assert(c.calls==0&&e.accepted);
c.keyPressEvent(&e);c.keyReleaseEvent(&e);assert(c.calls==1&&!c._keyboardPressed);c.keyReleaseEvent(&e);assert(c.calls==1);
c.disabled=true;c.keyPressEvent(&e);c.keyReleaseEvent(&e);assert(c.calls==1&&!c._keyboardPressed);
c.disabled=false;c.keyPressEvent(&e);c.focusOutEvent(&f);c.keyReleaseEvent(&e);assert(c.calls==1&&!c._keyboardPressed);
c.keyPressEvent(&e);c.clearState();c.keyReleaseEvent(&e);assert(c.calls==1&&!c._keyboardPressed);
c.keyPressEvent(&e);c.disabled=true;c.onStateChanged(0,0);c.disabled=false;c.onStateChanged(4,0);c.keyReleaseEvent(&e);assert(c.calls==1&&!c._keyboardPressed);
c.submit=false;e.accepted=false;c.keyPressEvent(&e);c.keyReleaseEvent(&e);assert(c.calls==1&&!e.accepted);
std::cout<<"Checkbox extracted key lifecycle PASS: healthy, bare/duplicate release, disabled, focus/hide/disable cancellation, other key\n";}
''')
    radio = function(check, 'void Radiobutton::keyPressEvent(')
    cpp(root, 'radio_navigation', r'''#include <cassert>
#include <algorithm>
#include <vector>
#include <iostream>
namespace Qt{constexpr int Key_Up=1,Key_Down=2,Key_Left=3,Key_Right=4,Vertical=5,Horizontal=6,OtherFocusReason=7;}
struct QKeyEvent{int code=2;bool accepted=false;int key(){return code;}void accept(){accepted=true;}void ignore(){}};
template<class T>T not_null(T v){return v;}
namespace ranges{template<class V,class T>auto find(V&v,T t){return std::find(v.begin(),v.end(),t);}}
namespace base{template<class T>struct Weak{T*p;T*get()const{return p;}};template<class T>Weak<T> make_weak(T*p){return {p};}}
struct Checkbox{void keyPressEvent(QKeyEvent*){}};
struct Radiobutton:Checkbox{struct Group{std::vector<Radiobutton*>_buttons;int selected=-1;void setValue(int v){selected=v;}}*_group;int _value=0,pos=0,focus=0;bool disabled=false;bool isDisabled(){return disabled;}int x(){return 0;}int y(){return pos;}void setFocus(int){++focus;}void keyPressEvent(QKeyEvent*);};
'''+radio+r'''
int main(){Radiobutton::Group group;Radiobutton a,b,c,d;group._buttons={&a,&b,&c,&d};int i=0;for(auto p:group._buttons){p->_group=&group;p->_value=i;p->pos=10*i++;}b.disabled=c.disabled=true;QKeyEvent e;a.keyPressEvent(&e);assert(group.selected==3&&d.focus==1);e.code=1;d.keyPressEvent(&e);assert(group.selected==0&&a.focus==1);a.disabled=true;e.code=2;a.keyPressEvent(&e);assert(group.selected==0&&e.accepted);d.disabled=true;a.disabled=false;a.keyPressEvent(&e);assert(group.selected==0);std::cout<<"Radio extracted arrow navigation PASS: disabled current, skipped choices, reverse and bounds\n";}
''')
    # Wiring checks are deliberately source contracts, not rendered behavior.
    accessible=source('Telegram/lib_ui/ui/accessible/ui_accessible_widget.cpp')
    assert 'result.disabled = result.disabled || button->isDisabled()' in accessible
    for class_name in ('LinkButton','FlatButton','RoundButton','IconButton','CrossButton','SettingsButton'):
        assert 'paintKeyboardFocus(' in function(source('Telegram/lib_ui/ui/widgets/buttons.cpp'),f'void {class_name}::paintEvent(')
    assert '(_active ? _st.textFgActive : _st.textFg)->c' in function(source('Telegram/lib_ui/ui/widgets/side_bar_button.cpp'),'void SideBarButton::paintEvent(')
    round_paint=function(source('Telegram/lib_ui/ui/widgets/buttons.cpp'),'void RoundButton::paintEvent(')
    assert '_textFgOverride.value_or' in round_paint and '_fullRadius ? std::nullopt : _cornerRadii' in round_paint
    focus=function(source('Telegram/lib_ui/ui/abstract_button.cpp'),'void AbstractButton::paintKeyboardFocus(')
    assert 'corner - st::buttonFocusInset' in focus and 'radius - st::buttonFocusInset' in focus
    assert 'p.drawPath(ComplexRoundedRectPath(' in focus
    for path, class_name in (
        ('ui/controls/emoji_button.cpp', 'EmojiButton'),
        ('ui/controls/send_as_button.cpp', 'SendAsButton'),
        ('media/player/media_player_button.cpp', 'PlayButton'),
        ('media/player/media_player_button.cpp', 'SpeedButton')):
        assert 'paintKeyboardFocus(' in function(source('Telegram/SourceFiles/'+path), f'void {class_name}::paintEvent(')
    send = function(source('Telegram/SourceFiles/ui/controls/send_button.cpp'), 'void SendButton::paintEvent(')
    assert send.count('paintFocus(p);') == 3, 'send focus missing from normal/animated paint path'
    cross = function(source('Telegram/lib_ui/ui/widgets/buttons.cpp'), 'void CrossButton::paintEvent(')
    assert 'paintKeyboardFocus(p);\n\t\t\treturn;' in cross
    print('Button/composer/playback focus/accessible-disabled SOURCE contracts PASS (not native render)')


def cache_and_layout(root):
    logo=source('Telegram/SourceFiles/luxury/ui/settings/settings_main.cpp')
    start=logo.index('if (cache->source != image.cacheKey()')
    block=function(logo[start:], 'if (cache->source')
    cpp(root, 'logo_cache', r'''#include <cassert>
#include <iostream>
namespace Qt{constexpr int KeepAspectRatio=1,SmoothTransformation=1;}
struct Image{int key=1;static inline int scales=0;int cacheKey(){return key;}Image scaled(int,int,int,int){++scales;return *this;}};
struct Cache{long long source=0;int ratio=0,size=0;Image image;};
void paint(Cache *cache,Image image,int ratio,int size){'''+block+r'''}
int main(){Cache c;Image i;for(int n=0;n!=100;n++)paint(&c,i,1,80);assert(Image::scales==1);paint(&c,i,2,80);assert(Image::scales==2);paint(&c,i,2,100);assert(Image::scales==3);i.key=2;paint(&c,i,2,100);assert(Image::scales==4);std::cout<<"Logo cache extracted PASS: 100 unchanged paints -> 1 scale; source/DPR/logical-size invalidation\n";}
''')
    row=source('Telegram/SourceFiles/settings/detailed_settings_button.cpp')
    assert '_style.button.style.font->height' in function(row, 'int DetailedSettingsButton::titleHeight()')
    assert 'accessibilityDescriptionChanged();' in row
    assert 'return _description.toString();' in row
    assert 'if (_layoutScheduled)' in row and '_layoutScheduled = false;' in row
    layout=function(row, 'void DetailedSettingsButton::refreshLayout()')
    cpp(root, 'layout_queue', r'''#include <cassert>
#include <functional>
#include <vector>
#include <iostream>
namespace Ui{std::vector<std::function<void()>>queue;void PostponeCall(std::function<void()> f){queue.push_back(std::move(f));}}
namespace crl{template<class T,class F>auto guard(T*,F f){return f;}}
struct DetailedSettingsButton{bool _layoutScheduled=false;int resizes=0,updates=0;int widthNoMargins(){return 300;}void resizeToWidth(int){++resizes;}void update(){++updates;}void refreshLayout();};
'''+layout+r'''
int main(){DetailedSettingsButton b;for(int i=0;i<100;++i)b.refreshLayout();assert(Ui::queue.size()==1&&b.resizes==0);auto f=std::move(Ui::queue.front());Ui::queue.clear();f();assert(b.resizes==1&&b.updates==1&&!b._layoutScheduled);b.refreshLayout();assert(Ui::queue.size()==1);std::cout<<"Detailed row extracted coalescing PASS: 100 requests -> 1 scheduled layout; reusable after flush (guard/Qt doubles)\n";}
''')
    logo_source=source('Telegram/SourceFiles/luxury/ui/luxury_logo.cpp')
    condition_start=logo_source.index('if (LAST_LOADED_NAME != iconName')
    condition=logo_source[condition_start:logo_source.index('{',condition_start)]
    cpp(root, 'logo_source', r'''#include <cassert>
#include <iostream>
#include <string>
std::string LAST_LOADED_NAME;int LAST_LOADED_RATIO=0;
bool reload(std::string iconName,int ratio){'''+condition+r'''{LAST_LOADED_NAME=iconName;LAST_LOADED_RATIO=ratio;return true;}return false;}
int main(){assert(reload("default",1));assert(!reload("default",1));assert(reload("default",2));assert(reload("other",2));assert(!reload("other",2));std::cout<<"Logo source extracted invalidation PASS: icon and display ratio controls\n";}
''')
    assert 'iconSize * style::DevicePixelRatio(),' in function(logo_source, 'QImage CreateImage(')
    assert 'return tr::luxury_LuxuryPreferences();' in logo
    palette=source('Telegram/lib_ui/ui/colors.palette')
    import re
    fg=re.search(r'windowSubTextFg: #(\w{6});',palette).group(1)
    def lum(value):
        values=[int(value[i:i+2],16)/255 for i in (0,2,4)]
        return sum(w*(v/12.92 if v<=.04045 else ((v+.055)/1.055)**2.4) for w,v in zip((.2126,.7152,.0722),values))
    contrast=1.05/(lum(fg)+.05)
    assert contrast >= 4.5
    print(f'Detailed row SOURCE contracts PASS; default secondary text/white calculated contrast {contrast:.2f}:1 (not every theme)')


def row_geometry(root):
    row=source('Telegram/SourceFiles/settings/detailed_settings_button.cpp')
    signatures=('int DetailedSettingsButton::titleTop(', 'int DetailedSettingsButton::titleHeight(',
                'int DetailedSettingsButton::descriptionTopValue(', 'int DetailedSettingsButton::firstDescriptionLineBottom(',
                'int DetailedSettingsButton::textAvailableWidth(', 'QRect DetailedSettingsButton::toggleRect(',
                'QRect DetailedSettingsButton::iconAreaRect(', 'QRect DetailedSettingsButton::iconRect(',
                'QRect DetailedSettingsButton::iconForegroundRect(', 'int DetailedSettingsButton::iconSize(')
    methods='\n'.join(function(row,s) for s in signatures)
    direction=function(source('Telegram/lib_ui/ui/style/style_core_direction.h'),'inline QRect rtlrect(const QRect &r, int outerw)')
    # Metrics below are explicit geometry controls, not generated styles/Qt font rendering.
    cpp(root,'row_geometry',r'''#include <cassert>
#include <algorithm>
#include <iostream>
struct QRect{int X=0,Y=0,W=0,H=0;QRect()=default;QRect(int x,int y,int w,int h):X(x),Y(y),W(w),H(h){}int x()const{return X;}int y()const{return Y;}int width()const{return W;}int height()const{return H;}};
namespace style{bool rtl=false;bool RightToLeft(){return rtl;}
'''+direction+r'''}
struct Font{int height=17;};struct TextStyle{Font*font;int lineHeight=0;};struct Margins{int l=60,t=10,r=22,b=8;int left()const{return l;}int top()const{return t;}int right()const{return r;}};
struct Size{int w=34,h=20;int width()const{return w;}int height()const{return h;}};struct Toggle{Size size;Size getSize()const{return size;}};
struct DetailedSettingsButton{struct{struct{TextStyle style;Margins padding;int toggleSkip=22,iconLeft=17;}button;struct{TextStyle style;}description;int descriptionTopSkip=4,iconPadding=6,iconForegroundPadding=3;} _style;
Toggle toggle;Toggle*_toggle=&toggle;int _descriptionTop=0,outer=300;int width()const{return outer;}QRect myrtlrect(QRect r)const{return style::rtlrect(r,outer);}
int titleTop()const;int titleHeight()const;int descriptionTopValue()const;int firstDescriptionLineBottom()const;int textAvailableWidth(int)const;QRect toggleRect()const;QRect iconAreaRect()const;QRect iconRect()const;QRect iconForegroundRect(QRect)const;int iconSize()const;};
'''+methods+r'''
int main(){
for(int scale:{1,2,3})for(int outer:{240,300,420})for(int lines:{1,3}){
Font font{17*scale};DetailedSettingsButton b;b.outer=outer*scale;b._style.button.style.font=b._style.description.style.font=&font;b._style.button.padding={60*scale,10*scale,22*scale,8*scale};b._style.button.toggleSkip=22*scale;b._style.button.iconLeft=17*scale;b._style.descriptionTopSkip=4*scale;b._style.iconPadding=6*scale;b._style.iconForegroundPadding=3*scale;b.toggle.size={34*scale,20*scale};
assert(b.descriptionTopValue()==31*scale&&b.titleHeight()==17*scale);b._descriptionTop=b.descriptionTopValue();
const auto t=b.toggleRect();const auto textEnd=b._style.button.padding.left()+b.textAvailableWidth(b.outer);assert(textEnd<=t.x()-22*scale&&t.y()>=0);
style::rtl=false;const auto icon=b.iconRect();assert(icon.x()+icon.width()<=b._style.button.padding.left());assert(icon.y()+icon.height()<=b._descriptionTop+lines*font.height+10*scale);
style::rtl=true;const auto mirrored=b.iconRect();assert(mirrored.x()==b.outer-icon.x()-icon.width()&&mirrored.width()==icon.width());const auto back=style::rtlrect(mirrored,b.outer);assert(back.x()==icon.x());
const auto foreground=b.iconForegroundRect(mirrored);assert(foreground.x()>=mirrored.x()&&foreground.x()+foreground.width()<=mirrored.x()+mirrored.width());
b._style.button.style.lineHeight=22*scale;assert(b.titleHeight()==22*scale);
}
DetailedSettingsButton narrow;assert(narrow.textAvailableWidth(1)==1);
std::cout<<"Detailed row extracted geometry PASS: 1x/2x/3x metrics, 240/300/420 widths, multiline envelope, RTL involution, icon/text/toggle separation, line-height override (not native layout)\n";
}
''')


def ttl_and_snapshot(root):
    text=source('Telegram/SourceFiles/history/view/controls/history_view_voice_record_bar.cpp')
    ctor=function(text,'TTLButton::TTLButton(')
    start=ctor.index('_active = !_active;')
    callback=ctor[start:ctor.index('\n\t});',start)]
    assert 'setDisabled(' not in ctor and 'isDisabled(' not in ctor
    assert 'active()' in function(text,'bool VoiceRecordBar::peekTTLState(')
    assert '->reset();' in function(text,'bool VoiceRecordBar::takeTTLState(')
    assert 'void clearState() override;' not in text[text.index('class TTLButton'):text.index('TTLButton::TTLButton(')]
    cpp(root,'ttl',r'''#include <cassert>
#include <iostream>
#include <utility>
namespace st{constexpr int universalDuration=150;}
struct TTLButton;
namespace base{struct Weak{bool*alive;explicit operator bool()const{return *alive;}};Weak make_weak(TTLButton*);}
struct RippleButton{int clears=0;void clearState(){++clears;}};struct QWidget{void update(){}};namespace Ui{using RippleButton=::RippleButton;struct RpWidget{bool visible=true;void hide(){visible=false;}};}
struct TTLButton:Ui::RippleButton,QWidget,Ui::RpWidget{bool _active=false,alive=true,disabled=false;int events=0;struct{int starts=0,stops=0;template<class F>void start(F,int,int,int){++starts;}void stop(){++stops;}}_activeAnimation;struct Changes{bool checked=false;};void accessibilityStateChanged(Changes){++events;}void reset();void toggle(){'''+callback+r'''}};
base::Weak base::make_weak(TTLButton*b){return {&b->alive};}
'''+function(text,'void TTLButton::reset()')+r'''
int main(){TTLButton b;assert(!b._active&&!b.disabled);b.toggle();assert(b._active&&!b.disabled&&b.events==1);b.toggle();assert(!b._active&&b.events==2);b.toggle();b.Ui::RippleButton::clearState();assert(b._active);b.reset();assert(!b._active&&!b.visible&&b.events==4&&b._activeAnimation.stops==1);b.visible=true;b.toggle();assert(b._active&&!b.disabled);std::cout<<"View-once extracted state PASS: off remains available, toggles both ways, press cancel preserves choice, reset clears/hides, reusable (not recording runtime)\n";}
''')
    send=source('Telegram/SourceFiles/ui/controls/send_button.cpp')
    grab=function(send,'QPixmap SendButton::grabContent(')
    focus=function(send,'void SendButton::paintFocus(')
    assert 'std::exchange(_grabbingContent, true)' in grab and '_grabbingContent = wasGrabbing;' in grab
    assert focus.index('if (_grabbingContent)') < focus.index('paintKeyboardFocus(')
    start=focus.index('if (_grabbingContent)')
    condition=focus[start:focus.index('{',start)]
    cpp(root,'focus_capture',r'''#include <cassert>
#include <iostream>
#include <utility>
struct Button{bool _grabbingContent=false,focused=true;int rings=0;void paintFocus(){'''+condition+r'''{return;}if(focused)++rings;}};
int main(){Button b;b.paintFocus();assert(b.rings==1);const auto was=std::exchange(b._grabbingContent,true);b.paintFocus();assert(b.rings==1);b._grabbingContent=was;b.focused=false;b.paintFocus();assert(b.rings==1);b.focused=true;b.paintFocus();assert(b.rings==2);std::cout<<"Focus capture extracted gate/source wiring PASS: snapshots exclude focus, on-screen focus remains dynamic (not pixel render)\n";}
''')


def webview_history(root):
    text=source('Telegram/lib_webview/webview/platform/win/webview_windows_edge_chromium.cpp')
    start=text.index('void Handler::updateHistoryStates()')
    condition_start=text.index('if (',start)
    condition=text[condition_start:text.index('{',condition_start)]
    cpp(root,'webview_history',r'''#include <cassert>
#include <iostream>
struct Handler{bool _destroying=false;bool _webview=true;int queries=0;void updateHistoryStates(){'''+condition+r'''{return;}++queries;}};
int main(){Handler h;h.updateHistoryStates();assert(h.queries==1);h._destroying=true;h.updateHistoryStates();assert(h.queries==1);h._destroying=false;h._webview=false;h.updateHistoryStates();assert(h.queries==1);std::cout<<"Webview history extracted lifetime gate PASS: healthy, controller closing, missing webview (not WebView2 runtime)\n";}
''')


def write_after_lock(root):
    text=source('Telegram/lib_storage/storage/storage_encrypted_file.cpp')
    body=function(text,'File::Result File::attemptOpenForWrite(')
    cpp(root,'write_after_lock',r'''#include <cassert>
#include <iostream>
struct EncryptionKey{};namespace QIODevice{enum Mode{ReadWrite,WriteOnly};}
struct File{enum class Result{Success,Failed,LockFailed};struct{int resizes=0;bool resizeOk=true;bool resize(int size){assert(size==0);++resizes;return resizeOk;}}_data;
struct{bool ok=true;template<class T>bool lock(T&,QIODevice::Mode mode){assert(mode==QIODevice::ReadWrite);return ok;}}_lock;
int headers=0;bool headerOk=true;bool writeHeader(const EncryptionKey&){++headers;return headerOk;}Result attemptOpenForWrite(const EncryptionKey&);};
'''+body+r'''
int main(){EncryptionKey k;File f;f._lock.ok=false;assert(f.attemptOpenForWrite(k)==File::Result::LockFailed&&f._data.resizes==0&&f.headers==0);f._lock.ok=true;f._data.resizeOk=false;assert(f.attemptOpenForWrite(k)==File::Result::Failed&&f.headers==0);f._data.resizeOk=true;f.headerOk=false;assert(f.attemptOpenForWrite(k)==File::Result::Failed&&f.headers==1);f.headerOk=true;assert(f.attemptOpenForWrite(k)==File::Result::Success&&f.headers==2);std::cout<<"Cache write extracted PASS: lock before truncation; resize failure stops header; healthy write\n";}
''')


if __name__=='__main__':
    os.environ['PATH']=str(Path(CXX).parent)+os.pathsep+os.environ.get('PATH','')
    with tempfile.TemporaryDirectory(prefix='luxury-ui-libs-') as temp:
        root=Path(temp)
        focus_manager(root)
        buttons(root)
        cache_and_layout(root)
        row_geometry(root)
        ttl_and_snapshot(root)
        webview_history(root)
        write_after_lock(root)
        marker=source('Telegram/lib_storage/storage/storage_encrypted_file.cpp')
        start=marker.index('if (writePlain(magic.subspan(')
        condition=marker[start:marker.index('{',start)]
        cpp(root,'header_marker',r'''#include <cassert>
#include <iostream>
namespace base{struct FileLock{static constexpr int kSkipBytes=4;};}
struct Span{Span subspan(int,int){return {};}}magic;
int written=0;int writePlain(Span){return written;}
bool marker(){'''+condition+r'''{return false;}return true;}
int main(){for(int n:{-1,0,1,2,3}){written=n;assert(!marker());}written=4;assert(marker());std::cout<<"Cache marker extracted predicate PASS: error/zero/short writes fail, complete marker succeeds\n";}
''')
    print('UI/library focused regressions PASS; NOT native Qt build/visual/accessibility/platform QA')
