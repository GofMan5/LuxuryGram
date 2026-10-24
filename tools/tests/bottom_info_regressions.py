"""Actual BottomInfo header/Object, paint/textState/sizing/counter layout methods.

Mechanical Qt/style/text/icon spies, no font shaping, native rendering or tooltips.
No profile, actual message, network or file mutation by compiled probe.
"""
from pathlib import Path
import re
from library_regressions import cpp, function, source


def check(root):
    text = source('Telegram/SourceFiles/history/view/history_view_bottom_info.cpp')
    style = source('Telegram/SourceFiles/ui/chat/chat.style')
    names = ('historySendStateSpace', 'historyShortcutStateSpace', 'historyViewsSpace',
             'historyViewsWidth', 'historyPinWidth', 'historySilentWidth',
             'historyEphemeralStateWidth', 'historyViewsTop', 'historyPinTop',
             'historySilentTop', 'historyEphemeralStateTop')
    values = {name: int(re.search(rf'{name}: (-?\d+)px;', style)[1]) for name in names}
    tick = re.search(r'historySendStatePosition: point\((-?\d+)px, (-?\d+)px\);', style)
    signatures = ('TextState BottomInfo::textState(', 'void BottomInfo::paint(',
                  'int BottomInfo::countEffectMaxWidth() const', 'int BottomInfo::countEffectHeight(',
                  'QSize BottomInfo::countOptimalSize()', 'QSize BottomInfo::countCurrentSize(',
                  'void BottomInfo::layoutViewsText()', 'void BottomInfo::layoutRepliesText()',
                  'int BottomInfo::firstLineWidth() const')
    methods = '\n'.join(function(text, s) for s in signatures)
    scale = function(source('Telegram/lib_ui/ui/style/style_core_scale.h'),
                     '[[nodiscard]] inline T ConvertScale(T value, int scale)')
    repo = Path(__file__).resolve().parents[2]
    for name in ('ui/text/text.h', 'base/flags.h'):
        header = root / name
        header.parent.mkdir(parents=True, exist_ok=True)
        header.write_text('#pragma once\n', encoding='utf-8')
    cpp(root, 'bottom_info', r'''
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>
using uint16=uint16_t;using int64=int64_t;using EffectId=int;using TimeId=int;
struct QDateTime{};
struct QString:std::string{using std::string::string;QString(std::string s):std::string(std::move(s)){}bool isEmpty()const{return empty();}};
template<class T>using not_null=T;
struct QPoint{int a=0,b=0;QPoint(int x=0,int y=0):a(x),b(y){}int x()const{return a;}int y()const{return b;}friend QPoint operator+(QPoint p,QPoint q){return {p.a+q.a,p.b+q.b};}};
struct QSize{int a=0,b=0;QSize(int w=0,int h=0):a(w),b(h){}int width()const{return a;}int height()const{return b;}};
struct QRect{int a,b,c,d;QRect(int x,int y,int w,int h):a(x),b(y),c(w),d(h){}bool contains(QPoint p)const{return p.x()>=a&&p.x()<a+c&&p.y()>=b&&p.y()<b+d;}};
struct Draw{std::string name;int x,y,w;};
struct Painter{std::vector<Draw> calls;};
namespace base{template<class E>struct flags{unsigned value=0;flags()=default;flags(E e):value(unsigned(e)){}explicit operator bool()const{return value!=0;}friend flags operator|(flags a,flags b){a.value|=b.value;return a;}friend flags operator&(flags a,flags b){a.value&=b.value;return a;}flags &operator|=(flags b){value|=b.value;return *this;}};double SafeRound(double v){return std::round(v);}}
template<class E>requires std::is_enum_v<E> base::flags<E> operator|(E a,E b){return base::flags<E>(a)|base::flags<E>(b);}
#define Assert(x) assert(x)
namespace style{template<typename T>
''' + scale + r'''}
template<class T>void accumulate_min(T &a,const T &b){if(a>b)a=b;}
namespace Ui{class ReactionFlyAnimation{};struct ReactionFlyAnimationArgs{};class AnimatedIcon{};struct ChatPaintContext;
namespace Text{struct String{std::string name;int w=0;bool isEmpty()const{return w==0;}int maxWidth()const{return w;}void clear(){w=0;}void setText(int,QString s,int){w=std::stoi(s);}void drawLeft(Painter &p,int x,int y,int width,int)const{p.calls.push_back({name,x,y,width});}};}int NameTextOptions(){return 0;}}
namespace Data{struct Reactions{};}
struct Icon{std::string name;void paint(Painter &p,QPoint pos,int)const{p.calls.push_back({name,pos.x(),pos.y(),0});}void paint(Painter &p,int x,int y,int outer)const{paint(p,{x,y},outer);}};
struct Font{int height=17;};
namespace st{Font font;Font *msgDateFont=&font;int msgDateTextStyle=0;int reactionInfoSize=20,reactionInfoBetween=4,reactionInfoSkip=8;
''' + ''.join(f'int {name}={value};\n' for name, value in values.items()) + f'QPoint historySendStatePosition({tick[1]},{tick[2]});\n' + r'''}
struct ChatStyle{Icon historySendingInvertedIcon()const{return {"sending"};}Icon historySendingIcon()const{return {"sending"};}Icon historySentInvertedIcon()const{return {"sent"};}Icon historyReceivedInvertedIcon()const{return {"received"};}Icon historySilentInvertedIcon()const{return {"silent"};}Icon historyEphemeralInvertedIcon()const{return {"ephemeral"};}Icon historyPinInvertedIcon()const{return {"pin"};}Icon historyViewsInvertedIcon()const{return {"views"};}Icon historyRepliesInvertedIcon()const{return {"replies"};}Icon historyViewsSendingInvertedIcon()const{return {"sending_left"};}Icon historyViewsSendingIcon()const{return {"sending_left"};}};
struct MessageStyle{Icon historySentIcon{"sent"},historyReceivedIcon{"received"},historySilentIcon{"silent"},historyEphemeralIcon{"ephemeral"},historyPinIcon{"pin"},historyViewsIcon{"views"},historyRepliesIcon{"replies"};};
namespace Ui{struct ChatPaintContext{const ChatStyle *st;const MessageStyle *message;const MessageStyle *messageStyle()const{return message;}};}
namespace LuxuryFeatures::MessageShot{bool taking=false;bool isTakingShot(){return taking;}}
namespace tr{int now=0;struct Tag{};Tag lt_count_decimal;QString lng_views_tooltip(int,Tag,int n){return "views:"+std::to_string(n);}QString lng_forwards_tooltip(int,Tag,int n){return "forwards:"+std::to_string(n);}}
using tr::lt_count_decimal;
namespace Lang{struct Count{QString string;};Count FormatCountToShort(int n){return {std::to_string(n)};}}
using ClickHandlerPtr=std::shared_ptr<int>;
struct HistoryItem{};
namespace HistoryView{enum class CursorState{None,Date};struct Message{HistoryItem item;const HistoryItem *data()const{return &item;}};struct TextState{bool customTooltip=false;QString customTooltipText;CursorState cursor=CursorState::None;ClickHandlerPtr link;explicit TextState(const HistoryItem*){}};}
#define private public
#include "history/view/history_view_bottom_info.h"
#undef private
namespace HistoryView{
struct BottomInfo::Effect{};
BottomInfo::BottomInfo(not_null<::Data::Reactions*> owner,Data &&data):_reactionsOwner(owner),_data(std::move(data)){}
BottomInfo::~BottomInfo()=default;
ClickHandlerPtr BottomInfo::replayEffectLink(not_null<const Message*>,QPoint)const{return nullptr;}
void BottomInfo::paintEffect(Painter&,QPoint,int,int,int,const PaintContext&)const{}
''' + methods + r'''}
const Draw &call(const Painter &p,std::string name){for(const auto &c:p.calls)if(c.name==name)return c;assert(false);return p.calls[0];}
int main(){using B=HistoryView::BottomInfo;using F=B::Data::Flag;Data::Reactions owner;HistoryView::Message message;ChatStyle chat;MessageStyle msg;Ui::ChatPaintContext context{&chat,&msg};int frames=0;
''' + ''.join(f'const int base_{name}=st::{name};\n' for name in values) + r'''
for(int scale:{100,125,150,200}){
''' + ''.join(f'st::{name}=style::ConvertScale(base_{name},scale);\n' for name in values) + r'''
st::font.height=style::ConvertScale(17,scale);
for(int bits=0;bits<64;++bits)for(int dateBase:{20,80,300})for(int viewsBase:{10,60,150})for(bool replies:{false,true})for(bool inverted:{false,true}){B::Data d;d.views=style::ConvertScale(viewsBase,scale);d.replies=replies?std::optional<int>(style::ConvertScale(60,scale)):std::nullopt;d.forwardsCount=7;if(bits&1)d.flags|=F::OutLayout;if(bits&2)d.flags|=F::Sending;if(bits&4)d.flags|=F::Pinned;if(bits&8)d.flags|=F::Silent;if(bits&16)d.flags|=F::Ephemeral;LuxuryFeatures::MessageShot::taking=bits&32;B b(&owner,std::move(d));b._authorEditedDate={"date",style::ConvertScale(dateBase,scale)};b._views.name="views_text";b._replies.name="replies_text";b.layoutViewsText();b.layoutRepliesText();b.initDimensions();b.resizeGetHeight(b.maxWidth());assert(b.firstLineWidth()==b.width());Painter painter;const QPoint origin(19,23);b.paint(painter,origin,b.width()+38,false,inverted,context);const auto &date=call(painter,"date");int dateSlot=b._authorEditedDate.maxWidth();if(!LuxuryFeatures::MessageShot::taking&&(b._data.flags&(F::OutLayout|F::Sending)))dateSlot+=st::historySendStateSpace;QRect legacyDateHit(b.width()-dateSlot,0,dateSlot,st::font.height);
std::optional<QRect> viewsRect;if(!b._views.isEmpty()){const auto &icon=call(painter,"views");const auto &label=call(painter,"views_text");assert(label.x==icon.x+st::historyViewsWidth);viewsRect.emplace(icon.x-origin.x(),0,label.x+label.w-icon.x,st::font.height);assert(label.x+label.w<=date.x);}
for(int y:{-1,0,st::font.height-1,st::font.height})for(int x=-1;x<=b.width();++x){const QPoint point(x,y);auto state=b.textState(&message,point);bool expected=viewsRect&&viewsRect->contains(point);assert(state.customTooltip==expected);assert((state.cursor==HistoryView::CursorState::Date)==legacyDateHit.contains(point));if(expected)assert(state.customTooltipText=="views:"+std::to_string(*b._data.views)+"\nforwards:7");else assert(state.customTooltipText.empty());}++frames;}
}
std::cout<<"BottomInfo actual paint/textState/counter layout/sizing PASS:"<<frames<<" frames,4 logical scales, badges/right or left sending/message-shot gates, date/view width disparity, replies and inverted paint; unchanged date/status tooltip coverage. Qt/text/icon doubles, no native raster/RTL/tooltip integration\n";}
''', extra=['-I', str(root), '-I', str(repo / 'Telegram/SourceFiles')])

    gif_info_check(root)


