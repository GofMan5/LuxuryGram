"""Execute real watcher status/gap callbacks; explicit clocks/protocol/peers, no Qt/network/profile."""
from pathlib import Path
import tempfile
from library_regressions import cpp, function, source


def contact_statuses(root):
    text = source('Telegram/SourceFiles/apiwrap.cpp')
    callback = text.split('_contactsStatusesRequestId = request(MTPcontacts_GetStatuses(', 1)[1].split(')).done([=](const MTPVector<MTPContactStatus> &result) {', 1)[1].split('}).fail([this]', 1)[0]
    user = source('Telegram/SourceFiles/data/data_user.cpp')
    methods = user[user.index('bool UserData::updateServerLastseen('):user.index('// see Serialize::readPeer')]
    cpp(root, 'watcher_contact_statuses', r'''
#include "luxury/features/watch/presence_history.h"
#include <cassert>
#include <iostream>
#include <optional>
#include <vector>
using TimeId=int;using UserId=int;
namespace base::unixtime{int clock=100;int now(){return clock;}}
enum{mtpc_userStatusOnline,mtpc_userStatusOffline,mtpc_userStatusRecently};
struct MTPUserStatus{int kind,till;int type()const{return kind;}};
namespace Data{struct LastseenStatus{int till=0;bool isOnline(int now)const{return till>now;}int onlineTill()const{return till;}};struct PeerUpdate{enum class Flag{OnlineStatus};};}
Data::LastseenStatus LastseenFromMTP(MTPUserStatus s,Data::LastseenStatus){return{s.till};}
struct UserData{Data::LastseenStatus _lastseen,_serverLastseen;LuxuryFeatures::Watch::PresenceTracker _trackedPresence;bool updateServerLastseen(const MTPUserStatus&);void resetTrackedPresence();std::optional<int>trackedOnlineStart()const;Data::LastseenStatus serverLastseen()const;Data::LastseenStatus lastseen()const{return _lastseen;}bool updateLastseen(Data::LastseenStatus s){const bool changed=_lastseen.till!=s.till;_lastseen=s;return changed;}bool isSelf()const{return false;}bool isBot()const{return false;}bool isServiceUser()const{return false;}};
namespace LuxuryOnline{struct Transition{bool online;int at;};std::vector<Transition>recorded;bool TrackingAllowed(){return true;}void recordTransition(UserData*,bool online,int at){recorded.push_back({online,at});}}
''' + methods + r'''
struct MTPContactStatus{struct Payload{int id;MTPUserStatus status;int vuser_id()const{return id;}MTPUserStatus vstatus()const{return status;}}p;Payload data()const{return p;}};template<class T>struct MTPVector{std::vector<T>v;};
struct Owner{UserData user;UserData*userLoaded(int id){return id==1?&user:nullptr;}};
struct Changes{int calls=0;void peerUpdated(UserData*,Data::PeerUpdate::Flag){++calls;}};
struct Session{Owner owner;Changes c;Owner&data(){return owner;}Changes&changes(){return c;}};
struct ApiWrap{Session storage;Session*_session=&storage;int _contactsStatusesRequestId=77;Session&session(){return storage;}void done(const MTPVector<MTPContactStatus>&result){
''' + callback + r'''
}};
int main(){ApiWrap api;
api.done({{{{1,{mtpc_userStatusOnline,110}}},{{2,{mtpc_userStatusOnline,110}}}}});
assert(api._contactsStatusesRequestId==0&&api.storage.c.calls==1);
assert(LuxuryOnline::recorded.size()==1&&LuxuryOnline::recorded.back().online);
assert(api.storage.owner.user.trackedOnlineStart()==100);
base::unixtime::clock=105;api.done({{{{1,{mtpc_userStatusOnline,120}}}}});
assert(LuxuryOnline::recorded.size()==1&&api.storage.c.calls==2);
api.storage.owner.user.resetTrackedPresence();api.done({{{{1,{mtpc_userStatusOnline,120}}}}});
assert(LuxuryOnline::recorded.size()==2&&api.storage.c.calls==3&&api.storage.owner.user.trackedOnlineStart()==105);
base::unixtime::clock=111;api.done({{{{1,{mtpc_userStatusOffline,109}}}}});
assert(LuxuryOnline::recorded.size()==3&&!LuxuryOnline::recorded.back().online&&LuxuryOnline::recorded.back().at==109);
assert(!api.storage.owner.user.trackedOnlineStart());
std::cout<<"Actual contacts_GetStatuses callback + server recorder PASS: online/offline, renew dedup, stored server departure, missing peer, existing change flags\n";
}
''', extra=('-I', str(Path(__file__).resolve().parents[2] / 'Telegram/SourceFiles')))


