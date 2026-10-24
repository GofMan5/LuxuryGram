"""Execute actual Luxury header layout subscription + FlatLabel/RpWidget methods.

Real rpl, mechanical font/text/widgets. Not full AddInnerToggle/backend or raster.
"""
from pathlib import Path
import re
from library_regressions import cpp, function, source


def check(root, height_only=False):
    text = source('Telegram/SourceFiles/luxury/ui/settings/settings_luxury_utils.cpp')
    arrow_start = text.index('\tconst auto arrow =', text.index('not_null<Ui::RpWidget*> AddInnerToggle('))
    start = text.index('\n\t}', arrow_start) + len('\n\t}')
    end = text.index('\n\twrap->toggledValue(', start)
    layout = text[start:end]
    label_style = re.search(r'\n\s*st::(\w+)\);', text[text.index('\tconst auto label =', text.index('not_null<Ui::RpWidget*> AddInnerToggle(')):start])[1]
    styles = source('Telegram/SourceFiles/luxury/ui/luxury_styles.style')
    block = re.search(rf'{label_style}: FlatLabel\([^)]*\) \{{(.*?)\}}', styles, re.S)
    max_height = re.search(r'maxHeight: (\d+)px;', block[1]) if block else None
    min_width = re.search(r'minWidth: (\d+)px;', block[1]) if block else None
    cap = int(max_height[1]) if max_height else 0
    minimum = int(min_width[1]) if min_width else 256
    widget = source('Telegram/lib_ui/ui/rp_widget.h')
    methods = '\n'.join(function(widget, sig) for sig in (
        'void moveToLeft(int x, int y, int outerw = 0)',
        'void resizeToWidth(int newWidth, bool internal = false)'))
    labels = source('Telegram/lib_ui/ui/widgets/labels.cpp')
    sizing = '\n'.join(function(labels, sig) for sig in (
        'int FlatLabel::resizeGetHeight(int newWidth)',
        'int FlatLabel::countTextWidth(int newWidth) const',
        'int FlatLabel::countTextHeight(int textWidth)'))
    scale = function(source('Telegram/lib_ui/ui/style/style_core_scale.h'), '[[nodiscard]] inline T ConvertScale(T value, int scale)')
    cpp(root, 'collapsible_header', r'''
#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <type_traits>
#include <utility>
#include <vector>
template<class...T>constexpr bool false_t(T&&...){return false;}
#include <rpl/variable.h>
#include <rpl/event_stream.h>
#include <rpl/combine.h>
#include <gsl/gsl>
using rpl::combine;
namespace base{double SafeRound(double v){return std::round(v);}namespace assertion{void log(const char*,const char*,int){std::abort();}}}
#undef Assert
#define Assert(x) assert(x)
struct QSize{int a=0,b=0;QSize(int x=0,int y=0):a(x),b(y){}int width()const{return a;}int height()const{return b;}friend bool operator==(QSize p,QSize q){return p.a==q.a&&p.b==q.b;}};
struct QMargins{int left()const{return 0;}int top()const{return 0;}int right()const{return 0;}int bottom()const{return 0;}};
namespace style{bool RightToLeft(){return false;}struct Font{int height=18;};struct TextStyle{Font *font;int lineHeight=22;};struct FlatLabel{TextStyle style;int minWidth=256,maxHeight=0;};struct Padding{int l;int left()const{return l;}};struct SettingsButton{Padding padding;};template<class T>
''' + scale + r'''}
namespace Ui{class RpWidget{public:using Widget=RpWidget;RpWidget *parent=nullptr;int w=0,h=0,x=0,y=0,natural=0;struct Streams{bool externalWidthWasSet=false;} streams;rpl::lifetime life;rpl::event_stream<QSize> sizes;rpl::event_stream<int> naturals;explicit RpWidget(RpWidget *p=nullptr):parent(p){}RpWidget *parentWidget()const{return parent;}int width()const{return w;}int height()const{return h;}QSize size()const{return {w,h};}QMargins getMargins()const{return {};}Streams &eventStreams(){return streams;}rpl::lifetime &lifetime(){return life;}void update(){}void move(int a,int b){x=a;y=b;}void resize(QSize s){w=s.width();h=s.height();sizes.fire_copy(s);}virtual int resizeGetHeight(int){return h;}int naturalWidth()const{return natural;}auto sizeValue(){return sizes.events_starting_with(size());}auto naturalWidthValue(){return naturals.events_starting_with_copy(natural);}
''' + methods + r'''};
class FlatLabel:public RpWidget{public:const style::FlatLabel &_st;struct Text{int w=0,line=22,min=0,lines=1;int maxWidth()const{return w;}int countHeight(int width,bool)const{width=std::max(1,std::max(width,min));return line*(lines+(w+width-1)/width-1);}}_text;int _textWidth=0,_fullTextHeight=0;bool _tryMakeSimilarLines=false,_breakEverywhere=false;FlatLabel(RpWidget *p,const style::FlatLabel &st,int natural):RpWidget(p),_st(st){_text={natural,std::max(st.style.lineHeight,st.style.font->height),st.minWidth?st.minWidth:8388607};this->natural=natural;}int resizeGetHeight(int)override;int countTextWidth(int)const;int countTextHeight(int);void setTextWidth(int value){_text.w=value;natural=value;naturals.fire_copy(value);resizeToWidth(width(),true);}};
''' + sizing + r'''}
void setup(Ui::RpWidget *button,Ui::RpWidget *toggleButton,Ui::FlatLabel *label,Ui::RpWidget *arrow,const style::SettingsButton &st){
''' + layout + r'''
}
int main(){int cases=0;style::Font font;
''' + f'constexpr int cap={cap},minimum={minimum};constexpr bool heightOnly={str(height_only).lower()};\n' + r'''
for(int scale:{100,125,150,200}){font.height=style::ConvertScale(18,scale);style::FlatLabel labelStyle{{&font,style::ConvertScale(22,scale)},style::ConvertScale(minimum,scale),style::ConvertScale(cap,scale)};style::SettingsButton st{{style::ConvertScale(22,scale)}};for(int width:{280,324,392,393,520}){Ui::RpWidget button,toggle(&button),arrow(&button);button.resize({style::ConvertScale(width,scale),style::ConvertScale(10,scale)+style::ConvertScale(20,scale)+style::ConvertScale(8,scale)});toggle.resize({style::ConvertScale(70,scale),button.height()});arrow.resize({style::ConvertScale(8,scale),style::ConvertScale(8,scale)});Ui::FlatLabel label(&button,labelStyle,style::ConvertScale(60,scale));setup(&button,&toggle,&label,&arrow,st);for(int natural:{60,220,800,35,100,60}){label.setTextWidth(style::ConvertScale(natural,scale));assert(label.height()<=button.height());assert(label.y==(button.height()-label.height())/2);int right=button.width()-toggle.width();if(!heightOnly)assert(arrow.x==std::min(st.padding.left()+label.naturalWidth(),right-arrow.width()));assert(label.width()<=right-st.padding.left()-arrow.width());++cases;}label._text.lines=3;label.setTextWidth(style::ConvertScale(60,scale));assert(label.height()<=style::ConvertScale(22,scale));assert(label.y==(button.height()-label.height())/2);button.resize({style::ConvertScale(width+35,scale),button.height()});assert(label.width()==button.width()-toggle.width()-st.padding.left()-arrow.width());assert(arrow.x==std::min(st.padding.left()+label.naturalWidth(),button.width()-toggle.width()-arrow.width()));++cases;button.life.destroy();auto old=arrow.x;label.setTextWidth(style::ConvertScale(140,scale));assert(arrow.x==old);}}
std::cout<<"Luxury actual header subscription + real-rpl/FlatLabel sizing PASS:"<<cases<<" 4logicalscales/5rowwidths/6reactivenaturalwidths plusmultilinecap/reactiveparentresize/disposedlifetime; mechanicalglyph/text/rowdoubles, nofullhelper/backend/RTL/raster/AT\n";}
''', extra=[arg for p in ('Telegram/lib_rpl', 'Telegram/lib_base', 'Telegram/ThirdParty/GSL/include') for arg in ('-I', str(Path(__file__).resolve().parents[2] / p))])


if __name__ == '__main__':
    import argparse
    import tempfile
    parser = argparse.ArgumentParser()
    parser.add_argument('--height-only', action='store_true')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='luxury-collapsible-header-') as directory:
        check(Path(directory), args.height_only)
