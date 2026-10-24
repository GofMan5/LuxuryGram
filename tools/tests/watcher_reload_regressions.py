"""Actual WatcherBody async reload/tick/rebuild entry; explicit queue, DB, widgets and lifetime doubles."""
from pathlib import Path
import tempfile
from library_regressions import ROOT, cpp, function, source


CASES = ('healthy', 'reload-counts', 'reload-reloads', 'reload-rebuild',
         'tick-counts', 'tick-reloads', 'tick-rebuild', 'rebuild-cancel', 'tick-reentry')


def check(root, case='healthy'):
    text = source('Telegram/SourceFiles/luxury/ui/watcher/watcher_box.cpp')
    methods = '\n'.join(function(text, signature) for signature in (
        'void WatcherBody::reload()', 'void WatcherBody::tick()'))
    rebuild = function(text, 'void WatcherBody::rebuild()')
    entry = rebuild[:rebuild.index('\n\t// Rebuild swaps')] + '\n\tfinishRebuild();\n}'
    cpp(root, 'watcher_reload_' + case.replace('-', '_'), r'''
#include "luxury/features/watch/presence_history.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <functional>
#include <iostream>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>
using ID=long long;using uint64=unsigned long long;template<class T>using Fn=std::function<T>;
using LuxuryFeatures::Watch::PairOnlineSessions;
struct Record{ID fakeId=0;bool online=false;int at=0;};using OnlineEvent=Record;using WatchEvent=Record;using LuxuryMessageBase=Record;
struct Owner{std::shared_ptr<bool>alive=std::make_shared<bool>(true);virtual~Owner(){*alive=false;}void check()const{assert(*alive);}};
namespace base{struct Weak{std::weak_ptr<bool>p;operator bool()const{auto a=p.lock();return a&&*a;}};template<class T>Weak make_weak(T*p){return{p->alive};}}
namespace crl{std::vector<Fn<void()>>tasks;template<class F>void on_main(F f){tasks.push_back(f);}void drain(){auto work=std::exchange(tasks,{});for(auto&f:work)f();}}
struct QApplication{static int pressed;static int mouseButtons(){return pressed;}};int QApplication::pressed=0;
struct Session{ID uniqueId(){return 1;}};struct Peer{Session s;Session&session(){return s;}};ID getDialogIdFromPeer(Peer*){return 2;}
namespace LuxuryDatabase{std::vector<Fn<void()>>tasks;std::vector<Record>online,watch;int reads=0;void async(Fn<void()>f){tasks.push_back(f);}void drain(){auto work=std::exchange(tasks,{});for(auto&f:work)f();}auto getOnlineEvents(ID,ID,int){++reads;return online;}auto getWatchEvents(ID,ID,int){return watch;}int countDeletedMessages(ID,ID){return 0;}int countEditedMessagesForDialog(ID,ID){return 0;}}
namespace LuxuryMessages{std::vector<Record>getDeletedMessages(ID,ID,int,int,int,int){return{};}std::vector<Record>getEditedMessagesForDialog(ID,ID,int){return{};}}
constexpr int kHistoryReadLimit=200;
template<class T>struct Stream{Owner*owner;int count=0;Fn<void(T)>callback;void fire(T v){owner->check();++count;if(callback){auto f=callback;f(v);}}void fire_copy(T v){fire(v);}};
struct SessionCard{Owner*owner;LuxuryFeatures::Watch::OnlineSession value;const auto&session(){return value;}int h=10,ticks=0;Fn<void()>onActive,onTick;int height(){owner->check();return h;}void setActive(bool){owner->check();if(onActive){auto f=onActive;f();}}void tick(){owner->check();++ticks;if(onTick){auto f=onTick;f();}}};
struct WatcherBody:Owner{Peer peer;Peer*_peer=&peer;bool _loading=false,_reloadAgain=false,_rebuildPending=false;uint64 _rebuildGeneration=0;int _eventEarlier=0,rebuildCount=0,layouts=0,w=274;Fn<void()>onRebuild;
std::vector<Record>_events,_watches,_deleted,_edits;std::vector<LuxuryFeatures::Watch::OnlineSession>_sessions;std::vector<SessionCard*>_sessionCards,_eventCards;Stream<std::array<int,3>>_counts{this};Stream<bool>_reloads{this};Stream<int>_rebuilds{this};
std::array<int,3>counts(){check();return{int(_sessions.size()),int(_sessions.size()),0};}int width(){check();return w;}void resizeToWidth(int){check();++layouts;}bool sessionActive(const auto&)const{return true;}void finishRebuild(){check();++rebuildCount;_sessionCards.clear();if(onRebuild){auto f=onRebuild;f();}}void reload();void tick();void rebuild();void invalidate(){*alive=false;}};
''' + methods + '\n' + entry + r'''
void complete(){LuxuryDatabase::drain();crl::drain();}
int main(){const std::string which="CASE";WatcherBody body;LuxuryDatabase::online={{1,true,100}};
if(which=="healthy"){
 body.reload();body.reload();body.reload();assert(LuxuryDatabase::tasks.size()==1&&body._reloadAgain);complete();assert(body.rebuildCount==1&&body._counts.count==1&&body._reloads.count==1&&LuxuryDatabase::tasks.size()==1);complete();assert(!body._loading&&!body._reloadAgain&&body.rebuildCount==2);
 body._sessionCards.push_back(new SessionCard{&body});auto card=body._sessionCards.back();LuxuryDatabase::online={{2,false,110},{1,true,100}};QApplication::pressed=1;body.reload();complete();assert(body._rebuildPending&&body.rebuildCount==2&&body._sessions.size()==1&&body._sessions[0].end==110);QApplication::pressed=0;body.tick();assert(!body._rebuildPending&&body.rebuildCount==3);complete();delete card;
 auto gone=std::make_unique<WatcherBody>();gone->reload();gone.reset();complete();
}else if(which=="rebuild-cancel"){
 body._rebuildPending=true;body.rebuild();assert(!body._rebuildPending);const auto count=body.rebuildCount;body.tick();assert(body.rebuildCount==count);
}else if(which=="tick-reentry"){
 SessionCard card{&body};body._sessionCards={&card};card.onActive=[&]{body.rebuild();};body.tick();assert(body.rebuildCount==1&&card.ticks==0);
}else{
 const bool tick=which.starts_with("tick-");
 if(which.ends_with("counts"))body._counts.callback=[&](auto){body.invalidate();};
 if(which.ends_with("reloads"))body._reloads.callback=[&](bool){body.invalidate();};
 if(which.ends_with("rebuild"))body._rebuilds.callback=[&](int){body.invalidate();};
 if(tick){body._rebuildPending=true;body.tick();}else{body._reloadAgain=true;body.reload();complete();}
 assert(!*body.alive);
}
std::cout<<"Actual WatcherBody reload/tick/rebuild entry PASS: "<<which<<"; queue/DB/widgets/lifetime seams, no native Qt or SQLite\n";
}
'''.replace('"CASE"', '"' + case + '"').replace('bool sessionActive(const auto&)const{return true;}', 'bool sessionActive(const LuxuryFeatures::Watch::OnlineSession&)const{return true;}').replace('struct SessionCard{Owner*owner;', 'struct SessionCard{Owner*owner;LuxuryFeatures::Watch::OnlineSession value;const auto&session(){return value;}'), extra=('-I', str(ROOT / 'Telegram/SourceFiles')))