def gaps(root):
    session = source('Telegram/SourceFiles/data/data_session.cpp')
    account = source('Telegram/SourceFiles/main/main_account.cpp')
    updates = source('Telegram/SourceFiles/api/api_updates.cpp')
    reset = function(session, 'void Session::resetTrackedPresence()')
    state = account.split('_mtp->setStateChangedHandler([=](MTP::ShiftedDcId dc, int32 state) {', 1)[1].split('\n\t});', 1)[0]
    mtp_reset = account.split('_mtp->setSessionResetHandler([=](MTP::ShiftedDcId shiftedDcId) {', 1)[1].split('\n\t});', 1)[0]
    methods = '\n'.join(function(updates, signature) for signature in (
        'void Updates::checkLastUpdate(', 'void Updates::mtpNewSessionCreated()'))
    cpp(root, 'watcher_gaps', r'''
#include "luxury/features/watch/presence_history.h"
#include <cassert>
#include <iostream>
#include <map>
#include <vector>
using int32=int;
namespace MTP {using ShiftedDcId=int;constexpr int ConnectedState=2;}
namespace crl {using time=long long;time clock=100;time now(){return clock;}}
constexpr auto kNoUpdatesAfterSleepTimeout=5,kNoUpdatesTimeout=30;
#define MTP_LOG(...) do{}while(false)
struct UserData;
struct PeerData {virtual~PeerData()=default;virtual UserData*asUser(){return nullptr;}};
struct Event {long long fakeId;bool online;int at;};
struct UserData:PeerData {LuxuryFeatures::Watch::PresenceTracker tracker;std::vector<Event>events;UserData*asUser()override{return this;}void resetTrackedPresence(){tracker.reset();}void observe(bool online,int at){if(auto transition=tracker.observe(online,true,at))events.push_back({static_cast<long long>(events.size()+1),*transition,at});}};
namespace Data {struct Session {std::map<int,PeerData*>_peers;void resetTrackedPresence();};
''' + reset + r'''
}
struct MainSession;
struct Updates {MainSession*p=nullptr;int _lastUpdateTime=0,_updatesSeq=7,pings=0,diffs=0;MainSession&session();void checkLastUpdate(bool);void mtpNewSessionCreated();void sendPing(){++pings;}void getDifference(){++diffs;}};
struct MainSession {Data::Session d;Updates u{this};Data::Session&data(){return d;}Updates&updates(){return u;}};
MainSession&Updates::session(){return*p;}
struct Mtp {int mainDcId(){return 1;}};
struct Account;
namespace Core {struct Proxy{int notifications=0;void connectionTypeChangesNotify(){++notifications;}};struct Settings{Proxy p;Proxy&proxy(){return p;}};struct Application{Settings s;int rotations=0,locks=0;Settings&settings(){return s;}void checkProxyRotation(Account*,int){++rotations;}void checkAutoLock(){++locks;}};Application app;Application&App(){return app;}}
struct Account {Mtp m;Mtp*_mtp=&m;MainSession*p=nullptr;MainSession*maybeSession(){return p;}void state(MTP::ShiftedDcId dc,int32 state){
''' + state + r'''
}void reset(MTP::ShiftedDcId shiftedDcId){
''' + mtp_reset + r'''
}};
''' + methods + r'''
int main(){MainSession session;UserData a,b;PeerData group;session.d._peers={{1,&a},{2,&b},{3,&group}};Account account;account.p=&session;
a.observe(true,10);b.observe(true,11);account.state(2,0);assert(a.tracker.start()==10&&b.tracker.start()==11);assert(Core::app.rotations==0);
account.state(1,MTP::ConnectedState);assert(a.tracker.start()==10&&Core::app.rotations==1);
account.state(1,0);assert(!a.tracker.start()&&!b.tracker.start());assert(a.events.size()==1&&b.events.size()==1);
a.observe(false,100);assert(a.events.size()==1);a.observe(true,101);a.observe(false,110);
auto paired=LuxuryFeatures::Watch::PairOnlineSessions(std::vector<Event>{a.events.rbegin(),a.events.rend()});assert(paired.size()==2&&!paired[0].end&&paired[1].start==101&&paired[1].end==110);
b.observe(true,120);account.reset(2);assert(b.tracker.start()==120&&session.u.diffs==0);account.reset(1);assert(!b.tracker.start()&&session.u.diffs==1);
a.observe(true,200);session.u.checkLastUpdate(false);assert(a.tracker.start()==200);session.u.checkLastUpdate(true);assert(a.tracker.start()==200);
a.observe(true,201);session.u.mtpNewSessionCreated();assert(!a.tracker.start()&&session.u._updatesSeq==0&&session.u.diffs==2);
account.p=nullptr;account.state(1,0);account.reset(1);assert(Core::app.rotations==3);
std::cout<<"Actual Session reset + MTProto state/reset + sleep/new-session callbacks PASS: media DC isolation, multi-user gap, no fabricated departure, resumed independent interval, absent account session\n";
}
'''.replace('long long(events.size()+1)', 'static_cast<long long>(events.size()+1)'), extra=('-I', str(Path(__file__).resolve().parents[2] / 'Telegram/SourceFiles')))


