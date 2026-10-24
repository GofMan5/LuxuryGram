"""Group-call reload through extracted production model and complete bar owner.

Real full-call replacement, participant slice/event suppression, structs/accessors,
GSL, range-v3 and rpl; typed MTP/Qt/peer/session/queue/weak-lifetime seams.
Offline C++20 Debug check, NOT full Qt TU/render/audio/transport/profile QA.
"""
from pathlib import Path
import os
import tempfile

from library_regressions import ROOT, cpp, function, source


def check(root):
    owner = (Path(os.environ['LUXURY_GROUP_CALL_BAR_SOURCE']).read_text(encoding='utf-8')
             if 'LUXURY_GROUP_CALL_BAR_SOURCE' in os.environ else source(
                 'Telegram/SourceFiles/history/view/history_view_group_call_bar.cpp'))
    model = source('Telegram/SourceFiles/data/data_group_call.cpp')
    header = source('Telegram/SourceFiles/data/data_group_call.h')
    bodies = '\n'.join(function(model, signature) for signature in (
        'bool GroupCall::listenersHidden() const',
        'not_null<PeerData*> GroupCall::peer() const',
        'auto GroupCall::participants() const',
        'rpl::producer<int> GroupCall::fullCountValue() const',
        'rpl::producer<> GroupCall::participantsReloaded()',
        'auto GroupCall::participantUpdated() const',
        'void GroupCall::processFullCallUsersChats(',
        'void GroupCall::processFullCallFields(',
        'void GroupCall::processFullCall(',
        'void GroupCall::applyParticipantsSlice('))
    bodies = bodies.replace('_participantUpdates.fire({',
                            '_participantUpdates.fire(ParticipantUpdate{')
    cpp(root, 'group_call_bar_reload', r'''
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include <gsl/gsl>
#include <rpl/rpl.h>
#include <range/v3/algorithm/contains.hpp>
#include <range/v3/algorithm/find.hpp>
namespace base::assertion { void log(const char*,const char*,int){std::abort();} }
using gsl::not_null;
using uint64=std::uint64_t;using uint32=std::uint32_t;
using TimeId=int;using QString=std::string;using QImage=int;
using InMemoryKey=std::pair<uint64,uint64>;
#define Assert(condition) assert(condition)
namespace base {
struct has_weak_ptr { std::shared_ptr<int> alive=std::make_shared<int>(1); };
struct weak { std::weak_ptr<int> alive;explicit operator bool()const{return !alive.expired();} };
template<class T>weak make_weak(T*p){return {p->alive};}
template<class T>weak make_weak(gsl::not_null<T*>p){return make_weak(static_cast<T*>(p));}
}
std::vector<std::function<void()>> queue;
namespace crl {
template<class F>void on_main(base::has_weak_ptr*p,F f){
 std::weak_ptr<int>alive=p->alive;queue.push_back([alive,f]{if(!alive.expired())f();});
}
}
void drain(){while(!queue.empty()){auto tasks=std::exchange(queue,{});for(auto&task:tasks)task();}}
namespace style { int DevicePixelRatio(){return 1;} struct GroupCallUserpics{}; }
namespace Ui {
struct PeerUserpicView{};
struct GroupCallUser { QImage userpic=0;InMemoryKey userpicKey={};uint64 id=0;bool speaking=false; };
struct GroupCallBarContent {
 QString title;TimeId scheduleDate=0;int count=0;bool shown=false;bool livestream=false;
 std::vector<GroupCallUser> users;
};
}
using std::begin;using std::end;
struct PeerData;
struct PeerOwner {
 not_null<PeerData*>peer(PeerData*p){return not_null<PeerData*>(p);}
 template<class T>void processUsers(const T&){}template<class T>void processChats(const T&){}
 void unregisterInvitedToCallUser(int,PeerData*,bool){}
};
namespace Main { struct Session { rpl::event_stream<>downloaded;auto downloaderTaskFinished(){return downloaded.events();} }; }
struct PeerData {
 struct Id{uint64 value=0;}id;Main::Session*s=nullptr;PeerOwner*o=nullptr;
 bool broadcast=false,hasPhoto=false,loaded=true;int version=1;
 Main::Session&session(){return *s;}PeerOwner&owner(){return *o;}bool isBroadcast(){return broadcast;}
 void setAbout(const QString&){}PeerData*asUser(){return this;}
 void loadUserpic(){}bool hasUserpic(){return hasPhoto;}bool useEmptyUserpic(Ui::PeerUserpicView&){return !loaded;}
 InMemoryKey userpicUniqueKey(Ui::PeerUserpicView&){return {id.value,uint64(loaded?version:0)};}
 static QImage GenerateUserpicImage(not_null<PeerData*>p,Ui::PeerUserpicView&,int){return p->loaded?int(p->id.value):0;}
};
namespace Calls {
struct ParticipantVideoParams{};
namespace Group { constexpr int kDefaultVolume=10000; }
template<class T,class U>auto ParseVideoParams(T,U,std::shared_ptr<ParticipantVideoParams>existing){return existing;}
}
uint32 GetAdditionalAudioSsrc(const std::shared_ptr<Calls::ParticipantVideoParams>&){return 0;}
PeerData*peerFromMTP(PeerData*p){return p;}QString qs(const QString&s){return s;}
template<class T>struct ApiValue { T v; };
template<class T>struct ApiOptional:std::optional<T> {
 using std::optional<T>::optional;T value_or_empty()const{return this->value_or(T{});}
};
struct MTPDgroupCallParticipant {
 PeerData*p=nullptr;int date=0;ApiOptional<int>active;bool left=false,muted=false,min=false;
 PeerData*vpeer()const{return p;}bool is_left()const{return left;}bool is_muted()const{return muted;}
 bool is_can_self_unmute()const{return !muted;}bool is_min()const{return min;}
 bool is_volume_by_admin()const{return false;}bool is_muted_by_you()const{return false;}
 bool is_video_joined()const{return false;}bool is_just_joined()const{return false;}
 ApiOptional<QString>vabout()const{return {};}
 ApiOptional<int>vactive_date()const{return active;}ApiOptional<int>vvolume()const{return {};}
 ApiOptional<uint64>vraise_hand_rating()const{return {};}
 ApiValue<int>vdate()const{return {date};}ApiValue<int>vsource()const{return {0};}
 int vvideo()const{return 0;}int vpresentation()const{return 0;}
};
struct MTPGroupCallParticipant { MTPDgroupCallParticipant data;template<class F>void match(F f)const{f(data);} };
template<class T>using QVector=std::vector<T>;
struct MTPDgroupCall { int count=0; };
struct MTPDgroupCallDiscarded{};
struct MTPGroupCall { MTPDgroupCall data;template<class F,class G>void match(F f,G)const{f(data);} };
struct MTPDphone_groupCall {
 ApiValue<QVector<MTPGroupCallParticipant>>list;MTPGroupCall call;
 const auto&vparticipants()const{return list;}QString vparticipants_next_offset()const{return {};}
 const MTPGroupCall&vcall()const{return call;}int vusers()const{return 0;}int vchats()const{return 0;}
};
struct MTPphone_GroupCall { MTPDphone_groupCall data;template<class F>void match(F f)const{f(data);} };
template<class K,class V>struct Map:std::map<K,V>{void remove(K k){this->erase(k);}};
namespace Data {
''' + function(header, 'struct GroupCallParticipant') + r''';
struct GroupCall:base::has_weak_ptr {
 using Participant=GroupCallParticipant;
''' + function(header, 'struct ParticipantUpdate') + ';\n'
+ function(header, 'enum class ApplySliceSource') + r''';
 PeerData*_peer=nullptr;int _id=1,_serverParticipantsCount=0;
 bool _allParticipantsLoaded=false,_listenersHidden=false,_rtmp=false;
 std::vector<Participant>_participants;Map<PeerData*,int>_speakingByActiveFinishes;
 std::map<uint32,not_null<PeerData*>>_participantPeerByAudioSsrc;QString _nextOffset;
 rpl::variable<int>_fullCount=0;rpl::variable<QString>_title=QString("call");
 rpl::variable<TimeId>_scheduleDate=0;
 rpl::event_stream<ParticipantUpdate>_participantUpdates;rpl::event_stream<>_participantsReloaded;
 explicit GroupCall(PeerData*p):_peer(p){}
 bool rtmp()const{return _rtmp;}bool listenersHidden()const;not_null<PeerData*>peer()const;
 auto participants()const->const std::vector<Participant>&;
 rpl::producer<int>fullCountValue()const;rpl::producer<>participantsReloaded();
 auto participantUpdated()const->rpl::producer<ParticipantUpdate>;
''' + function(header, '[[nodiscard]] rpl::producer<QString> titleValue() const') + '\n'
+ function(header, '[[nodiscard]] rpl::producer<TimeId> scheduleDateValue() const') + r'''
 void processFullCallUsersChats(const MTPphone_GroupCall&);
 void processFullCallFields(const MTPphone_GroupCall&);void processFullCall(const MTPphone_GroupCall&);
 void applyParticipantsSlice(const QVector<MTPGroupCallParticipant>&,ApplySliceSource);
 void applyCallFields(const MTPDgroupCall&d){_serverParticipantsCount=d.count;}
 void finishParticipantsSliceRequest(){_fullCount=_serverParticipantsCount;}
 void discard(const MTPDgroupCallDiscarded&){assert(false);}
 void changePeerEmptyCallFlag(){}void computeParticipantsCount(){_fullCount=_serverParticipantsCount;}
};
''' + bodies + '\n}\nnamespace HistoryView {\n'
+ function(source('Telegram/SourceFiles/history/view/history_view_group_call_bar.h'), 'struct UserpicInRow') + ';\n'
+ function(owner, 'rpl::producer<Ui::GroupCallBarContent> GroupCallBarContentByCall(') + r'''
}
MTPphone_GroupCall full(std::initializer_list<PeerData*>peers,int count=-1){
 MTPphone_GroupCall result;int date=100;
 for(auto p:peers)result.data.list.v.push_back({{.p=p,.date=date--}});
 result.data.call.data.count=(count<0)?int(peers.size()):count;return result;
}
std::vector<uint64>ids(const Ui::GroupCallBarContent&c){std::vector<uint64>r;for(auto&u:c.users)r.push_back(u.id);return r;}
int main(){
 Main::Session session;PeerOwner peers;PeerData p[8];
 for(int i=0;i!=8;++i){p[i].id.value=i+1;p[i].s=&session;p[i].o=&peers;}
 Data::GroupCall call(&p[7]);call.processFullCall(full({&p[0],&p[1],&p[2]}));
 std::vector<Ui::GroupCallBarContent>out;rpl::lifetime life;int updates=0,reloads=0;
 call.participantUpdated()|rpl::on_next([&]{++updates;},life);
 call.participantsReloaded()|rpl::on_next([&]{++reloads;},life);
 HistoryView::GroupCallBarContentByCall(&call,24)|rpl::on_next([&](const auto&c){out.push_back(c);},life);
 drain();assert(out.size()==1&&ids(out.back())==std::vector<uint64>({1,2,3}));out.clear();
 call.processFullCall(full({&p[3],&p[4],&p[5]}));drain();
 assert(updates==0&&reloads==1&&call.participants().size()==3&&call.participants().front().peer==&p[3]);
 assert(out.size()==1&&ids(out.back())==std::vector<uint64>({4,5,6})&&"authoritative same-count reload must replace all cached userpics");out.clear();
  p[3].version=2;call.processFullCall(full({&p[3],&p[4],&p[5]}));drain();
  assert(updates==0&&reloads==2&&out.size()==1&&ids(out.back())==std::vector<uint64>({4,5,6})&&out.back().users.front().userpicKey.second==2&&"authoritative reload must refresh avatar keys");out.clear();
  call.processFullCall(full({},0));drain();
  assert(updates==0&&reloads==3&&!out.empty()&&out.back().users.empty()&&out.back().count==0&&"authoritative empty reload must clear cached userpics");out.clear();
  call.processFullCall(full({&p[0],&p[1],&p[2],&p[3]}));drain();
  assert(updates==0&&reloads==4&&!out.empty()&&ids(out.back())==std::vector<uint64>({1,2,3})&&"authoritative reload must retain the existing cap3");
 std::cout<<"Group-call bar extracted production PASS: full model replacement and same-count bar refresh; actual complete producer + full-call/slice/event/accessors + GSL/range-v3/rpl, typed MTP/Qt/peer/count/queue/weak doubles. NOT native Qt/render/audio/transport QA\n";
}
''', extra=('-I'+str(ROOT/'Telegram/lib_rpl'),
            '-I'+str(ROOT/'Telegram/lib_base'),
            '-I'+str(ROOT/'Telegram/ThirdParty/GSL/include'),
            '-I'+str(ROOT/'Telegram/ThirdParty/range-v3/include')))


if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='luxury-group-call-bar-') as directory:
        check(Path(directory))