def gif_info_check(root):
    gif = source('Telegram/SourceFiles/history/view/media/history_view_gif.cpp')
    message = source('Telegram/SourceFiles/history/view/history_view_message.cpp')
    draw = function(message, 'void Message::drawInfo(')
    hit = function(message, 'TextState Message::bottomInfoTextState(')
    # Exact owning geometry slices, not copied formulas; palette calls are mechanical spies.
    draw_geometry = draw[draw.index('int32 infoRight'):draw.index('\n\tif (type == InfoDisplayType::Image)', draw.index('const auto dateY'))]
    hit_geometry = hit[hit.index('auto infoRight = right;'):hit.index('\n\treturn _bottomInfo.textState(')]
    gif_draw = function(gif, 'void Gif::draw(')
    gif_hit = function(gif, 'TextState Gif::textState(')
    draw_start = gif_draw.index('_parent->drawInfo(')
    draw_call = gif_draw[draw_start:gif_draw.index(');', draw_start)+2]
    hit_start = gif_hit.index('const auto bottomInfoResult = _parent->bottomInfoTextState(')
    hit_call = gif_hit[hit_start:gif_hit.index(');', hit_start)+2]
    metrics = source('Telegram/SourceFiles/ui/chat/chat.style')
    delta = int(re.search(r'msgDateImgDelta: (\d+)px;', metrics)[1])
    def point(name):
        return re.search(rf'{name}: point\((-?\d+)px, (-?\d+)px\);', metrics).groups()
    padding = re.search(r'msgPadding: margins\((\d+)px, (\d+)px, (\d+)px, (\d+)px\);', metrics).groups()
    scale = function(source('Telegram/lib_ui/ui/style/style_core_scale.h'), '[[nodiscard]] inline T ConvertScale(T value, int scale)')
    cpp(root, 'gif_info_geometry', r'''
#include <cassert>
#include <cmath>
#include <iostream>
#include <type_traits>
using int32=int;
struct QPoint{int a=0,b=0;QPoint(int x=0,int y=0):a(x),b(y){}int x()const{return a;}int y()const{return b;}friend bool operator==(QPoint p,QPoint q){return p.a==q.a&&p.b==q.b;}};
struct QSize{int a,b;int width()const{return a;}int height()const{return b;}};
struct QMargins{int l,t,r,b;int left()const{return l;}int top()const{return t;}int right()const{return r;}int bottom()const{return b;}};
namespace base{double SafeRound(double v){return std::round(v);}}
#define Assert(x) assert(x)
namespace style{template<typename T>
''' + scale + r'''}
namespace st{
''' + f'int msgDateImgDelta={delta};\nQPoint msgDateDelta({",".join(point("msgDateDelta"))});\nQPoint msgDateImgPadding({",".join(point("msgDateImgPadding"))});\nQMargins msgPadding{{{",".join(padding)}}};\n' + r'''}
enum class InfoDisplayType{Default,Image,Background};struct Painter{void setPen(int){}};struct ChatStyle{int msgDateImgFg()const{return 0;}int msgServiceFg()const{return 0;}};struct MessageStyle{int msgDateFg=0;};struct PaintContext{};
struct InfoSize{QSize size;QSize currentSize()const{return size;}};
QPoint drawGeometry(int right,int bottom,InfoDisplayType type,QSize dimensions){InfoSize _bottomInfo{dimensions};ChatStyle chat;MessageStyle message;auto st=&chat;auto stm=&message;Painter p;
''' + draw_geometry + r'''
return {dateX,dateY};}
QPoint hitGeometry(int right,int bottom,InfoDisplayType type,QSize dimensions){InfoSize _bottomInfo{dimensions};
''' + hit_geometry + r'''
return {infoLeft,infoTop};}
struct Parent{QSize dimensions;QPoint origin;void drawInfo(Painter&,const PaintContext&,int right,int bottom,int,InfoDisplayType type){origin=drawGeometry(right,bottom,type,dimensions);}QPoint bottomInfoTextState(int right,int bottom,QPoint,InfoDisplayType type){origin=hitGeometry(right,bottom,type,dimensions);return origin;}};
void gifDraw(Parent *_parent,int fullRight,int fullBottom,int paintx,int paintw,bool unwrapped){Painter p;PaintContext context;
''' + draw_call + r'''}
QPoint gifHit(Parent *_parent,int fullRight,int fullBottom,bool unwrapped){QPoint point;
''' + hit_call + r'''
return bottomInfoResult;}
int main(){const auto delta=st::msgDateImgDelta,padx=st::msgDateImgPadding.x(),pady=st::msgDateImgPadding.y();int cases=0;for(int scale:{100,125,150,200}){st::msgDateImgDelta=style::ConvertScale(delta,scale);st::msgDateImgPadding={style::ConvertScale(padx,scale),style::ConvertScale(pady,scale)};for(bool unwrapped:{false,true})for(int bottom:{160,321,640})for(int right:{100,480})for(int width:{40,220}){Parent p{{width,23}};gifDraw(&p,right,bottom,0,right,unwrapped);const auto painted=p.origin;const auto hit=gifHit(&p,right,bottom,unwrapped);assert(hit==painted);++cases;}}
std::cout<<"GIF actual drawInfo/hit call expressions + Message owning placement slices PASS:"<<cases<<" Image/Background cases at4 logical scales; no full Gif/Message TU, raster or media actions\n";}
''')


if __name__ == '__main__':
    import tempfile
    with tempfile.TemporaryDirectory(prefix='luxury-bottom-info-') as directory:
        check(Path(directory))
