"""Actual Updates::updateOnline and quit path; explicit clock/config/API/UI seams, no network."""
from pathlib import Path
import tempfile
from library_regressions import cpp, function, source


def check(root, case='all'):
    text = source('Telegram/SourceFiles/api/api_updates.cpp')
    methods = '\n'.join(function(text, s) for s in (
        'void Updates::updateOnline(crl::time lastNonIdleTime, bool gotOtherOffline)',
        'bool Updates::lastWasOnline() const', 'bool Updates::isQuitPrevent()'))
    cpp(root, 'status_update_' + case, r'''
#include <algorithm>
#include <cassert>
#include <functional>
#include <iostream>
#include <string>
#include <utility>
#include <vector>
using mtpRequestId=int;
#define Assert(x) assert(x)
#define LOG(...) do{}while(false)
namespace crl {using time=long long;time clock=100000;time now(){return clock;}template<class T,class F>void on_main(T*,F f){f();}}
namespace base {template<class T>T take(T&v){return std::exchange(v,T{});}namespace unixtime{int now(){return int(crl::clock/1000);}}}
struct Timer{int calls=0,last=-1;void callOnce(int v){++calls;last=v;}};
struct Flag{bool v=false;bool current()const{return v;}void operator=(bool b){v=b;}};
struct Config{int onlineUpdatePeriod=60000,offlineIdleTimeout=120000;};
struct MTPaccount_UpdateStatus{bool offline;explicit MTPaccount_UpdateStatus(bool b):offline(b){}};bool MTP_bool(bool b){return b;}
struct User{int updates=0;template<class T>void updateLastseen(T){++updates;}};
namespace Data{struct LastseenStatus{static int OnlineTill(int v){return v;}};struct PeerUpdate{enum class Flag{OnlineStatus};};}
struct Session;
struct Api{std::vector<bool>packets;int cancellations=0,drafts=0,next=1;struct Request{Api*a;bool offline;template<class F>Request&done(F){return*this;}template<class F>Request&fail(F){return*this;}int send(){a->packets.push_back(offline);return a->next++;}};Request request(MTPaccount_UpdateStatus s){return{this,s.offline};}struct Cancel{Api*a;void cancel(){++a->cancellations;}};Cancel request(int){return{this};}void saveCurrentDraftToCloud(){++drafts;}};
struct Session{Config config;Api sender;User self;struct Changes{void peerUpdated(User*,Data::PeerUpdate::Flag){}}changesData;struct Owner{void maybeStopWatchForOffline(User*){}}owner;Config&serverConfig(){return config;}User*user(){return&self;}Changes&changes(){return changesData;}Owner&data(){return owner;}};
namespace Core{struct AppType{bool active=true,quitting=false;crl::time last=100000;int finished=0;crl::time lastNonIdleTime(){return last;}void checkAutoLock(crl::time){}bool hasActiveWindow(Session*){return active;}void quitPreventFinished(){++finished;}}app;AppType&App(){return app;}bool Quitting(){return app.quitting;}}
struct Ghost{bool send=true;bool sendOnlinePackets()const{return send;}};namespace LuxurySettings{Ghost value;Ghost&ghost(Session*){return value;}}
struct Updates{Session storage;Session*_session=&storage;int _onlineRequest=0;Timer _idleFinishTimer,_onlineTimer;crl::time _lastSetOnline=0;bool _lastWasOnline=false,_lastStatusWasOnline=false;Flag _isIdle;Session&session(){return storage;}Api&api(){return storage.sender;}bool isIdle(){return _isIdle.current();}void updateOnline(crl::time t=0){updateOnline(t,false);}void updateOnline(crl::time,bool);bool lastWasOnline()const;bool isQuitPrevent();};
''' + methods + r'''
int main(){const std::string which="CASE";
if(which=="ghost"||which=="all"){
 Updates u;LuxurySettings::value.send=false;u.updateOnline();assert(u.api().packets.empty());for(int i=0;i<30;++i){crl::clock+=100;u.updateOnline();}assert(u.api().packets.empty());assert(!u.isQuitPrevent());
 LuxurySettings::value.send=true;u.updateOnline();assert(u.api().packets==std::vector<bool>{false});LuxurySettings::value.send=false;u.updateOnline();assert((u.api().packets==std::vector<bool>{false,true}));for(int i=0;i<30;++i){crl::clock+=100;u.updateOnline();}assert(u.api().packets.size()==2&&u.lastWasOnline());Core::app.active=false;u.updateOnline();assert(!u.lastWasOnline()&&u.api().packets.size()==2);Core::app.active=true;
}
if(which=="normal"||which=="all"){
 LuxurySettings::value.send=true;crl::clock=100000;Core::app.last=100000;Core::app.active=true;Updates u;u.updateOnline();assert(u.api().packets==std::vector<bool>{false});u.updateOnline();assert(u.api().packets.size()==1);crl::clock+=59999;u.updateOnline();assert(u.api().packets.size()==1);++crl::clock;u.updateOnline();assert(u.api().packets.size()==2);Core::app.last=crl::clock;u.updateOnline(0,true);assert(u.api().packets.size()==3);
 crl::clock=Core::app.last+u.storage.config.offlineIdleTimeout;u.updateOnline();assert(u.api().packets.back()&&u.isIdle());const auto n=u.api().packets.size();u.updateOnline();assert(u.api().packets.size()==n);Core::app.active=false;u.updateOnline();assert(!u.lastWasOnline()&&u.api().packets.size()==n);Core::app.active=true;
 Core::app.last=crl::clock;u.updateOnline();assert(!u.api().packets.back());Core::app.active=false;Core::app.quitting=true;assert(u.isQuitPrevent());assert(u.api().packets.back());assert(!u.isQuitPrevent());Core::app.quitting=false;Core::app.active=true;
}
std::cout<<"Actual Updates::updateOnline/quit PASS: "<<which<<"; Ghost repeated-call packet counts, online/offline transitions, renewal/idle, local activity separate from last transmitted status, no RPC/network\n";
}
'''.replace('"CASE"', '"' + case + '"'))


if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument('--case', choices=('all', 'ghost', 'normal'), default='all')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='luxury-status-updates-') as directory:
        check(Path(directory), args.case)
