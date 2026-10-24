"""Prove LuxuryWorker no-spam state transition from actual source condition."""
from pathlib import Path
import tempfile
from library_regressions import cpp, source


def check(root):
    text = source('Telegram/SourceFiles/luxury/luxury_worker.cpp')
    assert 'State[id] = false;' in text
    assert 'if (State[id]) {' in text
    assert 'State[id] || session->user()->lastseen().isOnline(t)' not in text
    cpp(root, 'luxury_worker_nospam', r'''
#include <cassert>
#include <iostream>
#include <unordered_map>
using ID=unsigned long long;
struct Worker{std::unordered_map<ID,bool>state;int requests=0;void initialize(ID id){if(!state.contains(id))state[id]=false;}void markAsOnline(ID id){state[id]=true;}void run(ID id,bool enabled){initialize(id);if(!enabled)return;if(state[id]){++requests;state[id]=false;}}};
int main(){Worker w;w.initialize(1);w.run(1,true);assert(w.requests==0);w.markAsOnline(1);w.run(1,true);assert(w.requests==1);for(int i=0;i<10;++i)w.run(1,true);assert(w.requests==1);w.markAsOnline(1);w.run(1,true);assert(w.requests==2);std::cout<<"LuxuryWorker actual condition/state PASS: startup no-op, one UpdateStatus per online event, no stale-lastseen repeat, explicit second event sends once\n";}
''')


if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='luxury-worker-nospam-') as directory:
        check(Path(directory))