def cards(root):
    text = source('Telegram/SourceFiles/luxury/ui/watcher/watcher_box.cpp')
    methods = '\n'.join(function(text, signature) for signature in (
        'void SessionCard::setActive(', 'void SessionCard::tick()'))
    cpp(root, 'watcher_session_card', r'''
#include <algorithm>
#include <cassert>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
using QString=std::string;using qint64=long long;QString operator""_q(const char16_t*s,size_t n){QString r;for(size_t i=0;i<n;++i)r+=char(s[i]);return r;}
namespace base{struct Weak{std::weak_ptr<bool>p;operator bool()const{auto a=p.lock();return a&&*a;}};template<class T>Weak make_weak(T*p){return{p->alive};}namespace unixtime{qint64 clock=130;qint64 now(){return clock;}}}
namespace Ui{QString FormatDurationText(qint64 s){return std::to_string(s);}}
namespace tr{constexpr int now=0;QString luxury_OnlineHistoryOpen(int){return "Online";}QString luxury_WatcherEndUnobserved(int){return "Unknown";}}
struct SessionCard{std::shared_ptr<bool>alive=std::make_shared<bool>(true);bool _active=true;struct{std::optional<int>start=100;}_session;QString _startLabel="Start",_endLabel="End",_startValue="100",_endValue="Online",_durationText;struct{void start(){}void stop(){}}_pulse;int layouts=0,updates=0,resizes=0,names=0;std::function<void()>onName,onTooltip,onResize;void check(){assert(*alive);}int width(){check();return 274;}void computeLayout(int){check();++layouts;}void update(){check();++updates;}void resizeToWidth(int){check();++resizes;if(onResize){auto f=onResize;f();}}void setToolTip(const QString&){check();if(onTooltip){auto f=onTooltip;f();}}void updateAccessibleName(){check();++names;if(onName){auto f=onName;f();}}void invalidate(){*alive=false;}void tick();void setActive(bool);};
''' + methods + r'''
int main(){
 {SessionCard c;c.onName=[&]{assert(c.layouts>0&&c.updates>0&&c._durationText=="30");c.invalidate();};c.tick();assert(!*c.alive);}
 {SessionCard c;c._active=false;c.onName=[&]{c.invalidate();};c.setActive(true);assert(!*c.alive);}
 {SessionCard c;c._durationText="30";c.onName=[&]{assert(!c._active&&c._durationText.empty()&&c.resizes==1);c.invalidate();};c.setActive(false);assert(!*c.alive);}
 {SessionCard c;c.onTooltip=[&]{c.invalidate();};c.setActive(false);assert(!*c.alive&&c.resizes==0);}
 {SessionCard c;c.onResize=[&]{c.invalidate();};c.setActive(false);assert(!*c.alive);}
 {SessionCard c;c.tick();const auto before=c.names;c.tick();assert(c.names==before);c._session.start.reset();c.tick();assert(c.names==before);}
 std::cout<<"Actual SessionCard tick/setActive PASS: committed duration/layout before name event, no post-callback access, active/inactive/tooltips/resize boundaries, equal/no-start no-op; explicit Qt/AT doubles\n";
}
''')


if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument('--case', choices=CASES)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='luxury-watcher-reload-') as directory:
        for case in ((args.case,) if args.case else CASES):
            check(Path(directory), case)
        if args.case is None:
            cards(Path(directory))
