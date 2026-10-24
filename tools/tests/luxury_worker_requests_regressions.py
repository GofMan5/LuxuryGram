"""Execute the full production LuxuryWorker namespace with counted asynchronous RPC/timer/account seams."""
from pathlib import Path
import tempfile
from library_regressions import cpp, source


def check(root, case='all'):
    text = source('Telegram/SourceFiles/luxury/luxury_worker.cpp')
    worker = text[text.index('namespace LuxuryWorker {'):]
    cpp(root, 'luxury_worker_requests_' + case, r'''
#include <cassert>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
using uint64=unsigned long long;using mtpRequestId=int;
#define DEBUG_LOG(...) do{}while(false)
template<class T>using not_null=T;
namespace crl{using time=long long;}
namespace base{template<class T>T take(T&v){return std::exchange(v,T{});}namespace unixtime{int now(){return 10;}}struct Timer{bool active=false,repeat=false;int armed=0,canceled=0;std::function<void()>cb;template<class F>Timer(F f):cb(f){}void callEach(crl::time){active=true;repeat=true;++armed;}void callOnce(crl::time){active=true;repeat=false;++armed;}bool isActive(){return active;}void cancel(){active=false;++canceled;}void fire(){if(!active)return;active=repeat;cb();}};}
struct MTPBool{};namespace MTP{struct Error{};}
struct MTPaccount_UpdateStatus{bool offline;explicit MTPaccount_UpdateStatus(bool b):offline(b){}};bool MTP_bool(bool b){return b;}
struct Ghost{bool enabled=false;bool sendOfflinePacketAfterOnline()const{return enabled;}};
struct Api{struct Pending{std::function<void(const MTPBool&,mtpRequestId)>done;std::function<void(const MTP::Error&,mtpRequestId)>fail;};static int next;int sent=0,cancels=0;std::map<int,Pending>requests;struct Builder{Api*a;Pending pending;template<class F>Builder&done(F f){pending.done=f;return*this;}template<class F>Builder&fail(F f){pending.fail=f;return*this;}int send(){++a->sent;int id=++Api::next;a->requests.emplace(id,std::move(pending));return id;}};Builder request(MTPaccount_UpdateStatus s){assert(s.offline);return{this,{}};}struct Cancel{Api*a;int id;void cancel(){if(a->requests.erase(id))++a->cancels;}};Cancel request(int id){return{this,id};}void complete(int id){auto p=std::move(requests.at(id));requests.erase(id);if(p.done)p.done({},id);}void fail(int id){auto p=std::move(requests.at(id));requests.erase(id);if(p.fail)p.fail({},id);}};int Api::next=0;
namespace Main{struct Session{uint64 id;Ghost setting;struct User{struct Status{bool online=false;bool isOnline(int)const{return online;}}status;auto lastseen(){return status;}}self;Api sender;explicit Session(uint64 v):id(v){}uint64 uniqueId(){return id;}User*user(){return&self;}Api&api(){return sender;}};struct Account{std::unique_ptr<Session>s;Session*maybeSession(){return s.get();}};struct Domain{bool ready=true;std::map<int,std::unique_ptr<Account>>values;bool started(){return ready;}const auto&accounts(){return values;}};struct Application{Domain d;bool launched=true,quitting=false;Domain&domain(){return d;}};}
namespace Core{Main::Application app;bool IsAppLaunched(){return app.launched;}Main::Application&App(){return app;}bool Quitting(){return app.quitting;}}
namespace LuxurySettings{const Ghost&ghost(Main::Session*s){return s->setting;}}
namespace LuxuryWorker{void wake();void markAsOnline(not_null<Main::Session*>);}
''' + worker + r'''
Main::Session*add(int key,uint64 id){auto a=std::make_unique<Main::Account>();a->s=std::make_unique<Main::Session>(id);auto p=a->s.get();Core::app.d.values[key]=std::move(a);return p;}
void tick(){LuxuryWorker::workerTimer().fire();}
int main(){const std::string which="CASE";using namespace LuxuryWorker;
if(which=="idle"||which=="all"){
 auto s=add(0,1);initialize();tick();assert(s->sender.sent==0&&!workerTimer().isActive());markAsOnline(s);tick();assert(s->sender.sent==0);Core::app.d.values.clear();wake();tick();
}
if(which=="enable"||which=="all"){
 auto s=add(0,2);s->self.status.online=true;s->setting.enabled=true;initialize();tick();assert(s->sender.sent==0);markAsOnline(s);tick();assert(s->sender.sent==1);for(int i=0;i<10;++i){wake();tick();}assert(s->sender.sent==1);s->sender.complete(s->sender.requests.begin()->first);wake();tick();assert(s->sender.sent==1);
 s->setting.enabled=false;wake();tick();assert(s->sender.sent==1&&s->sender.requests.size()==0);s->setting.enabled=true;markAsOnline(s);wake();tick();assert(s->sender.sent==2);Core::app.d.values.clear();wake();tick();
}
if(which=="inflight"||which=="all"){
 auto s=add(0,3);s->setting.enabled=true;initialize();tick();assert(s->sender.sent==0);for(int i=0;i<20;++i)markAsOnline(s);tick();assert(s->sender.sent==1&&s->sender.requests.size()==1);int first=s->sender.requests.begin()->first;
 for(int i=0;i<20;++i){markAsOnline(s);tick();}assert(s->sender.sent==1&&s->sender.requests.size()==1); // A parked FLOOD_WAIT stays in flight.
 auto old=s->sender.requests.at(first).done;s->sender.complete(first);tick();assert(s->sender.sent==2&&s->sender.requests.size()==1);int second=s->sender.requests.begin()->first;if(old)old({},first);markAsOnline(s);tick();assert(s->sender.sent==2);s->sender.complete(second);tick();assert(s->sender.sent==3);s->sender.fail(s->sender.requests.begin()->first);wake();tick();assert(s->sender.sent==3&&s->sender.requests.empty());
 Core::app.d.values.clear();wake();tick();
}
if(which=="accounts"||which=="all"){
 auto a=add(0,4),b=add(1,5);a->setting.enabled=true;initialize();tick();markAsOnline(a);markAsOnline(b);tick();assert(a->sender.sent==1&&b->sender.sent==0);int request=a->sender.requests.begin()->first;auto late=a->sender.requests.at(request).done;
 a->setting.enabled=false;wake();tick();assert(a->sender.cancels==1&&a->sender.requests.empty());a->setting.enabled=true;markAsOnline(a);wake();tick();assert(a->sender.sent==2&&a->sender.requests.size()==1);if(late)late({},request);markAsOnline(a);tick();assert(a->sender.sent==2);
 Core::app.d.values.erase(0);wake();tick();auto renewed=add(0,4);renewed->setting.enabled=true;markAsOnline(renewed);tick();assert(renewed->sender.sent==1);if(late)late({},request);markAsOnline(renewed);tick();assert(renewed->sender.sent==1);assert(b->sender.sent==0);
 Core::app.d.values.clear();wake();tick();
}
std::cout<<"Entire production LuxuryWorker PASS: "<<which<<"; no stale-self repeat, one in-flight request per account including server-parked wait, coalesced new events, enable/startup privacy seed, terminal failure/disable/logout/late-response guards; explicit RPC/timer/accounts, no network\n";
}
'''.replace('"CASE"', '"' + case + '"'))


if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument('--case', choices=('all', 'idle', 'enable', 'inflight', 'accounts'), default='all')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='luxury-worker-requests-') as directory:
        for case in ((args.case,) if args.case != 'all' else ('idle','enable','inflight','accounts')):
            check(Path(directory), case)
