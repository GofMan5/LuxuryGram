"""Actual IconPicker header/constructor/paint/hit/key methods, mechanical Qt doubles.

Checks source-backed geometry and input, not QImage raster, shell effects or native AT.
"""
from pathlib import Path
import re
from library_regressions import cpp, function, source


def check(root):
    text = source('Telegram/SourceFiles/luxury/ui/components/icon_picker.cpp')
    metrics = source('Telegram/SourceFiles/luxury/ui/luxury_styles.style')
    values = {name: int(re.search(rf'{name}: (\d+)px;', metrics)[1]) for name in (
        'iconPickerIconSize', 'iconPickerImagePadding', 'iconPickerSelectedPadding', 'iconPickerSelectedRounding')}
    names = text[text.index('constexpr auto kHoverOpacity'):text.index('} // namespace')]
    info = source('Telegram/SourceFiles/info/info.style')
    settings = source('Telegram/SourceFiles/settings/settings.style')
    minimum = int(re.search(r'infoMinimalWidth: (\d+)px;', info)[1])
    desired = int(re.search(r'infoDesiredWidth: (\d+)px;', info)[1])
    margin = re.search(r'settingsButtonNoIcon:.*?padding: margins\((\d+)px, \d+px, (\d+)px,', settings, re.S)
    caller = source('Telegram/SourceFiles/luxury/ui/settings/settings_appearance.cpp')
    assert '.margin = st::settingsButtonNoIcon.padding' in function(caller, 'void BuildAppIcon(')
    signatures = ('IconPicker::IconPicker(QWidget *parent)', 'void IconPicker::drawIcon(',
                  'int IconPicker::cellWidth() const', 'void IconPicker::paintEvent(',
                  'bool IconPicker::applyIcon(', 'void IconPicker::activateIcon(',
                  'void IconPicker::mousePressEvent(', 'void IconPicker::keyPressEvent(',
                  'int IconPicker::indexAt(', 'void IconPicker::setHovered(',
                  'void IconPicker::mouseMoveEvent(', 'void IconPicker::leaveEventHook(',
                  'QAccessible::Role IconPicker::accessibilityRole()',
                  'Qt::FocusPolicy IconPicker::accessibilityFocusPolicy()',
                  'bool IconPicker::accessibilitySelectionList()',
                  'int IconPicker::accessibilityChildCount()',
                  'QString IconPicker::accessibilityChildName(',
                  'QAccessible::State IconPicker::accessibilityChildState(',
                  'QAccessible::Role IconPicker::accessibilityChildRole()',
                  'QRect IconPicker::accessibilityChildRect(',
                  'bool IconPicker::accessibilityChildSupportsActions(',
                  'quintptr IconPicker::accessibilityChildIdentity(',
                  'int IconPicker::accessibilityChildIndexByIdentity(',
                  'void IconPicker::accessibilityChildSetFocus(',
                  'void IconPicker::accessibilityChildActivate(')
    methods = '\n'.join(function(text, s) for s in signatures)
    if 'QRect IconPicker::iconRect(' in text:
        methods += '\n' + function(text, 'QRect IconPicker::iconRect(')
    scale = function(source('Telegram/lib_ui/ui/style/style_core_scale.h'), '[[nodiscard]] inline T ConvertScale(T value, int scale)')
    mirror = function(source('Telegram/lib_ui/ui/style/style_core_direction.h'), 'inline QRect rtlrect(const QRect &r, int outerw)')
    repo = Path(__file__).resolve().parents[2]
    for name in ('ui/effects/animations.h', 'ui/rp_widget.h'):
        header = root / name
        header.parent.mkdir(parents=True, exist_ok=True)
        header.write_text('#pragma once\n', encoding='utf-8')
    cpp(root, 'icon_picker', f'constexpr int minimumLayerWidth={minimum},desiredLayerWidth={desired},leftMargin={margin[1]},rightMargin={margin[2]};\n' + r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cmath>
#include <functional>
#include <iostream>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>
struct QString:std::string{using std::string::string;QString(std::string s):std::string(std::move(s)){}bool isEmpty()const{return empty();}};
QString operator+(QString left,const QString &right){left.append(right);return left;}
#define QStringLiteral(x) QString(x)
namespace std{template<>struct hash<QString>{size_t operator()(const QString &s)const{return hash<string>()(s);}};}
typedef std::uintptr_t quintptr;
template<class T>using QVector=std::vector<T>;
namespace QAccessible{enum Role{List,ListItem};struct State{bool disabled=false,focusable=false,selectable=false,selected=false,focused=false,invisible=false,offscreen=false;};}
namespace Qt{enum FocusPolicy{NoFocus=0,TabFocus=1,StrongFocus=2};enum FocusReason{OtherFocusReason};enum{Key_Left,Key_Right,Key_Up,Key_Down,Key_Enter,Key_Return,Key_Space,Key_Escape,LeftButton};}
struct QPoint{int a=0,b=0;QPoint(int x=0,int y=0):a(x),b(y){}int x()const{return a;}int y()const{return b;}};
struct QMargins{int l,t,r,b;QMargins(int a,int c,int d,int e):l(a),t(c),r(d),b(e){}};
struct QRect{int a=0,b=0,c=0,d=0;QRect()=default;QRect(int x,int y,int w,int h):a(x),b(y),c(w),d(h){}int x()const{return a;}int y()const{return b;}int width()const{return c;}int height()const{return d;}bool isEmpty()const{return c<=0||d<=0;}int left()const{return a;}int right()const{return a+c-1;}int top()const{return b;}int bottom()const{return b+d-1;}QPoint topLeft()const{return {a,b};}QRect marginsAdded(QMargins m)const{return {a-m.l,b-m.t,c+m.l+m.r,d+m.t+m.b};}QRect marginsRemoved(QMargins m)const{return {a+m.l,b+m.t,c-m.l-m.r,d-m.t-m.b};}QRect intersected(QRect q)const{const auto l=std::max(a,q.a),t=std::max(b,q.b),r=std::min(right(),q.right()),bt=std::min(bottom(),q.bottom());return (r<l||bt<t)?QRect():QRect(l,t,r-l+1,bt-t+1);}bool contains(QPoint p)const{return p.x()>=a&&p.x()<a+c&&p.y()>=b&&p.y()<b+d;}friend bool operator==(QRect p,QRect q){return p.a==q.a&&p.b==q.b&&p.c==q.c&&p.d==q.d;}};
struct QPaintEvent{};struct QEvent{};struct QKeyEvent{int k;bool accepted=false;explicit QKeyEvent(int key):k(key){}int key()const{return k;}void accept(){accepted=true;}};
struct QMouseEvent{QPoint point;explicit QMouseEvent(QPoint p):point(p){}QPoint pos()const{return point;}int button()const{return Qt::LeftButton;}void accept(){}};
struct QImage{QString name;};struct QWidget{virtual ~QWidget()=default;};
namespace style{bool rtl=false;bool RightToLeft(){return rtl;}int cur_default=0,cur_pointer=1;
''' + mirror + r'''}
namespace base{double SafeRound(double v){return std::round(v);}}
#define Assert(x) assert(x)
namespace style{template<typename T>
''' + scale + r'''}
namespace rpl{struct Next{std::function<void(int)> f;};template<class F,class L>Next on_next(F f,L&){return {f};}}
namespace crl{template<class T,class F>void on_main(T*,F f){f();}}
namespace Ui{class RpWidget:public QWidget{public:int w=280,h=0,updates=0,repaints=0,baseKeys=0,policy=0,cursor=-1,leaves=0;bool focused=false,tracking=false;std::function<void(int)> widthChanged;explicit RpWidget(QWidget*){}virtual ~RpWidget()=default;virtual void paintEvent(QPaintEvent*){}virtual void mousePressEvent(QMouseEvent*){}virtual void mouseMoveEvent(QMouseEvent*){}virtual void leaveEventHook(QEvent*){++leaves;}virtual void keyPressEvent(QKeyEvent*){++baseKeys;}void setMouseTracking(bool b){tracking=b;}void setCursor(int c){cursor=c;}virtual QAccessible::Role accessibilityRole(){return QAccessible::List;}virtual Qt::FocusPolicy accessibilityFocusPolicy(){return Qt::TabFocus;}virtual bool accessibilitySelectionList()const{return false;}virtual int accessibilityChildCount()const{return -1;}virtual QString accessibilityChildName(int)const{return {}; }virtual QAccessible::State accessibilityChildState(int)const{return {}; }virtual QAccessible::Role accessibilityChildRole()const{return QAccessible::ListItem;}virtual QRect accessibilityChildRect(int)const{return {}; }virtual bool accessibilityChildSupportsActions(int)const{return false;}virtual quintptr accessibilityChildIdentity(int)const{return 0;}virtual int accessibilityChildIndexByIdentity(quintptr)const{return -1;}virtual void accessibilityChildSetFocus(quintptr){}virtual void accessibilityChildActivate(quintptr){}void accessibilityChildSelectionChanged(int){}void accessibilityChildFocused(int){}void setFocusPolicy(Qt::FocusPolicy p){policy=p;}void setAccessibleName(QString){}void setFocus(Qt::FocusReason){focused=true;}bool isEnabled()const{return true;}bool isVisible()const{return true;}QRect rect()const{return {0,0,w,h};}RpWidget *widthValue(){return this;}int &lifetime(){return policy;}void resize(int a,int b){bool changed=w!=a;w=a;h=b;if(changed&&widthChanged)widthChanged(a);}int width()const{return w;}int height()const{return h;}bool hasFocus()const{return focused;}void update(){++updates;}void repaint(){++repaints;}QRect myrtlrect(QRect r)const{return style::rtlrect(r,w);}struct Style *style()const;};
namespace Animations{struct Simple{double value(double v)const{return v;}template<class F>void start(F f,double,double,int,int){f();}};}}
void operator|(Ui::RpWidget *w,rpl::Next n){w->widthChanged=n.f;n.f(w->width());}
struct Draw{QRect rect;QString name;};std::vector<Draw> images;std::vector<QRect> rings,focusRects;std::vector<float> ringOpacity;
struct QPen{QPen(int,int){}};struct QBrush{explicit QBrush(int){}};
struct QPainter{float opacity=1.f;explicit QPainter(QWidget*){}void save(){}void restore(){opacity=1.f;}void setPen(QPen){}void setBrush(QBrush){}void setOpacity(float o){opacity=o;}void drawRoundedRect(int x,int y,int w,int h,int,int){rings.emplace_back(x,y,w,h);ringOpacity.push_back(opacity);}void drawRoundedRect(QRect r,int,int){rings.push_back(r);ringOpacity.push_back(opacity);}void drawImage(QRect r,QImage i){images.push_back({r,i.name});}};
struct Painter:QPainter{using QPainter::QPainter;};struct PainterHighQualityEnabler{explicit PainterHighQualityEnabler(QPainter&){} };
struct QStyleOptionFocusRect{QRect rect;int state=0;};struct QStyle{enum{State_KeyboardFocusChange=1,PE_FrameFocusRect=2};};
namespace Ui{struct Style{void drawPrimitive(int,QStyleOptionFocusRect *o,QPainter*,const QWidget*){focusRects.push_back(o->rect);}};Style *RpWidget::style()const{static Style s;return &s;}}
namespace st{int boxDividerBg=1;
''' + ''.join(f'int {name}={value};\n' for name, value in values.items()) + r'''}
namespace tr{int now=0;QString luxury_AppIconPickerAccessible(int){return "Icon picker";}QString luxury_AppIconHeader(int){return "App icon";}}
namespace anim{int easeOutCubic=0;}
class LuxurySettings{public:QString selected="default";int writes=0;static LuxurySettings &getInstance(){static LuxurySettings s;return s;}const QString &appIcon()const{return selected;}void setAppIcon(QString s){selected=std::move(s);++writes;}};
namespace LuxuryAssets{const QString DEFAULT_ICON="default",ALT_ICON="alt",DISCORD_ICON="discord",SPOTIFY_ICON="spotify",EXTERA_ICON="extera",NOTHING_ICON="nothing",BARD_ICON="bard",YAPLUS_ICON="yaplus",WIN95_ICON="win95",CHIBI_ICON="chibi",CHIBI2_ICON="chibi2",EXTERA2_ICON="extera2";int applies=0;QImage loadPreview(QString s){return {s};}QString currentAppLogoName(){return LuxurySettings::getInstance().appIcon();}void applyAppIcon(){++applies;}}
#include "luxury/ui/components/icon_picker.h"
namespace {
''' + names + '\n}\n' + methods + r'''
class Probe:public IconPicker{public:using IconPicker::IconPicker;using IconPicker::paintEvent;using IconPicker::mousePressEvent;using IconPicker::mouseMoveEvent;using IconPicker::leaveEventHook;using IconPicker::keyPressEvent;};
void paint(Probe &p){images.clear();rings.clear();ringOpacity.clear();focusRects.clear();QPaintEvent e;p.paintEvent(&e);assert(images.size()==icons.size()&&rings.size()==icons.size());}
QRect expected(int i,int w){int cell=w/IconPicker::kColumns;QRect r((i%4)*cell+(cell-st::iconPickerIconSize)/2,(i/4)*cell+st::iconPickerImagePadding,st::iconPickerIconSize,st::iconPickerIconSize);return style::rtlrect(r,w);}
void key(Probe &p,int k){QKeyEvent e(k);p.keyPressEvent(&e);assert(e.accepted);}
int main(){QWidget host;auto &settings=LuxurySettings::getInstance();const auto baseIcon=st::iconPickerIconSize,baseImagePadding=st::iconPickerImagePadding,baseSelectedPadding=st::iconPickerSelectedPadding,baseRounding=st::iconPickerSelectedRounding;
for(double scale:{1.,1.25,1.5,2.}){st::iconPickerIconSize=style::ConvertScale(baseIcon,int(scale*100));st::iconPickerImagePadding=style::ConvertScale(baseImagePadding,int(scale*100));st::iconPickerSelectedPadding=style::ConvertScale(baseSelectedPadding,int(scale*100));st::iconPickerSelectedRounding=style::ConvertScale(baseRounding,int(scale*100));for(bool rtl:{false,true}){style::rtl=rtl;for(int layerWidth:{minimumLayerWidth,desiredLayerWidth,desiredLayerWidth+1}){const auto percent=int(scale*100);const auto width=style::ConvertScale(layerWidth,percent)-style::ConvertScale(leftMargin,percent)-style::ConvertScale(rightMargin,percent);Probe p(&host);p.resize(width,p.height());assert(p.height()==2*(width/4)+st::iconPickerIconSize+2*st::iconPickerImagePadding);paint(p);for(int i=0;i<int(icons.size());++i){assert(images[i].rect==expected(i,width));assert(images[i].name==icons[i]);int inset=st::iconPickerSelectedPadding;auto ring=images[i].rect.marginsAdded(QMargins(inset,inset,inset,inset));assert(rings[i]==ring);assert(ring.left()>=0&&ring.right()<width&&ring.top()>=0&&ring.bottom()<p.height());
settings.selected=icons[(i+1)%icons.size()];int before=settings.writes,apply=LuxuryAssets::applies;auto r=images[i].rect;QMouseEvent hit(QPoint(r.x()+r.width()/2,r.y()+r.height()/2));p.mousePressEvent(&hit);assert(settings.selected==icons[i]&&settings.writes==before+1&&LuxuryAssets::applies==apply+1);p.focused=true;paint(p);assert(focusRects.size()==1&&focusRects[0]==rings[i]);for(int submit:{Qt::Key_Space,Qt::Key_Return,Qt::Key_Enter})key(p,submit);assert(settings.writes==before+1&&LuxuryAssets::applies==apply+1);QMouseEvent outside(QPoint(r.right()+1,r.y()+r.height()/2));p.mousePressEvent(&outside);assert(settings.writes==before+1&&LuxuryAssets::applies==apply+1);}
settings.selected=icons[0];Probe samePicker(&host);samePicker.resize(width,samePicker.height());samePicker.focused=true;key(samePicker,Qt::Key_Right);paint(samePicker);assert(focusRects[0]!=rings[0]);int beforeUpdates=samePicker.updates,beforeRepaints=samePicker.repaints,beforeWrites=settings.writes,beforeApplies=LuxuryAssets::applies;auto selectedRect=images[0].rect;QMouseEvent same(QPoint(selectedRect.x()+selectedRect.width()/2,selectedRect.y()+selectedRect.height()/2));samePicker.mousePressEvent(&same);assert(samePicker.updates>beforeUpdates||samePicker.repaints>beforeRepaints);assert(settings.writes==beforeWrites&&LuxuryAssets::applies==beforeApplies);paint(samePicker);assert(focusRects[0]==rings[0]);}
Probe p(&host);p.focused=true;paint(p);int writes=settings.writes,apply=LuxuryAssets::applies;key(p,Qt::Key_Right);paint(p);int right=rtl?11:1;assert(focusRects[0]==rings[right]);key(p,Qt::Key_Left);paint(p);assert(focusRects[0]==rings[0]);key(p,Qt::Key_Down);paint(p);assert(focusRects[0]==rings[4]);key(p,Qt::Key_Up);paint(p);assert(focusRects[0]==rings[0]);assert(settings.writes==writes&&LuxuryAssets::applies==apply);QKeyEvent unknown(Qt::Key_Escape);p.keyPressEvent(&unknown);assert(!unknown.accepted&&p.baseKeys==1);
assert(p.tracking&&settings.selected==icons[0]);paint(p);auto hr=images[2].rect,sr=images[0].rect;int u=p.updates;QMouseEvent over(QPoint(hr.x()+hr.width()/2,hr.y()+hr.height()/2));p.mouseMoveEvent(&over);assert(p.cursor==style::cur_pointer&&p.updates==u+1);paint(p);assert(ringOpacity[2]==kHoverOpacity&&ringOpacity[1]==0.f&&ringOpacity[0]==1.f);p.mouseMoveEvent(&over);assert(p.updates==u+1);QMouseEvent overSelected(QPoint(sr.x()+sr.width()/2,sr.y()+sr.height()/2));p.mouseMoveEvent(&overSelected);paint(p);assert(ringOpacity[0]==1.f&&ringOpacity[2]==0.f);QMouseEvent gap(QPoint(p.width()*2,p.height()*2));p.mouseMoveEvent(&gap);assert(p.cursor==style::cur_default);paint(p);assert(ringOpacity[2]==0.f&&ringOpacity[0]==1.f);p.mouseMoveEvent(&over);QEvent leave;p.leaveEventHook(&leave);assert(p.cursor==style::cur_default&&p.leaves==1);paint(p);assert(ringOpacity[2]==0.f);assert(settings.writes==writes&&LuxuryAssets::applies==apply);}}
std::cout<<"IconPicker actual header/constructor/draw/paint/mouse/key PASS:12 identities, centred source metrics, RTL, minimum/odd widths,4 scales, focus/selection geometry, hover cursor/frame/leave and fake-only setting effects\n";}
''', extra=['-I', str(root), '-I', str(repo / 'Telegram/SourceFiles')])


if __name__ == '__main__':
    import tempfile
    with tempfile.TemporaryDirectory(prefix='luxury-icon-picker-') as directory:
        check(Path(directory))
