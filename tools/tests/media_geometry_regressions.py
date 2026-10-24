"""Actual shared-media section sizing and provider preload estimates.
Qt/style/layout/mosaic doubles, no native raster, app build, account or download.
"""
from pathlib import Path
import re
from library_regressions import cpp, function, source


def check(root, case=None):
    section = source('Telegram/SourceFiles/info/media/info_media_list_section.cpp')
    common = source('Telegram/SourceFiles/info/media/info_media_common.cpp')
    stories = source('Telegram/SourceFiles/info/stories/info_stories_provider.cpp')
    default = source('Telegram/SourceFiles/layout/abstract_layout_item.cpp')
    style = source('Telegram/SourceFiles/info/info.style')
    values = {n: int(re.search(n + r': (\d+)px;', style).group(1))
              for n in ('infoMediaSkip', 'infoMediaLeft', 'infoMediaMinGridSize')}
    header_x = int(re.search(r'infoMediaHeaderPosition: point\((\d+)px,', style).group(1))
    overview_style = source('Telegram/SourceFiles/overview/overview.style')
    max_width = int(re.search(r'overviewFileLayout:[^}]*maxWidth: (\d+)px;', overview_style).group(1))
    song_right = int(re.search(r'songPadding: margins\(\d+px, \d+px, (\d+)px,', overview_style).group(1))
    import struct
    icon_width = struct.unpack('>I', (Path(__file__).resolve().parents[2] /
        'Telegram/Resources/icons/stickers_reorder.png').read_bytes()[16:20])[0]
    includes = r'''
#include <algorithm>
#include <cassert>
#include <iostream>
#include <vector>
using int32=int;template<class T>using not_null=T;
#define Unexpected(x) assert(false)
struct QPoint{int a=0,b=0;int x()const{return a;}};
struct QRect{int a=0,b=0,c=0,d=0;QRect(int x=0,int y=0,int w=0,int h=0):a(x),b(y),c(w),d(h){}int width()const{return c;}int height()const{return d;}int x()const{return a;}int y()const{return b;}};
struct Padding{int t=3,b=5;int top()const{return t;}int bottom()const{return b;}};
struct FileStyle{Padding songPadding,filePadding;int songThumbSize=30,fileThumbSize=40;};
namespace st{int infoMediaSkip=2,infoMediaLeft=3,infoMediaMinGridSize=82,lineWidth=1,linksPhotoSize=32,linksBorder=1;Padding linksMargin;FileStyle overviewFileLayout;
struct Icon{int w=16;int width()const{return w;}}stickersReorderIcon;
struct Position{int a=14;int x()const{return a;}}infoMediaHeaderPosition;}
enum class Type{Photo,Video,PhotoVideo,RoundFile,GIF,RoundVoiceFile,MusicFile,File,Link};
'''
    if case in (None, 'width'):
        methods = '\n'.join(function(section, sig) for sig in (
            'void ListSection::resizeToWidth(int newWidth)',
            'bool ListSection::isOneColumn() const',
            'int ListSection::oneColumnRightPadding() const',
            'QRect ListSection::findItemRect('))
        cpp(root, 'media_section_width', includes + r'''
struct AbstractLayoutItem{int _width=0,_height=0,_maxw=10000,_minh=42,_position=0;int resizeGetHeight(int);int height()const{return _height;}int width()const{return _width;}int position()const{return _position;}};
''' + function(default, 'int AbstractLayoutItem::resizeGetHeight(int width)') + r'''
using BaseLayout=AbstractLayoutItem;
struct Mosaic{bool empty()const{return true;}QRect findRect(int)const{return {};}void setFullWidth(int){}};
struct ListSection{Type _type=Type::MusicFile;bool _canReorder=false;int _minGridSize=0,_itemsLeft=0,_itemsTop=0,_itemsInRow=1,_itemWidth=0,_itemHeight=0;Mosaic _mosaic;std::vector<BaseLayout*>_items;void resizeToWidth(int);bool isOneColumn()const;int oneColumnRightPadding()const;QRect findItemRect(not_null<const BaseLayout*>)const;void refreshHeight(){}};
''' + methods + r'''
int main(){int checks=0;
''' + f'const auto baseSkip={values["infoMediaSkip"]},baseLeft={values["infoMediaLeft"]},baseGrid={values["infoMediaMinGridSize"]},baseHeader={header_x},baseMax={max_width},baseSongRight={song_right},baseIcon={icon_width};\n' + r'''
for(int scale:{1,2,3,4}){st::infoMediaSkip=baseSkip*scale;st::infoMediaLeft=baseLeft*scale;st::infoMediaMinGridSize=baseGrid*scale;st::infoMediaHeaderPosition.a=baseHeader*scale;for(int icon:{12,16,24}){st::stickersReorderIcon.w=icon*scale;for(Type type:{Type::MusicFile,Type::RoundVoiceFile,Type::File,Type::Link})for(bool reorder:{false,true})for(int outer:{100,320,600,1000}){AbstractLayoutItem item;ListSection s;s._type=type;s._canReorder=reorder;s._items={&item};s.resizeToWidth(outer*scale);int margin=(type==Type::File||type==Type::Link)?baseHeader*scale:0;int right=reorder?(icon+baseLeft)*scale:0;int expected=outer*scale-2*margin-right;assert(item.width()==expected-right&&"incumbent layout reserves its own handle strip inside section geometry");auto r=s.findItemRect(&item);assert(r.x()==margin&&r.width()==expected);assert(r.width()==item.width()+right);++checks;}}}
for(int scale:{1,2,3,4})for(int outer:{100,320,600,1000}){st::infoMediaSkip=baseSkip*scale;st::infoMediaLeft=baseLeft*scale;st::infoMediaMinGridSize=baseGrid*scale;st::stickersReorderIcon.w=baseIcon*scale;AbstractLayoutItem item;item._maxw=baseMax*scale;ListSection s;s._canReorder=true;s._items={&item};s.resizeToWidth(outer*scale);auto r=s.findItemRect(&item);int right=s.oneColumnRightPadding();int handleLeft=r.x()+r.width()-right;int nameEdge=r.x()+item.width()-baseSongRight*scale;assert(nameEdge<=handleLeft&&"actual style title budget must not overlap painted handle strip");int candidateEdge=r.x()+std::min(r.width(),baseMax*scale)-baseSongRight*scale;if(outer==320){assert(candidateEdge>handleLeft&&"removing second inset alone overlaps handle; preserve incumbent");}++checks;}
std::cout<<"Actual Section sizing/findItemRect + actual AbstractLayoutItem resize PASS:"<<checks<<" scale/type/reorder/icon/width controls; explicit style/Qt/layout/mosaic doubles, no paint/glyph/RTL proof\n";}
''')
    if case in (None, 'preload'):
        cpp(root, 'media_preload_height', includes +
            function(common, 'int MinItemHeight(Type type, int width)') + '\n' +
            function(stories, '[[nodiscard]] int MinStoryHeight(int width)') + r'''
int main(){int checks=0;
''' + f'const auto baseSkip={values["infoMediaSkip"]},baseGrid={values["infoMediaMinGridSize"]};\n' + r'''
for(int scale:{1,2,3,4}){st::infoMediaSkip=baseSkip*scale;st::infoMediaMinGridSize=baseGrid*scale;
for(int width:{1,2,40,83,84,85,86,100,320,1000,7000,7680,16000,0,-100}){for(Type t:{Type::Photo,Type::Video,Type::PhotoVideo,Type::RoundFile,Type::GIF}){auto h=MinItemHeight(t,width);assert(h>=1&&"preload estimate must remain a positive divisor at narrow and wide widths");int raw=(width-st::infoMediaSkip)/(st::infoMediaMinGridSize+st::infoMediaSkip);if(raw>0&&(st::infoMediaMinGridSize+st::infoMediaSkip)/raw>0){assert(h==(st::infoMediaMinGridSize+st::infoMediaSkip)/raw);}assert(MinStoryHeight(width)==MinItemHeight(Type::PhotoVideo,width));int preloaded=900/h;assert(preloaded>=0);++checks;}}
for(Type t:{Type::File,Type::MusicFile,Type::RoundVoiceFile,Type::Link}){assert(MinItemHeight(t,1)==MinItemHeight(t,16000));++checks;}}
std::cout<<"Actual Common+Stories preload estimates PASS:"<<checks<<" scale/type/narrow/zero/wide controls, valid incumbent outputs preserved; no real viewport/provider/network execution\n";}
''')
    callers = ('Telegram/SourceFiles/info/media/info_media_provider.cpp',
               'Telegram/SourceFiles/info/global_media/info_global_media_provider.cpp')
    for path in callers:
        body = function(source(path), 'void Provider::checkPreload(')
        assert 'MinItemHeight(' in body and 'preloadedHeight / minItemHeight' in body
    assert 'MinStoryHeight(visibleWidth)' in function(stories, 'void Provider::checkPreload(')


if __name__ == '__main__':
    import argparse
    import tempfile
    parser = argparse.ArgumentParser()
    parser.add_argument('--case', choices=('width', 'preload'))
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='luxury-media-geometry-') as directory:
        check(Path(directory), args.case)
