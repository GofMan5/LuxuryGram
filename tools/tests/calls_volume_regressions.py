"""Actual RTMP consumer subscriptions + volume preview/commit and GroupCall methods.

Real bundled rpl; mechanical slider/guard/animation/output doubles, no Qt/audio/call.
"""
from pathlib import Path
from library_regressions import cpp, function, source


def check(root):
    item = source('Telegram/SourceFiles/calls/group/calls_volume_item.cpp')
    callbacks = item[item.index('\t_slider->setChangeProgressCallback('):item.index('\n\tstd::move(\n\t\tparticipantState')]
    settings = source('Telegram/SourceFiles/calls/group/calls_group_settings.cpp')
    consumers = settings[settings.index('\t\tconst auto toggleMute = crl::guard(layout,'):settings.index('\n\t\tfakeMenu->addAction(std::move(volumeItem));')]
    group = source('Telegram/SourceFiles/calls/group/calls_group_call.cpp')
    methods = '\n'.join(function(group, sig) for sig in (
        'void GroupCall::toggleMute(const Group::MuteRequest &data)',
        'void GroupCall::changeVolume(const Group::VolumeRequest &data)'))
    common = source('Telegram/SourceFiles/calls/group/calls_group_common.h')
    requests = common[common.index('constexpr auto kDefaultVolume ='):common.index('\nstruct ParticipantState')]
    cpp(root, 'calls_volume', r'''
#include <algorithm>
#include <cassert>
#include <cmath>
#include <functional>
#include <iostream>
#include <memory>
#include <utility>
#include <vector>
#include "rpl/event_stream.h"
#include "rpl/merge.h"
#include "rpl/range.h"
#include "rpl/producer.h"
#include <optional>
using float64=double;
struct PeerData{};
template<class T>using not_null=T;
namespace base{int SafeRound(double value){return std::lround(value);}namespace assertion{void log(const char*,const char*,int){std::abort();}}}
namespace crl{using time=int;template<class T,class F>F guard(T*,F f){return f;}}
namespace st{constexpr auto callPanelDuration=200;}
namespace Calls{namespace Group{
''' + requests + r'''
}class GroupCall{public:bool _rtmp=true;int _singleSourceVolume=0,updates=0,localCalls=0,remoteCalls=0;bool videoStream(){return false;}void updateInstanceVolumes(){++updates;}void applyParticipantLocally(PeerData*,bool,std::optional<int>){++localCalls;}void editParticipant(PeerData*,bool,std::optional<int>){++remoteCalls;}void toggleMute(const Group::MuteRequest &data);void changeVolume(const Group::VolumeRequest &data);};
''' + methods + r'''
struct Animation{template<class...T>void start(T...){}};
struct Arcs{void setValue(double){}};
struct Slider{double value=0;std::function<void(double)> progress,finished;template<class F>void setChangeProgressCallback(F f){progress=f;}template<class F>void setChangeFinishedCallback(F f){finished=f;}void drag(double v){progress(v);}void release(double v){finished(v);value=v;}};
class MenuVolumeItem{public:int _maxVolume=Group::kMaxVolume,_cloudVolume=0,_speakerRect=0;bool _cloudMuted=false,_localMuted=false,_waitingForUpdateVolume=false;Slider slider;Slider*_slider=&slider;Arcs arcs;Arcs*_arcs=&arcs;Animation _crossLineAnimation;rpl::lifetime life;rpl::event_stream<bool>_toggleMuteLocallyRequests,_toggleMuteRequests;rpl::event_stream<int>_changeVolumeLocallyRequests,_changeVolumeRequests;explicit MenuVolumeItem(int volume):_cloudVolume(volume){
''' + callbacks + r'''
}void update(int){}void updateSliderColor(double){}rpl::lifetime&lifetime(){return life;}auto toggleMuteLocallyRequests(){return _toggleMuteLocallyRequests.events();}auto toggleMuteRequests(){return _toggleMuteRequests.events();}auto changeVolumeLocallyRequests(){return _changeVolumeLocallyRequests.events();}auto changeVolumeRequests(){return _changeVolumeRequests.events();}};
void connectRtmp(MenuVolumeItem*volumeItem,GroupCall*call,PeerData*peer){auto layout=volumeItem;
''' + consumers + r'''
}
}int main(){using namespace Calls;int cases=0;for(int initial:{0,1000,10000,20000})for(double target:{0.,.25,.5,1.}){GroupCall call;call._singleSourceVolume=initial;PeerData peer;MenuVolumeItem item(initial);connectRtmp(&item,&call,&peer);item.slider.drag(target);assert((call._singleSourceVolume==base::SafeRound(target*Group::kMaxVolume))&&"preview output");item.slider.release(target);assert((call._singleSourceVolume==base::SafeRound(target*Group::kMaxVolume))&&"finished output");assert(call.remoteCalls==0&&call.localCalls==0);item.slider.drag(.75);item.slider.release(.75);assert(call._singleSourceVolume==15000);item.slider.drag(0.);item.slider.release(0.);assert(call._singleSourceVolume==0);++cases;}std::cout<<"Actual RTMP consumer + volume callbacks/GroupCall routing PASS:"<<cases<<" initialvolumes/zero-positive commits/repeated unmute-remute, real-rpl; slider/guard/animation/output doubles, no native popup/Qt/audio/network/profile\n";}
''', extra=[arg for p in ('Telegram/lib_rpl', 'Telegram/lib_base', 'Telegram/ThirdParty/GSL/include') for arg in ('-I', str(Path(__file__).resolve().parents[2] / p))])


if __name__ == '__main__':
    import tempfile
    with tempfile.TemporaryDirectory(prefix='luxury-calls-volume-') as directory:
        check(Path(directory))
