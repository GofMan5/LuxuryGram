"""Actual SavedMusic provider memento methods + sparse slice; no Qt, account, playback or network."""
from pathlib import Path
import tempfile
from library_regressions import ROOT, cpp, function, source
from saved_music_navigation_regressions import check as navigation


def check(root, case='all'):
    text = source('Telegram/SourceFiles/info/saved/info_saved_music_provider.cpp')
    methods = '\n'.join(function(text, signature) for signature in (
        'bool MusicProvider::isMyItem(', 'int64 MusicProvider::scrollTopStatePosition(',
        'HistoryItem *MusicProvider::scrollTopStateItem(',
        'void MusicProvider::saveState(', 'void MusicProvider::restoreState('))
    widget_h = source('Telegram/SourceFiles/info/media/info_media_widget.h')
    setters = widget_h[widget_h.index('\tvoid setAroundId('):widget_h.index('\tvoid setSearchState(')]
    cpp(root, 'saved_music_scroll_' + case, r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>
using int64=long long;using uint64=unsigned long long;template<class T>using Fn=std::function<T>;
template<class T>struct not_null{T p;not_null(T v):p(v){assert(v);}T get()const{return p;}T operator->()const{return p;}operator T()const{return p;}};
namespace ranges{using std::ranges::find;using std::ranges::lower_bound;using std::ranges::reverse;}
namespace base{template<class T>struct flat_set;}
#define Expects(x) assert(x)
struct MsgId{int64 bare=0;MsgId()=default;MsgId(int64 v):bare(v){}friend auto operator<=>(MsgId,MsgId)=default;};
struct FullMsgId{int peer=0;MsgId msg;friend auto operator<=>(FullMsgId,FullMsgId)=default;};struct GlobalMsgId{int sessionUniqueId=0;FullMsgId itemId;friend auto operator<=>(GlobalMsgId,GlobalMsgId)=default;};
struct Peer{int id;};struct History{Peer*peer;int session;};struct HistoryItem{MsgId id;History*h;bool saved=true;History*history()const{return h;}bool isSavedMusicItem()const{return saved;}FullMsgId fullId()const{return{h->peer->id,id};}GlobalMsgId globalId()const{return{h->session,fullId()};}};
std::map<GlobalMsgId,HistoryItem*>registry;HistoryItem*MessageByGlobalId(GlobalMsgId id){auto i=registry.find(id);return i==registry.end()?nullptr:i->second;}
namespace Data{
''' + function(source('Telegram/SourceFiles/data/data_abstract_sparse_ids.h'), 'template <typename IdsContainer>') + r''';
using SavedMusicSlice=AbstractSparseIds<std::vector<not_null<HistoryItem*>>>;
}
struct Session{int id=1;struct Owner{int session=1;HistoryItem*message(FullMsgId id){return MessageByGlobalId({session,id});}}owner;Owner&data(){return owner;}};struct Controller{Session s;Session&session(){return s;}};
namespace base{struct Weak{std::weak_ptr<bool>p;operator bool()const{auto a=p.lock();return a&&*a;}};template<class T>Weak make_weak(T*p){return{p->alive};}}
namespace Info::Media{struct ListScrollTopState{int64 position=0;HistoryItem*item=nullptr;int shift=0;};struct Memento{FullMsgId _aroundId;int _idsLimit=0;GlobalMsgId _scrollTopItem;int64 _scrollTopItemPosition=0;int _scrollTopShift=0;
''' + setters + r'''
};}
namespace Info::Saved{using Media::ListScrollTopState;struct MusicProvider{std::shared_ptr<bool>alive=std::make_shared<bool>(true);static constexpr int kMinimalIdsLimit=16;Peer*_peer;Controller*_controller;HistoryItem*_aroundId=nullptr;int _idsLimit=16,refreshes=0;Data::SavedMusicSlice _slice;MusicProvider(Peer*p,Controller*c):_peer(p),_controller(c){}~MusicProvider(){*alive=false;}void refreshViewer(){assert(*alive);++refreshes;}bool isMyItem(not_null<const HistoryItem*>);int64 scrollTopStatePosition(not_null<HistoryItem*>);HistoryItem*scrollTopStateItem(ListScrollTopState);void saveState(not_null<Media::Memento*>,ListScrollTopState);void restoreState(not_null<Media::Memento*>,Fn<void(ListScrollTopState)>);};
''' + methods + r'''
}
int main(){using namespace Info;const std::string which="CASE";Peer peer{10},otherPeer{11},foreignSameId{10};History history{&peer,1},otherHistory{&otherPeer,1},foreignHistory{&foreignSameId,2};Controller controller;std::vector<HistoryItem>items;items.reserve(80);for(int i=0;i<80;++i){items.push_back({MsgId(-1000+i*5),&history});registry[items.back().globalId()]=&items.back();}HistoryItem unrelated{MsgId(-1000),&otherHistory},foreign{MsgId(-800),&foreignHistory};registry[unrelated.globalId()]=&unrelated;registry[foreign.globalId()]=&foreign;
Saved::MusicProvider p(&peer,&controller);p._aroundId=&items[40];p._idsLimit=30;std::vector<not_null<HistoryItem*>>loaded;for(int i=30;i<70;++i)loaded.push_back(&items[i]);p._slice={loaded,80,30,10};
Media::Memento saved;Media::ListScrollTopState state{items[43].id.bare,&items[43],7};
if(which=="save"||which=="all"){
 p.saveState(&saved,state);assert(saved.idsLimit()==30&&saved.aroundId()==items[43].fullId()&&saved.scrollTopItem()==items[43].globalId()&&saved.scrollTopItemPosition()==items[43].id.bare&&saved.scrollTopShift()==7);
 Media::Memento untouched;p.saveState(&untouched,{});assert(untouched.idsLimit()==0);p.saveState(&untouched,{1,&unrelated,4});assert(untouched.idsLimit()==0);
 p._aroundId=nullptr;p.saveState(&untouched,state);assert(untouched.aroundId()==items[43].fullId());HistoryItem notMusic{MsgId(4),&history,false};Media::Memento rejected;p.saveState(&rejected,{4,&notMusic,1});assert(rejected.idsLimit()==0);
}
if(which=="resolve"||which=="all"){
 assert(p.scrollTopStateItem(state)==&items[43]);HistoryItem removed{items[43].id,&history};p._slice={{&items[42],&items[44]},2,0,0};assert(p.scrollTopStateItem({removed.id.bare,&removed,7})==&items[42]);assert(p.scrollTopStateItem({items[44].id.bare,nullptr,0})==&items[44]);p._slice={};assert(!p.scrollTopStateItem(state));
 HistoryItem largeA{MsgId((1LL<<56)+100),&history},largeB{MsgId((1LL<<56)+300),&history};p._slice={{&largeB,&largeA},2,0,0};assert(p.scrollTopStateItem({largeA.id.bare,&largeA,0})==&largeA);assert(p.scrollTopStateItem({(1LL<<56)+110,nullptr,0})==&largeA);
 assert(p.scrollTopStateItem({std::numeric_limits<int64>::min(),nullptr,0})==&largeA);assert(p.scrollTopStateItem({std::numeric_limits<int64>::max(),nullptr,0})==&largeB);
 auto dead=std::make_unique<HistoryItem>(HistoryItem{MsgId((1LL<<56)+101),&history});Media::ListScrollTopState old{dead->id.bare,dead.get(),3};dead.reset();assert(p.scrollTopStateItem(old)==&largeA);
}
if(which=="restore"||which=="all"){
 if(which=="restore"){saved.setAroundId(items[43].fullId());saved.setIdsLimit(30);saved.setScrollTopItem(items[43].globalId());saved.setScrollTopItemPosition(state.position);saved.setScrollTopShift(state.shift);}
 Saved::MusicProvider restored(&peer,&controller);int callbacks=0;restored.restoreState(&saved,[&](auto value){++callbacks;assert(value.item==&items[43]&&value.position==state.position&&value.shift==7);assert(restored._aroundId==&items[43]);});assert(callbacks==1&&restored._idsLimit==30&&restored.refreshes==1);
 saved.setAroundId(unrelated.fullId());restored.restoreState(&saved,[&](auto){assert(false);});assert(restored.refreshes==1);
 saved.setAroundId(items[43].fullId());saved.setScrollTopItem(foreign.globalId());restored.restoreState(&saved,[&](auto value){assert(!value.item);});assert(restored.refreshes==2);
 registry.erase(items[43].globalId());saved.setScrollTopItem(items[43].globalId());restored.restoreState(&saved,[&](auto value){assert(!value.item);});assert(!restored._aroundId&&restored.refreshes==3);
 saved.setIdsLimit(0);restored.restoreState(&saved,[&](auto){assert(false);});saved.setIdsLimit(-3);restored.restoreState(&saved,[&](auto){assert(false);});assert(restored.refreshes==3);
 saved.setIdsLimit(30);auto gone=std::make_unique<Saved::MusicProvider>(&peer,&controller);gone->restoreState(&saved,[&](auto){gone.reset();});assert(!gone);
}
std::cout<<"Actual SavedMusic provider+memento+SparseIds PASS: "<<which<<"; viewport anchor/window/shift roundtrip, null/wrong-peer/removed/foreign-session controls, live-only fallback, callback deletion; no native Qt/playback/network\n";
}
'''.replace('"CASE"', '"' + case + '"'))
    if case == 'all':
        navigation(root)


if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument('--case', choices=('save', 'resolve', 'restore', 'all'), default='all')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='luxury-music-scroll-') as directory:
        check(Path(directory), args.case)