def all_accounts(root):
    method = function(source('Telegram/SourceFiles/core/application.cpp'), 'void Application::checkLocalTime()')
    cpp(root, 'watcher_all_accounts', r'''
#include <cassert>
#include <iostream>
#include <map>
namespace crl{bool adjusted=false;bool adjust_time(){return adjusted;}}
namespace base{struct Timer{static int adjusts;static void Adjust(){++adjusts;}};int Timer::adjusts=0;struct ConcurrentTimerEnvironment{static void Adjust(){}};namespace unixtime{void http_invalidate(){}}}
struct Updates{int checks=0,gaps=0;void checkLastUpdate(bool gap){++checks;gaps+=gap;}};
struct Data{int resets=0;void resetTrackedPresence(){++resets;}};struct Session{Updates u;Data d;Updates&updates(){return u;}Data&data(){return d;}};struct Account{Session*p;Session*maybeSession(){return p;}};
struct Domain{std::map<int,Account*>a;const auto&accounts(){return a;}};
struct Application{Domain*_domain;Session*primary;Session*maybePrimarySession(){return primary;}void checkLocalTime();};
''' + method + r'''
int main(){Session a,b;Account first{&a},second{&b},signedOut{nullptr};Domain domain{{{1,&first},{2,&second},{3,&signedOut}}};Application app{&domain,&a};
app.checkLocalTime();assert(a.u.checks==1&&b.u.checks==0&&a.d.resets==0&&b.d.resets==0&&base::Timer::adjusts==0);
crl::adjusted=true;app.checkLocalTime();assert(a.u.checks==2&&b.u.checks==0&&a.d.resets==1&&b.d.resets==1&&base::Timer::adjusts==1);
app.primary=nullptr;app.checkLocalTime();assert(a.d.resets==2&&b.d.resets==2&&a.u.checks==2&&b.u.checks==0);
std::cout<<"Actual application local-time caller PASS: ordinary and adjusted time across both account sessions, signed-out account skipped, primary-only network checks preserved\n";
}
''')


def storage_queue(root):
    text = source('Telegram/SourceFiles/luxury/data/messages_storage.cpp')
    methods = '\n'.join(function(text, s) for s in (
        'void recordTransition(', 'std::optional<int> lastOfflineAt('))
    cpp(root, 'watcher_storage_queue', r'''
#include <cassert>
#include <functional>
#include <iostream>
#include <map>
#include <optional>
#include <utility>
#include <vector>
using ID=long long;template<class T>using not_null=T;
struct Session{ID id;};struct PeerData{Session*s;ID dialog;struct{ID value;}id;Session&session(){return*s;}};struct UserData:PeerData{};
ID DatabaseUserId(const Session&s){return s.id;}ID getDialogIdFromPeer(PeerData*p){return p->dialog;}
struct OnlineEvent{ID userId=0,dialogId=0,peerId=0;bool online=false;int at=0;};
constexpr auto kMaxLastOfflineEntries=1000;std::map<std::pair<ID,ID>,int>LastOffline;
namespace LuxuryDatabase{std::vector<std::function<void()>>queue;std::vector<OnlineEvent>rows;void async(std::function<void()>f){queue.push_back(f);}void addOnlineEvent(OnlineEvent e){rows.push_back(e);}}
namespace LuxuryOnline{
''' + methods + r'''
}
int main(){using namespace LuxuryOnline;Session a{1},b{2};UserData user{{&a,3,{4}}},other{{&b,3,{4}}};
recordTransition(&user,false,10);recordTransition(&other,false,11);assert(lastOfflineAt(&user)==10&&lastOfflineAt(&other)==11);
recordTransition(&user,true,20);assert(!lastOfflineAt(&user)&&lastOfflineAt(&other)==11);
recordTransition(&user,false,21);assert(lastOfflineAt(&user)==21);assert(LuxuryDatabase::rows.empty());
user.dialog=99;user.id.value=999;
for(auto&f:LuxuryDatabase::queue)f();
const auto&rows=LuxuryDatabase::rows;assert(rows.size()==4&&rows[0].at==10&&rows[1].at==11&&rows[2].online&&rows[2].at==20&&!rows[3].online&&rows[3].at==21);
assert(rows[0].userId==1&&rows[1].userId==2&&rows[2].dialogId==3&&rows[3].peerId==4);
std::cout<<"Actual online recorder + lastOffline cache PASS: ordered queued rows, captured IDs, account isolation, renewed online invalidates old departure\n";
}
''')


