"""Requests-bar same-count refresh through extracted production owners.

Real ChatData/ChannelData setters, profile producer, complete requests-bar producer,
GSL, range-v3 and rpl. Changes dispatch, Qt/userpic, queue and weak guard doubles.
Offline C++20 Debug probe; NOT native Qt TU/render/platform/network acceptance.
"""
from pathlib import Path
import os
import re
import tempfile

from library_regressions import ROOT, cpp, function, source


def check(root):
    owner = (Path(os.environ['LUXURY_REQUESTS_BAR_SOURCE']).read_text(encoding='utf-8')
             if 'LUXURY_REQUESTS_BAR_SOURCE' in os.environ else source(
                 'Telegram/SourceFiles/history/view/history_view_requests_bar.cpp'))
    setters = '\n'.join(function(source('Telegram/SourceFiles/data/data_' + kind + '.cpp'),
        'void ' + kind.title() + 'Data::setPendingRequestsCount(\n\t\tint count,\n\t\tstd::vector<UserId>')
        for kind in ('chat', 'channel'))
    limit = re.search(r'inline constexpr auto kRecentRequestsLimit = \d+;', source(
        'Telegram/SourceFiles/history/view/history_view_requests_bar.h')).group()
    content = function(source('Telegram/SourceFiles/ui/chat/requests_bar.h'),
                       'struct RequestsBarContent')
    row = function(source('Telegram/SourceFiles/history/view/history_view_group_call_bar.h'),
                   'struct UserpicInRow')
    profile = function(source('Telegram/SourceFiles/info/profile/info_profile_values.cpp'),
                       'rpl::producer<int> PendingRequestsCountValue(')
    cpp(root, 'requests_bar_same_count', r'''
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include <gsl/gsl>
#include <rpl/rpl.h>
#include <range/v3/algorithm/equal.hpp>
#include <range/v3/algorithm/find.hpp>
#include <range/v3/algorithm/rotate.hpp>
namespace base::assertion { void log(const char*,const char*,int){std::abort();} }
using gsl::not_null;
using QString=std::string;
using UserId=int;
using InMemoryKey=int;
#define Unexpected(message) std::abort()
namespace base {
struct has_weak_ptr { std::shared_ptr<int> alive=std::make_shared<int>(1); };
template<class T>auto take(T&value){return std::exchange(value,T{});}
}
std::vector<std::function<void()>> queue;
namespace crl {
template<class F>void on_main(base::has_weak_ptr*p,F f){
 std::weak_ptr<int> alive=p->alive;queue.push_back([alive,f]{if(!alive.expired())f();});
}
}
void drain(){while(!queue.empty()){auto tasks=std::exchange(queue,{});for(auto&task:tasks)task();}}
namespace style { int DevicePixelRatio(){return 1;} }
namespace Ui {
struct PeerUserpicView{};
struct GroupCallUser { int userpic=0,userpicKey=0,id=0; };
''' + content + r''';
}
struct PeerData;struct UserData;struct ChatData;struct ChannelData;
namespace Data {
enum class UpdateFlag { PendingRequests };
struct Changes {
 rpl::event_stream<PeerData*> events;std::set<PeerData*> pending;
 void peerUpdated(PeerData*p,UpdateFlag){pending.insert(p);}
 rpl::producer<PeerData*>peerFlagsValue(not_null<PeerData*>p,UpdateFlag){
  return rpl::single(p.get())|rpl::then(events.events()|rpl::filter([=](PeerData*v){return v==p.get();}));
 }
 void dispatch(){for(auto p:std::exchange(pending,{}))events.fire_copy(p);}
};
struct Session { std::map<int,std::unique_ptr<UserData>> users;UserData*user(int id); };
}
using UpdateFlag=Data::UpdateFlag;
namespace Main {
struct Session {
 Data::Changes changes_;Data::Session data;rpl::event_stream<>downloaded;
 Data::Changes&changes(){return changes_;}
 auto downloaderTaskFinished(){return downloaded.events();}
};
}
struct PeerData {
 Main::Session*s;struct Id{int value=0;}id;
 int _pendingRequestsCount=0;std::vector<UserId>_recentRequesters;
 bool broadcast=false,forum=false,tabs=false,hasPhoto=false,loaded=true;
 virtual ~PeerData()=default;
 explicit PeerData(Main::Session*p):s(p){}
 ChatData*asChat();ChannelData*asChannel();
 bool isChat(){return asChat()!=nullptr;}bool isChannel(){return asChannel()!=nullptr;}
 bool isBroadcast(){return broadcast;}bool isForum(){return forum;}bool useSubsectionTabs(){return tabs;}
 Main::Session&session(){return *s;}Data::Session&owner(){return s->data;}
 int pendingRequestsCount()const{return _pendingRequestsCount;}
 const auto&recentRequesters()const{return _recentRequesters;}
 void loadUserpic(){}bool hasUserpic(){return hasPhoto;}bool useEmptyUserpic(Ui::PeerUserpicView&){return !loaded;}
 int userpicUniqueKey(Ui::PeerUserpicView&){return id.value*10+(loaded?1:0);}
 static int GenerateUserpicImage(not_null<PeerData*>p,Ui::PeerUserpicView&,int){return p->loaded?p->id.value:0;}
};
struct UserData:PeerData {
 UserData(Main::Session*s,int value):PeerData(s){id.value=value;}
 QString shortName(){return "short-"+std::to_string(id.value);}QString name(){return "full-"+std::to_string(id.value);}
};
struct ChatData:PeerData { using PeerData::PeerData;void setPendingRequestsCount(int,std::vector<UserId>); };
struct ChannelData:PeerData {
 using PeerData::PeerData;enum class Flag{Forum};rpl::event_stream<bool>forumChanges;
 void setPendingRequestsCount(int,std::vector<UserId>);
};
ChatData*PeerData::asChat(){return dynamic_cast<ChatData*>(this);}
ChannelData*PeerData::asChannel(){return dynamic_cast<ChannelData*>(this);}
UserData*Data::Session::user(int id){return users.at(id).get();}
namespace Data {
auto PeerFlagValue(not_null<ChannelData*>p,ChannelData::Flag){return rpl::single(p->forum)|rpl::then(p->forumChanges.events());}
}
''' + setters + '\nnamespace Info::Profile {\n' + profile + '\n}\nnamespace HistoryView {\n' + limit + '\n' + row + ';\n' + function(owner, 'rpl::producer<Ui::RequestsBarContent> RequestsBarContentByPeer(') + r'''
}
std::vector<int>ids(const Ui::RequestsBarContent&c){std::vector<int>r;for(auto&u:c.users)r.push_back(u.id);return r;}
template<class Peer>void exercise(bool broadcast=false){
 Main::Session session;
 for(int id=1;id!=9;++id)session.data.users.emplace(id,std::make_unique<UserData>(&session,id));
 Peer peer(&session);peer.broadcast=broadcast;peer.setPendingRequestsCount(1,{1});session.changes().dispatch();
 std::vector<int>counts;std::vector<Ui::RequestsBarContent>out;rpl::lifetime life;
 Info::Profile::PendingRequestsCountValue(&peer)|rpl::on_next([&](int count){counts.push_back(count);},life);
 HistoryView::RequestsBarContentByPeer(&peer,24,false)|rpl::on_next([&](const auto&c){out.push_back(c);},life);
 drain();assert(!out.empty()&&out.back().nameFull=="full-1"&&out.back().isGroup==!broadcast);out.clear();
 peer.setPendingRequestsCount(1,{2});session.changes().dispatch();drain();
 assert(counts==std::vector<int>({1,1})&&peer.recentRequesters()==std::vector<int>({2}));
 assert(out.size()==1&&out.back().nameFull=="full-2"&&out.back().nameShort=="short-2"&&"same-count requester replacement must refresh the owning bar");
 assert(ids(out.back())==std::vector<int>({2})&&out.back().users.front().userpic==2);out.clear();
 peer.setPendingRequestsCount(1,{2});session.changes().dispatch();drain();assert(out.empty());
 session.changes().peerUpdated(&peer,UpdateFlag::PendingRequests);session.changes().dispatch();drain();assert(out.empty());
 peer.setPendingRequestsCount(3,{2,3,4});session.changes().dispatch();drain();assert(out.size()==1&&out.back().nameFull.empty());out.clear();
 peer.setPendingRequestsCount(3,{4,2,3});session.changes().dispatch();drain();assert(out.size()==1&&ids(out.back())==std::vector<int>({4,2,3}));out.clear();
 peer.setPendingRequestsCount(3,{5,2,3});session.changes().dispatch();drain();assert(out.size()==1&&ids(out.back())==std::vector<int>({5,2,3}));out.clear();
 peer.setPendingRequestsCount(7,{1,2,3,4});session.changes().dispatch();drain();assert(out.size()==1&&ids(out.back())==std::vector<int>({1,2,3}));out.clear();
 peer.setPendingRequestsCount(7,{1,2,3,5});session.changes().dispatch();drain();assert(out.empty());
 peer.setPendingRequestsCount(7,{1});session.changes().dispatch();drain();assert(out.empty());
 peer.setPendingRequestsCount(7,{1,2,3});session.changes().dispatch();drain();assert(out.size()==1&&ids(out.back())==std::vector<int>({1,2,3}));out.clear();
 peer.setPendingRequestsCount(0,{});session.changes().dispatch();drain();assert(out.size()==1&&out.back().count==0&&out.back().users.empty()&&out.back().nameFull.empty());out.clear();
 peer.setPendingRequestsCount(1,{1});session.changes().dispatch();peer.setPendingRequestsCount(1,{2});session.changes().dispatch();drain();
 assert(out.size()==1&&out.back().nameFull=="full-2"&&ids(out.back())==std::vector<int>({2}));out.clear();
 auto user=session.data.user(3);user->hasPhoto=true;user->loaded=false;
 peer.setPendingRequestsCount(1,{3});session.changes().dispatch();drain();assert(out.size()==1&&out.back().users.front().userpic==0);out.clear();
 user->loaded=true;session.downloaded.fire({});drain();assert(out.size()==1&&out.back().users.front().userpic==3);out.clear();
 peer.setPendingRequestsCount(1,{4});session.changes().dispatch();life.destroy();drain();assert(out.empty());
}
int main(){
 exercise<ChatData>();exercise<ChannelData>();exercise<ChannelData>(true);
 std::cout<<"Requests-bar extracted production PASS: Chat/channel/broadcast same-count replacement, reorder, capped/incomplete samples, dedupe, coalescing, downloader, lifetime. Real setters/profile/complete owner + GSL/range-v3/rpl; Qt/Changes/queue/guard doubles. NOT native Qt/render/transport QA\n";
}
''', extra=('-I'+str(ROOT/'Telegram/lib_rpl'),
            '-I'+str(ROOT/'Telegram/lib_base'),
            '-I'+str(ROOT/'Telegram/ThirdParty/GSL/include'),
            '-I'+str(ROOT/'Telegram/ThirdParty/range-v3/include')))


if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='luxury-requests-bar-') as directory:
        check(Path(directory))
