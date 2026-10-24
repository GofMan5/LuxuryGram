"""Exercise actual story snapshot/restore and tray owner binding with real rpl/base weak.
Qt widgets/application/session/profile methods are explicit doubles; no profiles/UI.
"""
from pathlib import Path
import os, shutil, subprocess, tempfile
root = Path(__file__).resolve().parents[2]
def read(p): return (root / 'Telegram/SourceFiles' / p).read_text('utf-8')
state = read('luxury/luxury_state.cpp')
state_defs = state[state.index('struct StoryGhostState {'):state.index('\n} // namespace',state.index('struct StoryGhostState {'))]
state_funcs = state[state.index('void setDisableGhostModeOnStoryClose('):state.rindex('\n}')]
tray = read('tray.cpp')
binding = tray[tray.index('\t\tauto ghostActiveChanges = '):tray.index('\n\t\tauto turnGhostModeText',tray.index('\t\tauto ghostActiveChanges = '))]
settings = read('luxury/luxury_settings.cpp')
def ghost_method(name):
    start = settings.index('void GhostModeAccountSettings::' + name + '(')
    end = settings.index('\n}',start) + 2
    return settings[start:end]
startup = settings[settings.index('void LuxurySettings::applyStartupGhost('):settings.index('GhostModeAccountSettings &LuxurySettings::ghostProfile(')]
load = settings[settings.index('void LuxurySettings::load()'):settings.index('void LuxurySettings::save()')]
assert load.index('const auto startupGuard = gsl::finally') < load.index('if (!file.open(')
assert 'Core::App().tray().updateMenuText();' in settings[settings.index('bool LuxurySettings::reset()'):settings.index('GhostModeAccountSettings &LuxurySettings::ghost(')]
assert 'ghostProfile(getInstance().getOverriddenGhostUserId(userId))' in settings
assert 'cState->lockChanges.fire({});' in read('luxury/ui/settings/settings_luxury_utils.cpp')[read('luxury/ui/settings/settings_luxury_utils.cpp').index('.refresh = [cState]') :]
assert 'setGhostModeEnabled(true);\n\t\t\t\t\tLuxuryState::setDisable' not in read('window/window_controller.cpp')
weak_header = (root / 'Telegram/lib_base/base/weak_ptr.h').read_text('utf-8').replace('#include "base/basic_types.h"','#include <gsl/gsl>')
cpp = r'''
#include <array>
#include <cassert>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <utility>
template <typename... Args> constexpr bool false_t(Args&&...) { return false; }
#include <rpl/event_stream.h>
#include <rpl/variable.h>
#include <rpl/combine.h>
#include <rpl/flatten_latest.h>
WEAK_HEADER_HERE
namespace base::assertion { void log(const char*,const char*,int) { std::abort(); } }
using uint64 = unsigned long long;
namespace base { template<class T> T take(T &v) { return std::exchange(v,T{}); } }
namespace Main { struct Session : base::has_weak_ptr { uint64 id; explicit Session(uint64 id) : id(id) {} uint64 uniqueId() { return id; } }; }
struct GhostModeAccountSettings : base::has_weak_ptr {
 bool _sendReadMessages = true, _sendReadStories = true, _sendOnlinePackets = true, _sendUploadProgress = true, _sendOfflinePacketAfterOnline = false;
 bool _startupGhostApplied = false; std::array<bool,5> locked = {}; std::array<uint64,5> _privacyRevisions = {}; rpl::variable<bool> active = false;
 const auto &privacyRevisions() const { return _privacyRevisions; }
 bool sendReadMessages() const { return _sendReadMessages; } bool sendReadStories() const { return _sendReadStories; } bool sendOnlinePackets() const { return _sendOnlinePackets; } bool sendUploadProgress() const { return _sendUploadProgress; } bool sendOfflinePacketAfterOnline() const { return _sendOfflinePacketAfterOnline; }
 bool sendReadMessagesLocked() const { return locked[0]; } bool sendReadStoriesLocked() const { return locked[1]; } bool sendOnlinePacketsLocked() const { return locked[2]; } bool sendUploadProgressLocked() const { return locked[3]; } bool sendOfflinePacketAfterOnlineLocked() const { return locked[4]; }
 void update() { active = !_sendReadMessages && !_sendReadStories && !_sendOnlinePackets && !_sendUploadProgress && _sendOfflinePacketAfterOnline; }
 void setSendReadMessages(bool); void setSendReadStories(bool); void setSendOnlinePackets(bool); void setSendUploadProgress(bool); void setSendOfflinePacketAfterOnline(bool); void setGhostModeEnabled(bool);
 auto ghostModeActiveValue() const { return active.value(); }
};
inline bool cli = false; bool cGhost() { return cli; }
class LuxurySettings {
public:
 rpl::variable<bool> global = false; std::map<uint64,std::unique_ptr<GhostModeAccountSettings>> profiles; Main::Session *current = nullptr;
 static LuxurySettings &getInstance() { static LuxurySettings s; return s; } bool useGlobalGhostMode() const { return global.current(); } auto useGlobalGhostModeValue() { return global.value(); }
 static GhostModeAccountSettings &ghostProfile(uint64 id) { auto &v = getInstance().profiles[id]; if(!v) v = std::make_unique<GhostModeAccountSettings>(); return *v; }
 static GhostModeAccountSettings &ghost() { auto &s = getInstance(); return ghostProfile(s.global.current() || !s.current ? 0 : s.current->uniqueId()); }
 static void applyStartupGhost(GhostModeAccountSettings&);
 static void save() { for(auto &[id,profile] : getInstance().profiles) profile->update(); }
};
namespace LuxuryWorker { void wake() {} void markAsOnline(Main::Session*) {} }
namespace Core { struct Window { Main::Session *maybeSession() { return nullptr; } }; struct StubApp { Window *activeWindow() { return nullptr; } } stub; StubApp &AppForGhost() { return stub; } }
SETTERS_HERE
STARTUP_HERE
namespace LuxuryState { namespace {
STATE_DEFS_HERE
}
STATE_FUNCS_HERE
}
namespace Core { struct Domain { rpl::variable<Main::Session*> current = nullptr; auto activeSessionValue() { return current.value(); } }; struct AppType { Domain d; Domain &domain() { return d; } } app; AppType &App() { return app; } }
struct Tray { rpl::event_stream<> _textUpdates; rpl::lifetime lifetime; bool shownActive = false; void bind() {
BINDING_HERE
 std::move(ghostActiveChanges) | rpl::on_next([=](bool active) { shownActive = active; }, lifetime);
} };
int main() {
 auto &s = LuxurySettings::getInstance(); Main::Session a(10),b(20);
 auto &ga = LuxurySettings::ghostProfile(10); ga.setSendReadMessages(false); ga.setSendUploadProgress(false); ga.setSendOfflinePacketAfterOnline(true);
 LuxuryState::setDisableGhostModeOnStoryClose(&a); assert(ga.active.current()); LuxuryState::disableGhostModeOnStoryClose(&b); assert(ga.active.current()); LuxuryState::disableGhostModeOnStoryClose(&a);
 assert(!ga.sendReadMessages() && ga.sendReadStories() && ga.sendOnlinePackets() && !ga.sendUploadProgress() && ga.sendOfflinePacketAfterOnline());
 LuxuryState::setDisableGhostModeOnStoryClose(&a); ga.setSendOnlinePackets(true); ga.locked[1] = true; s.global = true; LuxuryState::disableGhostModeOnStoryClose(&a);
 assert(ga.sendOnlinePackets() && !ga.sendReadStories() && !LuxurySettings::ghostProfile(0).active.current()); ga.locked[1] = false;
 s.global = false; LuxuryState::setDisableGhostModeOnStoryClose(&a); auto beforeB = LuxurySettings::ghostProfile(20).sendReadStories(); LuxuryState::setDisableGhostModeOnStoryClose(&b); assert(ga.sendOnlinePackets()); LuxuryState::disableGhostModeOnStoryClose(&a); assert(LuxurySettings::ghostProfile(20).active.current()); LuxuryState::disableGhostModeOnStoryClose(&b); assert(LuxurySettings::ghostProfile(20).sendReadStories() == beforeB);
 ga.setGhostModeEnabled(false); LuxuryState::setDisableGhostModeOnStoryClose(&a); ga.setGhostModeEnabled(false); ga.setGhostModeEnabled(true); LuxuryState::disableGhostModeOnStoryClose(&a); assert(ga.active.current());
 ga.setGhostModeEnabled(false); LuxuryState::setDisableGhostModeOnStoryClose(&a); ga.setGhostModeEnabled(true); LuxuryState::disableGhostModeOnStoryClose(&a); assert(ga.active.current());
 LuxuryState::setDisableGhostModeOnStoryClose(&a); s.profiles.erase(10); auto &replacement = LuxurySettings::ghostProfile(10); LuxuryState::disableGhostModeOnStoryClose(&a); assert(replacement.sendReadMessages());
 { auto dying = std::make_unique<Main::Session>(30); LuxuryState::setDisableGhostModeOnStoryClose(dying.get()); dying.reset(); LuxuryState::disableGhostModeOnStoryClose(nullptr); assert(!LuxuryState::storyGhost); }
 s.current = &a; Core::app.d.current = &a; replacement.setGhostModeEnabled(true); Tray tray; tray.bind(); assert(tray.shownActive);
 s.current = &b; Core::app.d.current = &b; assert(!tray.shownActive); replacement.setGhostModeEnabled(false); assert(!tray.shownActive); LuxurySettings::ghostProfile(20).setGhostModeEnabled(true); assert(tray.shownActive);
 s.profiles.clear(); tray._textUpdates.fire({}); assert(!tray.shownActive); LuxurySettings::ghostProfile(20).setGhostModeEnabled(true); assert(tray.shownActive);
 s.global = true; assert(!tray.shownActive); LuxurySettings::ghostProfile(0).setGhostModeEnabled(true); assert(tray.shownActive); s.current = &a; tray._textUpdates.fire({}); assert(tray.shownActive);
 cli = true; GhostModeAccountSettings startup; LuxurySettings::applyStartupGhost(startup); assert(!startup.sendReadMessages() && !startup.sendReadStories() && !startup.sendOnlinePackets() && !startup.sendUploadProgress() && startup.sendOfflinePacketAfterOnline()); startup.setSendReadMessages(true); LuxurySettings::applyStartupGhost(startup); assert(startup.sendReadMessages());
 GhostModeAccountSettings second; LuxurySettings::applyStartupGhost(second); assert(!second.sendReadMessages());
 std::cout << "PASS: actual story restore/profile lifetime and tray owner rebind with real rpl/weak; startup flags plus load-path source guard; session/profile doubles\n";
}
'''
setters = '\n'.join(ghost_method(name) for name in ['setSendReadMessages','setSendReadStories','setSendOnlinePackets','setSendUploadProgress','setSendOfflinePacketAfterOnline','setGhostModeEnabled']).replace('Core::App()', 'Core::AppForGhost()')
for field in ['_sendReadMessages','_sendReadStories','_sendOnlinePackets','_sendUploadProgress','_sendOfflinePacketAfterOnline']:
    setters = setters.replace(field + '.current()',field)
for i,field in enumerate(['_sendReadMessagesLocked','_sendReadStoriesLocked','_sendOnlinePacketsLocked','_sendUploadProgressLocked','_sendOfflinePacketAfterOnlineLocked']):
    setters = setters.replace(field + '.current()',f'locked[{i}]')
for marker,value in {'SETTERS_HERE':setters,'WEAK_HEADER_HERE':weak_header,'STARTUP_HERE':startup,'STATE_DEFS_HERE':state_defs,'STATE_FUNCS_HERE':state_funcs,'BINDING_HERE':binding}.items(): cpp = cpp.replace(marker,value)
with tempfile.TemporaryDirectory(prefix='luxury-ghost-owner-') as directory:
    work = Path(directory); test = work / 'owners.cpp'; test.write_text(cpp,encoding='utf-8'); exe = work / ('owners.exe' if os.name == 'nt' else 'owners')
    subprocess.run([os.environ.get('CXX') or shutil.which('g++') or r'C:\Strawberry\c\bin\g++.exe','-std=c++20','-O0','-D_DEBUG','-I',str(root / 'Telegram/lib_rpl'),'-I',str(root / 'Telegram/lib_base'),'-I',str(root / 'Telegram/ThirdParty/GSL/include'),str(test),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
