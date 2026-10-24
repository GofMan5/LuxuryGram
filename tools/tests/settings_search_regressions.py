"""Actual search-result constructor + RpWidget geometry + FlatLabel sizing extracts.

Real bundled rpl; widget/font/text/paint/check-icon doubles, no native raster or FAQ.
"""
from pathlib import Path
import re
from library_regressions import cpp, function, source


def check(root):
    search = source('Telegram/SourceFiles/settings/settings_search.cpp')
    widget = source('Telegram/lib_ui/ui/rp_widget.h')
    label = source('Telegram/lib_ui/ui/widgets/labels.cpp')
    styles = source('Telegram/SourceFiles/settings/settings.style')
    constructor = function(search, '[[nodiscard]] not_null<Ui::SettingsButton*> CreateSearchResultButtonRaw(')
    details = re.search(r'settingsSearchResultDetails: FlatLabel\(settingsNotificationTypeDetails\) \{(.*?)\}', styles, re.S)[1]
    height = re.search(r'maxHeight: (\d+)px;', details)
    cap = int(height[1]) if height else 0
    paddings = [re.search(rf'{name}: SettingsButton\([^)]*\) \{{.*?padding: margins\((\d+)px, (\d+)px, (\d+)px, (\d+)px\);', styles, re.S).groups() for name in ('settingsSearchResult', 'settingsSearchResultNoIcon')]
    methods = '\n'.join(function(widget, signature) for signature in (
        'void moveToLeft(int x, int y, int outerw = 0)',
        'void resizeToWidth(int newWidth, bool internal = false)',
        'void resizeToNaturalWidth(int newWidth)'))
    sizing = '\n'.join(function(label, signature) for signature in (
        'int FlatLabel::resizeGetHeight(int newWidth)',
        'int FlatLabel::countTextWidth(int newWidth) const',
        'int FlatLabel::countTextHeight(int textWidth)'))
    scale = function(source('Telegram/lib_ui/ui/style/style_core_scale.h'), '[[nodiscard]] inline T ConvertScale(T value, int scale)')
    cpp(root, 'settings_search_geometry', r'''
#include <algorithm>
#include <cassert>
#include <cmath>
#include <functional>
#include <iostream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>
template<class...T>constexpr bool false_t(T&&...){return false;}
#include <rpl/variable.h>
#include <rpl/range.h>
#include <rpl/then.h>
#include <rpl/event_stream.h>
#include <rpl/combine.h>
#include <gsl/gsl>
namespace base{double SafeRound(double v){return std::round(v);}namespace assertion{void log(const char*,const char*,int){std::abort();}}}
#undef Assert
#define Assert(x) assert(x)
using QString=std::string;
template<class T>using not_null=T;
struct QSize{int a=0,b=0;QSize(int w=0,int h=0):a(w),b(h){}int width()const{return a;}int height()const{return b;}friend bool operator==(QSize p,QSize q){return p.a==q.a&&p.b==q.b;}};
struct QMargins{int l=0,t=0,r=0,b=0;int left()const{return l;}int top()const{return t;}int right()const{return r;}int bottom()const{return b;}};
namespace style{bool rtl=false;bool RightToLeft(){return rtl;}struct Font{int height=17;};struct TextStyle{Font *font;int lineHeight=0;};struct FlatLabel{TextStyle style;int minWidth=0,maxHeight=0;QMargins margin;};struct SettingsButton{QMargins padding;int height=40;};template<typename T>
''' + scale + r'''}
struct QWidget{QWidget *parent;std::vector<QWidget*> children;int w=0,h=0,px=0,py=0;bool shown=false,transparent=false,updated=false;struct Streams{bool externalWidthWasSet=false;} streams;rpl::lifetime life;rpl::event_stream<QSize> sizes;explicit QWidget(QWidget *p=nullptr):parent(p){if(p)p->children.push_back(this);}virtual ~QWidget(){life.destroy();for(auto p:children)delete p;}QWidget *parentWidget()const{return parent;}int width()const{return w;}int height()const{return h;}QSize size()const{return {w,h};}void resize(QSize s){w=s.width();h=s.height();sizes.fire_copy(s);}void move(int x,int y){px=x;py=y;}void show(){shown=true;}void update(){updated=true;}void setAttribute(int){transparent=true;}rpl::lifetime &lifetime(){return life;}auto sizeValue(){return rpl::single(size()) | rpl::then(sizes.events());}Streams &eventStreams(){return streams;}};
namespace Ui{class RpWidget:public QWidget{public:using Widget=QWidget;using QWidget::QWidget;int natural=-1;virtual QMargins getMargins()const{return {};}int naturalWidth()const{return natural;}virtual int resizeGetHeight(int){return 0;}
''' + methods + r'''};}
namespace Qt{enum{WA_TransparentForMouseEvents};}
namespace st{style::Font font;style::FlatLabel settingsSearchResultDetails{{&font}};}
namespace Ui{class SettingsButton:public RpWidget{public:const style::SettingsButton &st;bool pointer=true;int checks=0;SettingsButton(QWidget *p,const style::SettingsButton &s):RpWidget(p),st(s){}void setPointerCursor(bool value){pointer=value;}int resizeGetHeight(int)override{return st.padding.top()+st.height+st.padding.bottom();}};
class FlatLabel:public RpWidget{public:const style::FlatLabel &_st;struct Text{int w=0,h=17;int maxWidth()const{return w;}int countHeight(int width,bool)const{return w==0?0:h*((w+std::max(1,width)-1)/std::max(1,width));}}_text;int _textWidth=0,_fullTextHeight=0;bool _tryMakeSimilarLines=false,_breakEverywhere=false;FlatLabel(QWidget *p,const QString &s,const style::FlatLabel &st):RpWidget(p),_st(st){_text={int(s.size()),st.style.font->height};natural=_text.maxWidth();resizeToWidth(natural,true);}QMargins getMargins()const override{return _st.margin;}int resizeGetHeight(int)override;int countTextWidth(int)const;int countTextHeight(int);};
''' + sizing + r'''
template<class T,class...A>T *CreateChild(QWidget *p,A&&...a){return new T(p,std::forward<A>(a)...);}}
namespace Builder{enum class SearchEntryCheckIcon{None,Checked,Unchecked};}
struct IconDescriptor{bool icon=false;};struct Obj{Ui::SettingsButton *p;Ui::SettingsButton *release(){return p;}};
Obj CreateButtonWithIcon(QWidget *p,rpl::producer<QString>,const style::SettingsButton &s,IconDescriptor){return {new Ui::SettingsButton(p,s)};}
void SetupCheckIcon(Ui::SettingsButton *b,Builder::SearchEntryCheckIcon,const style::SettingsButton&){++b->checks;}
''' + constructor + '\nconstexpr int cap=' + str(cap) + ';\n' + r'''
int main(){int cases=0;QWidget host;
''' + 'const std::vector<QMargins> pads={' + ','.join('{' + ','.join(p) + '}' for p in paddings) + '};\n' + r'''
for(int scale:{100,125,150,200}){st::font.height=style::ConvertScale(17,scale);st::settingsSearchResultDetails.maxHeight=style::ConvertScale(cap,scale);for(bool rtl:{false,true}){style::rtl=rtl;for(auto pad:pads){style::SettingsButton s{{style::ConvertScale(pad.l,scale),style::ConvertScale(pad.t,scale),style::ConvertScale(pad.r,scale),style::ConvertScale(pad.b,scale)},style::ConvertScale(40,scale)};for(int textWidth:{0,35,220,800})for(auto check:{Builder::SearchEntryCheckIcon::None,Builder::SearchEntryCheckIcon::Checked,Builder::SearchEntryCheckIcon::Unchecked}){auto b=CreateSearchResultButtonRaw(&host,"title",QString(style::ConvertScale(textWidth,scale),'x'),s,{},check);auto d=dynamic_cast<Ui::FlatLabel*>(b->children.back());assert(d&&d->shown&&d->transparent&&!b->pointer);assert(b->checks==(check==Builder::SearchEntryCheckIcon::None?0:1));for(int baseWidth:{324,392,393,280,520,324}){int width=style::ConvertScale(baseWidth,scale);b->resizeToWidth(width);int available=width-s.padding.left()-s.padding.right();assert(d->width()<=available);assert(st::settingsSearchResultDetails.maxHeight>0&&st::settingsSearchResultDetails.maxHeight<2*st::font.height);assert(d->height()<=st::settingsSearchResultDetails.maxHeight);int left=rtl?width-s.padding.left()-d->width():s.padding.left();assert(d->px==left);assert(d->py==s.padding.top()+s.height-d->height());assert(d->py>=s.padding.top()+st::font.height);assert(d->py+d->height()<=b->height()-s.padding.bottom());++cases;}}}}}
std::cout<<"Settings search actual result constructor/RpWidget geometry/FlatLabel sizing + real-rpl PASS:"<<cases<<" narrow/wide/reused sizes,4logicalscales,2paddingstyles,conditionalRTL,3check states andshort/long/empty subtitles; font/text/widgets doubled, no native shape/raster/AT or FAQ actions\n";}
''', extra=[arg for p in ('Telegram/lib_rpl', 'Telegram/lib_base', 'Telegram/ThirdParty/GSL/include') for arg in ('-I', str(Path(__file__).resolve().parents[2] / p))])


if __name__ == '__main__':
    import tempfile
    with tempfile.TemporaryDirectory(prefix='luxury-settings-search-') as directory:
        check(Path(directory))
