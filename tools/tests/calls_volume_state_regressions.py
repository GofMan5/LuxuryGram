"""Actual participant menu construction, volume callbacks/state and visual setters.

Bundled real rpl; Qt/row/animation/arcs/output doubles. Not native Qt/audio/server QA.
Missing non-min wire volume must mean default gain; explicit wire zero remains unadjudicated.
Generic zero/null menu sentinels and conflicting acknowledgement are characterized separately.
"""
from pathlib import Path
import os
from library_regressions import cpp, function, source


def check(root):
    item = (Path(os.environ['LUXURY_CALLS_VOLUME_SOURCE']).read_text(encoding='utf-8')
            if 'LUXURY_CALLS_VOLUME_SOURCE' in os.environ else source(
                'Telegram/SourceFiles/calls/group/calls_volume_item.cpp'))
    start = item.index('\t_slider->setChangeProgressCallback(')
    callbacks = item[start:item.index('\n\t_slider->setAdjustCallback(', start)]
    methods = '\n'.join(function(item, sig) for sig in (
        'void MenuVolumeItem::setCloudVolume(int volume)',
        'void MenuVolumeItem::setSliderVolume(int volume)',
        'void MenuVolumeItem::setCanMute(bool canMute)'))
    init = item[item.index(', _maxVolume(maxVolume)'):item.index(', _slider(')].replace(',', ':', 1)
    arcs_init = item[item.index('\t_localMuted ?'):item.index(',\n\tUi::Paint::ArcsAnimation::Direction::Right')]
    common = source('Telegram/SourceFiles/calls/group/calls_group_common.h')
    start = common.index('constexpr auto kDefaultVolume')
    default_volume = common[start:common.index(';', start) + 1]
    group = (Path(os.environ['LUXURY_CALLS_GROUP_SOURCE']).read_text(encoding='utf-8')
             if 'LUXURY_CALLS_GROUP_SOURCE' in os.environ else source(
                 'Telegram/SourceFiles/calls/group/calls_group_call.cpp'))
    adapter = function(group, 'void GroupCall::applyOtherParticipantUpdate(')
    state_type = common[common.index('struct ParticipantState {'):common.index('\nstruct RejoinEvent')]
    members = source('Telegram/SourceFiles/calls/group/calls_group_members.cpp')
    start = members.index('\tconst auto muteState = row->state();', members.index('void Members::Controller::addMuteActionsToContextMenu'))
    declarations = members[start:members.index('\n\tauto mutesFromVolume', start)]
    start = members.index('\tconst auto addVolumeItem', start)
    construction = members[start:members.index('\n\t\tmutesFromVolume', start)]
    slider = source('Telegram/SourceFiles/ui/widgets/continuous_sliders.cpp')
    slider_methods = '\n'.join(function(slider, sig).replace('ContinuousSlider::', 'Slider::') for sig in (
        'float64 ContinuousSlider::value() const',
        'void ContinuousSlider::setValue(float64 value)',
        'void ContinuousSlider::setValue(float64 value, float64 receivedTill)',
        'void ContinuousSlider::mousePressEvent(QMouseEvent *e)',
        'void ContinuousSlider::mouseReleaseEvent(QMouseEvent *e)',
        'void ContinuousSlider::updateDownValueFromPos(const QPoint &pos)'))
    cpp(root, 'calls_volume_state', r'''
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <utility>
#include <vector>
#include "rpl/event_stream.h"
#include "rpl/filter.h"
using float64=double; using QPoint=double;
template<class T>using not_null=T;
struct PeerData{PeerData*participant=nullptr;PeerData&owner(){return *this;}PeerData*peer(int){return participant;}};
int peerFromMTP(int peer){return peer;}
struct WireVolume{std::optional<int>value;int value_or_empty()const{return value.value_or(0);}int value_or(int fallback)const{return value.value_or(fallback);}};
struct MTPDgroupCallParticipant{bool min=false,muted=false;WireVolume volume;bool is_min()const{return min;}bool is_muted_by_you()const{return muted;}int vpeer()const{return 1;}const WireVolume&vvolume()const{return volume;}};
struct QMouseEvent{double position=0;double pos(){return position;}void ignore(){}};
namespace base{int SafeRound(double v){return std::lround(v);}template<class T>bool make_weak(T*){return true;}template<class T,class...Args>auto make_unique_q(Args&&...args){return std::make_unique<T>(std::forward<Args>(args)...);}namespace assertion{void log(const char*,const char*,int){std::abort();}}}
namespace st{constexpr auto callPanelDuration=200,groupCallPopupVolumeMenu=1,groupCallMenuVolumeSlider=2,groupCallMenuVolumePadding=3;}
namespace Calls{
namespace Group{constexpr auto kMaxVolume=20000;
''' + default_volume + '\n' + state_type + r'''
}
struct GroupCall{PeerData*_peer;bool known=true;rpl::event_stream<Group::ParticipantState>_otherParticipantStateValue;void applyOtherParticipantUpdate(const MTPDgroupCallParticipant&);};
bool LookupParticipant(GroupCall*call,PeerData*peer){return call->known&&peer;}
''' + adapter + r'''
struct Animation{template<class...T>void start(T...){} };struct Arcs{double value=0;int sets=0;void setValue(double v){value=v;++sets;}};
struct Slider{
 double _value=0,_downValue=0,_receivedTill=-1;bool _mouseDown=false,_disabled=false;int updates=0;
 std::function<void(double)>_changeProgressCallback,_changeFinishedCallback;
 double getCurrentValue()const{return _mouseDown?_downValue:_value;}bool isChanging(){return _mouseDown;}bool isEnabled(){return true;}void update(){++updates;}double computeValue(double v){return v;}
 void setChangeProgressCallback(std::function<void(double)>f){_changeProgressCallback=std::move(f);}void setChangeFinishedCallback(std::function<void(double)>f){_changeFinishedCallback=std::move(f);}
 double value()const;void setValue(double);void setValue(double,double);void mousePressEvent(QMouseEvent*);void mouseReleaseEvent(QMouseEvent*);void updateDownValueFromPos(const QPoint&);
 void drag(double v){QMouseEvent e{v};if(!_mouseDown)mousePressEvent(&e);else updateDownValueFromPos(v);}void release(){QMouseEvent e;mouseReleaseEvent(&e);}
};
''' + slider_methods + r'''
class MenuVolumeItem{public:
 int _maxVolume,_cloudVolume=0,_speakerRect=1,repaints=0;bool _cloudMuted=false,_localMuted=false,_waitingForUpdateVolume=false,_canMute=true;
 double color=0;Slider slider;Slider*_slider=&slider;Arcs arcs;Arcs*_arcs=&arcs;Animation _crossLineAnimation;rpl::lifetime life;
 rpl::event_stream<int>_changeVolumeRequests,_changeVolumeLocallyRequests;rpl::event_stream<bool>_toggleMuteRequests,_toggleMuteLocallyRequests;
 MenuVolumeItem(int,int,int,rpl::producer<Group::ParticipantState>participantState,int startVolume,int maxVolume,bool muted,int)
''' + init + r'''{arcs.value=
''' + arcs_init + r''';setCloudVolume(startVolume);
''' + callbacks + r'''
 }
 void update(int){++repaints;}void updateSliderColor(double v){color=v;}void setCloudVolume(int);void setSliderVolume(int);void setCanMute(bool);rpl::lifetime&lifetime(){return life;}
};
''' + methods + r'''
struct Recorder{std::vector<int>volume,previewVolume;std::vector<bool>mute,previewMute;rpl::lifetime life;
 void listen(MenuVolumeItem&item){item._changeVolumeRequests.events()|rpl::on_next([&](int v){volume.push_back(v);},life);item._changeVolumeLocallyRequests.events()|rpl::on_next([&](int v){previewVolume.push_back(v);},life);item._toggleMuteRequests.events()|rpl::on_next([&](bool v){mute.push_back(v);},life);item._toggleMuteLocallyRequests.events()|rpl::on_next([&](bool v){previewMute.push_back(v);},life);}
 void noRequests(){assert(volume.empty()&&previewVolume.empty()&&mute.empty()&&previewMute.empty());}};
struct Row{enum class State{Active,Inactive,Muted,RaisedHand};State status=State::Active;bool local=false;int vol=10000;State state(){return status;}bool mutedByMe(){return local;}int volume(){return vol;}};
struct Menu{int menu(){return 0;}};
struct Call{bool manage=false,stream=false;bool canManage(){return manage;}bool rtmp(){return stream;}int rtmpVolume(){return 5000;}rpl::event_stream<Group::ParticipantState>states;auto otherParticipantStateValue(){return states.events();}};
struct Controller{Call*_call;bool self=false;bool isMe(PeerData*){return self;}std::unique_ptr<MenuVolumeItem>initial(Menu*menu,PeerData*participantPeer,Row*row,bool participantIsCallAdmin){
''' + declarations + construction + r'''
return volumeItem;}return nullptr;}};
void visual(MenuVolumeItem&item,double target){assert(item.slider.value()==target);assert(item.arcs.value==target);assert(item.color==target);}
}
int main(){using namespace Calls;int cases=0;
for(int initial:{1000,10000,15000,20000})for(bool startMuted:{false,true})for(bool targetMuted:{false,true}){
 PeerData participant;PeerData host{&participant};GroupCall call{&host};
 MenuVolumeItem item(0,0,0,call._otherParticipantStateValue.events(),initial,20000,startMuted,0);Recorder out;out.listen(item);
 call.applyOtherParticipantUpdate({.muted=targetMuted});
 assert(item.slider.value()==(targetMuted?0.:.5)&&"absent non-min wire volume must reset an open menu to default gain");
 visual(item,targetMuted?0.:.5);assert(item._cloudVolume==Group::kDefaultVolume);
 call.applyOtherParticipantUpdate({.muted=targetMuted,.volume={8000}});visual(item,targetMuted?0.:.4);
 call.applyOtherParticipantUpdate({.muted=targetMuted});visual(item,targetMuted?0.:.5);out.noRequests();++cases;
}
for(bool known:{false,true})for(bool minimal:{false,true})if(!known||minimal){
 PeerData participant;PeerData host{&participant};GroupCall call{&host,known};
 MenuVolumeItem item(0,0,0,call._otherParticipantStateValue.events(),8000,20000,false,0);Recorder out;out.listen(item);
 call.applyOtherParticipantUpdate({.min=minimal,.muted=true});visual(item,.4);assert(!item._cloudMuted);out.noRequests();++cases;
}
for(int initial:{10000,15000}){
 PeerData participant;PeerData host{&participant};GroupCall call{&host};
 MenuVolumeItem item(0,0,0,call._otherParticipantStateValue.events(),initial,20000,true,0);Recorder out;out.listen(item);
 item.slider.drag(.75);item.slider.release();assert(item._waitingForUpdateVolume);
 call.applyOtherParticipantUpdate({});assert(!item._waitingForUpdateVolume);
 if(initial==Group::kDefaultVolume){visual(item,.75);assert(out.volume==std::vector<int>{15000});}
 else{visual(item,.5);assert(out.volume.empty());}
 assert(out.mute==std::vector<bool>{false});call.applyOtherParticipantUpdate({.volume={15000}});visual(item,.75);++cases;
}
{
 PeerData participant;PeerData host{&participant};GroupCall call{&host};
 MenuVolumeItem item(0,0,0,call._otherParticipantStateValue.events(),15000,20000,false,0);Recorder out;out.listen(item);
 item.slider.drag(.65);call.applyOtherParticipantUpdate({});visual(item,.65);assert(out.volume.empty());
 item.slider.release();assert(out.volume==std::vector<int>{13000});call.applyOtherParticipantUpdate({.volume={13000}});visual(item,.65);++cases;
}
for(int initial:{1000,5000,10000,20000})for(bool muted:{false,true}){rpl::event_stream<Group::ParticipantState>events;MenuVolumeItem item(0,0,0,events.events(),initial,20000,muted,0);Recorder out;out.listen(item);visual(item,muted?0.:double(initial)/20000);out.noRequests();for(bool target:{true,false,true,false}){const auto repaints=item.repaints;const auto changed=item._cloudMuted!=target;events.fire({.peer=nullptr,.volume=initial,.mutedByMe=target});assert(item._cloudVolume==initial);assert(item._cloudMuted==target&&item._localMuted==target);visual(item,target?0.:double(initial)/20000);assert(!changed||item.repaints>repaints);out.noRequests();}events.fire({.peer=nullptr,.volume=12000,.mutedByMe=true});visual(item,0.);assert(item._cloudVolume==12000);events.fire({.peer=nullptr,.volume=12000,.mutedByMe=false});visual(item,.6);out.noRequests();++cases;}
for(int initial:{1000,5000,10000,20000}){rpl::event_stream<Group::ParticipantState>events;MenuVolumeItem item(0,0,0,events.events(),initial,20000,false,0);Recorder out;out.listen(item);item.slider.drag(.75);const auto count=item.arcs.sets;events.fire({.peer=nullptr,.volume=initial,.mutedByMe=false});assert(item.slider.value()==.75&&item.arcs.sets==count);item.slider.release();assert(out.volume==std::vector<int>{15000});assert(out.mute.empty()&&out.previewMute.empty());events.fire({.peer=nullptr,.volume=initial,.mutedByMe=false});assert((item.slider.value()==.75)&&"repeated saved gain must not erase released intent");events.fire({.peer=nullptr,.volume=15000,.mutedByMe=false});visual(item,.75);assert(!item._waitingForUpdateVolume);++cases;}
for(int initial:{1000,5000,10000,20000}){rpl::event_stream<Group::ParticipantState>events;MenuVolumeItem item(0,0,0,events.events(),initial,20000,true,0);Recorder out;out.listen(item);item.slider.drag(.75);item.slider.release();assert(item._waitingForUpdateVolume&&!item._localMuted);assert(out.mute==std::vector<bool>{false}&&out.previewMute==std::vector<bool>{false});assert(out.volume.empty());for(auto noVolume:{std::optional<int>(0),std::optional<int>()}){events.fire({.peer=nullptr,.volume=noVolume,.mutedByMe=false});assert(item._waitingForUpdateVolume&&item.slider.value()==.75&&out.volume.empty());}events.fire({.peer=nullptr,.volume=initial,.mutedByMe=false});assert(!item._waitingForUpdateVolume&&item.slider.value()==.75);assert(out.volume==std::vector<int>{15000});events.fire({.peer=nullptr,.volume=15000,.mutedByMe=false});visual(item,.75);assert(out.volume==std::vector<int>{15000});++cases;}
 for(int initial:{1000,10000}){rpl::event_stream<Group::ParticipantState>events;MenuVolumeItem item(0,0,0,events.events(),initial,20000,true,0);Recorder out;out.listen(item);item.slider.drag(.75);item.slider.release();assert(item._waitingForUpdateVolume);events.fire({.peer=nullptr,.volume=12000,.mutedByMe=false});assert(!item._waitingForUpdateVolume);assert(item._cloudVolume==12000&&item.slider.value()==.6&&"conflicting server acknowledgement must settle the authoritative volume");++cases;}
for(auto state:{Row::State::Active,Row::State::Inactive,Row::State::Muted,Row::State::RaisedHand})for(bool local:{false,true})for(bool self:{false,true})for(bool admin:{false,true})for(bool manage:{false,true})for(bool stream:{false,true}){Call call;call.manage=manage;call.stream=stream;Controller owner{&call,self};Menu menu;PeerData peer;Row row;row.status=state;row.local=local;row.vol=8000;auto item=owner.initial(&menu,&peer,&row,admin);const auto adminMuted=state==Row::State::Muted||state==Row::State::RaisedHand;assert(bool(item)==(!adminMuted||self));if(item){visual(*item,(adminMuted||local)?0.:double(stream?5000:8000)/20000);assert(item->_cloudVolume==(stream?5000:8000));assert(item->_canMute==(!self&&!(admin&&manage)));}++cases;}
std::cout<<"Actual Members/init/callback/state/setters + actual slider press/release PASS:"<<cases<<" matrix cases; slider/color/arcs/repaint parity, request cardinality/drag/delayed unmute, real-rpl. Qt/row/arcs/animation/output doubled; no native popup/Qt/audio/server/profile\n";}
''', extra=[arg for p in ('Telegram/lib_rpl', 'Telegram/lib_base', 'Telegram/ThirdParty/GSL/include') for arg in ('-I', str(Path(__file__).resolve().parents[2] / p))])


if __name__ == '__main__':
    import tempfile
    with tempfile.TemporaryDirectory(prefix='luxury-calls-volume-state-') as directory:
        check(Path(directory))
