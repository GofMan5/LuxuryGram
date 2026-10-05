"""Compile the real server-presence methods against small protocol/clock doubles.
This verifies recording integration logic, not a native Qt/protocol build.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'Telegram/SourceFiles/data/data_user.cpp').read_text('utf-8')
methods = source.split('bool UserData::updateServerLastseen(', 1)[1].split('// see Serialize::readPeer', 1)[0]
methods = 'bool UserData::updateServerLastseen(' + methods
harness = r'''
#include "luxury/features/watch/presence_history.h"
#include <cassert>
#include <iostream>
#include <vector>

using TimeId = int;
namespace base::unixtime {
int clock = 100;
int now() { return clock; }
}
enum { mtpc_userStatusOnline, mtpc_userStatusOffline, mtpc_userStatusRecently };
struct MTPUserStatus {
    int kind = 0;
    int till = 0;
    int type() const { return kind; }
};
namespace Data {
struct LastseenStatus {
    int till = 0;
    bool local = false;
    bool isOnline(int now) const { return till > now; }
};
}
Data::LastseenStatus LastseenFromMTP(MTPUserStatus status, Data::LastseenStatus previous) {
    if (status.kind == mtpc_userStatusRecently && previous.local) {
        return previous; // Real UI parser preserves local inference for Recently.
    }
    return { status.till, false };
}
class UserData {
public:
    bool updateServerLastseen(const MTPUserStatus &status);
    void resetTrackedPresence();
    std::optional<int> trackedOnlineStart() const;
    Data::LastseenStatus serverLastseen() const;
    bool updateLastseen(Data::LastseenStatus status) { _lastseen = status; return true; }
    bool isSelf() const { return self; }
    bool isBot() const { return bot; }
    bool isServiceUser() const { return service; }
    bool self = false;
    bool bot = false;
    bool service = false;
    Data::LastseenStatus _lastseen;
    Data::LastseenStatus _serverLastseen;
    LuxuryFeatures::Watch::PresenceTracker _trackedPresence;
};
namespace LuxuryOnline {
struct Transition { bool online = false; int at = 0; };
bool allowed = true;
std::vector<Transition> recorded;
bool TrackingAllowed() { return allowed; }
void recordTransition(UserData *, bool online, int at) { recorded.push_back({ online, at }); }
}
'''
harness += methods
harness += r'''
int main() {
    using namespace LuxuryOnline;
    UserData user;
    user.updateServerLastseen({ mtpc_userStatusOffline, 90 });
    assert(recorded.empty());
    user.updateServerLastseen({ mtpc_userStatusOnline, 110 });
    assert(recorded.size() == 1 && recorded.back().online && user.trackedOnlineStart() == 100);
    base::unixtime::clock = 105;
    user.updateServerLastseen({ mtpc_userStatusOnline, 120 });
    assert(recorded.size() == 1 && user.trackedOnlineStart() == 100);
    base::unixtime::clock = 121;
    user.updateServerLastseen({ mtpc_userStatusOffline, 120 });
    assert(recorded.size() == 2 && !recorded.back().online); // Old expiry already elapsed.
    user._lastseen = { 200, true }; // madeAction-like UI inference.
    user.updateServerLastseen({ mtpc_userStatusRecently, 0 });
    assert(recorded.size() == 2 && !user.trackedOnlineStart());
    assert(user._lastseen.isOnline(121) && !user.serverLastseen().isOnline(121));
    user.updateServerLastseen({ mtpc_userStatusOnline, 130 });
    assert(recorded.size() == 3);
    user.updateServerLastseen({ mtpc_userStatusRecently, 0 });
    assert(recorded.size() == 3 && !user.trackedOnlineStart()); // Hidden status is a gap.
    user.updateServerLastseen({ mtpc_userStatusOffline, 120 });
    assert(recorded.size() == 3); // No fabricated logout across a gap.
    user.updateServerLastseen({ mtpc_userStatusOnline, 130 });
    assert(recorded.size() == 4);
    base::unixtime::clock = 131;
    user.updateServerLastseen({ mtpc_userStatusOnline, 150 });
    assert(recorded.size() == 5 && user.trackedOnlineStart() == 131); // Renew after expiry: new observation.
    user.updateServerLastseen({ mtpc_userStatusOnline, 125 });
    assert(recorded.size() == 5 && !user.trackedOnlineStart()); // Stale online is not offline.
    allowed = false;
    user.updateServerLastseen({ mtpc_userStatusOnline, 150 });
    assert(recorded.size() == 5 && !user.trackedOnlineStart());
    allowed = true;
    user.updateServerLastseen({ mtpc_userStatusOffline, 125 });
    assert(recorded.size() == 5);
    user.updateServerLastseen({ mtpc_userStatusOnline, 150 });
    assert(recorded.size() == 6);
    user.resetTrackedPresence();
    user.updateServerLastseen({ mtpc_userStatusOnline, 150 });
    assert(recorded.size() == 7); // Clear/restart resumes independently.
    UserData excluded;
    excluded.self = true;
    excluded.updateServerLastseen({ mtpc_userStatusOnline, 150 });
    excluded.self = false;
    excluded.bot = true;
    excluded.updateServerLastseen({ mtpc_userStatusOnline, 150 });
    excluded.bot = false;
    excluded.service = true;
    excluded.updateServerLastseen({ mtpc_userStatusOnline, 150 });
    assert(recorded.size() == 7);
    std::cout << "Production server presence: PASS (live transition, elapsed expiry, inference isolation, hidden/stale statuses, gates, exclusions)\n";
}
'''
with tempfile.TemporaryDirectory(prefix='luxury-presence-recording-') as tmp:
    cpp = Path(tmp) / 'recording.cpp'
    exe = Path(tmp) / 'recording.exe'
    cpp.write_text(harness, encoding='utf-8')
    subprocess.run([
        'g++', '-std=c++20', '-Wall', '-Wextra', '-Werror',
        '-I', str(root / 'Telegram/SourceFiles'), str(cpp), '-o', str(exe),
    ], check=True)
    subprocess.run([str(exe)], check=True)