def reactive_refresh(root):
    text = source('Telegram/SourceFiles/luxury/ui/watcher/watcher_box.cpp')
    start = text.index('\tconst auto refresh = [=] {', text.index('void FillWatcherBox('))
    end = text.index('\n\tbody->rebuilds(', start)
    subscription = text[start:end]
    cpp(root, 'watcher_reactive_refresh', r'''
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <utility>
#include <rpl/event_stream.h>
#include <rpl/filter.h>
namespace base::assertion{void log(const char*,const char*,int){std::abort();}}
#undef Expects
#define Expects(x) assert(x)
namespace Data{struct PeerUpdate{enum class Flag{OnlineStatus,Name};int peer;Flag flag;};}
struct Peer;
struct Changes{rpl::event_stream<Data::PeerUpdate>events;auto peerUpdates(Peer*,Data::PeerUpdate::Flag);};
struct Session{Changes c;Changes&changes(){return c;}};
struct Peer{int id;Session*s;Session&session(){return*s;}};
auto Changes::peerUpdates(Peer*p,Data::PeerUpdate::Flag flag){return events.events()|rpl::filter([=](Data::PeerUpdate e){return e.peer==p->id&&e.flag==flag;});}
struct Box{rpl::lifetime life;int scroll=42;auto&lifetime(){return life;}int scrollTop(){return scroll;}};struct Body{int reloads=0;void refresh(){++reloads;}};struct State{int scrollTop=0;};
void setup(Box*box,Body*body,Peer*peer,State*state){
''' + subscription + r'''
}
int main(){Session session;Peer peer{1,&session};Box box;Body body;State state;setup(&box,&body,&peer,&state);
session.c.events.fire({2,Data::PeerUpdate::Flag::OnlineStatus});session.c.events.fire({1,Data::PeerUpdate::Flag::Name});assert(body.reloads==0);
session.c.events.fire({1,Data::PeerUpdate::Flag::OnlineStatus});assert(body.reloads==1&&state.scrollTop==42);box.scroll=90;session.c.events.fire({1,Data::PeerUpdate::Flag::OnlineStatus});assert(body.reloads==2&&state.scrollTop==90);
box.life.destroy();session.c.events.fire({1,Data::PeerUpdate::Flag::OnlineStatus});assert(body.reloads==2);
std::cout<<"Actual watcher OnlineStatus refresh subscription PASS: correct peer/flag, captured scroll, real rpl, lifetime disconnection; database/rendering separate\n";
}
''', extra=[arg for p in ('Telegram/lib_rpl', 'Telegram/lib_base', 'Telegram/ThirdParty/GSL/include') for arg in ('-I', str(Path(__file__).resolve().parents[2] / p))])


def difference_gap(root):
    method = function(source('Telegram/SourceFiles/api/api_updates.cpp'), 'void Updates::getDifference()')
    prefix = method[:method.index('\n\tapi().request(')] + '\n}'
    cpp(root, 'watcher_difference_gap', r'''
#include <cassert>
#include <iostream>
struct Timer{int cancels=0;void cancel(){++cancels;}};struct Data{int resets=0;void resetTrackedPresence(){++resets;}};struct Session{Data d;Data&data(){return d;}};struct Waiter{bool requesting=false;void setRequesting(bool b){requesting=b;}};struct Queued{int clears=0;void clear(){++clears;}};
struct Updates{int _getDifferenceTimeByPts=1,_getDifferenceTimeAfterFail=1;Queued _bySeqUpdates;Timer _bySeqTimer,_noUpdatesTimer;Waiter _ptsWaiter;Session s;Session&session(){return s;}bool requestingDifference(){return _ptsWaiter.requesting;}void getDifference();};
''' + prefix + r'''
int main(){Updates u;u.getDifference();assert(u.s.d.resets==1&&u._ptsWaiter.requesting&&u._bySeqUpdates.clears==1&&u._noUpdatesTimer.cancels==1);u.getDifference();assert(u.s.d.resets==1&&u._bySeqUpdates.clears==1);u._ptsWaiter.requesting=false;u.getDifference();assert(u.s.d.resets==2);std::cout<<"Actual getDifference pre-request guard PASS: a newly started catch-up breaks presence continuity, already-running request is idempotent; request/transport not executed\n";}
''')


def check(root):
    contact_statuses(root)
    gaps(root)
    all_accounts(root)
    storage_queue(root)
    reactive_refresh(root)
    difference_gap(root)


if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='luxury-watcher-online-') as directory:
        check(Path(directory))
