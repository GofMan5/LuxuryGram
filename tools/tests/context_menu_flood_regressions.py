"""Extract actual delete-all FLOOD_WAIT branch; no network or delayed retry."""
from pathlib import Path
import tempfile
from library_regressions import cpp, source


def check(root):
    text = source('Telegram/SourceFiles/luxury/ui/context_menu/context_menu.cpp')
    start = text.index('if (Api::ErrorWaitSeconds(type)) {')
    branch = text[start:text.index('\n\t\t\tif (type ==', start)]
    assert 'base::call_delayed' not in branch
    assert 'state->say(Api::ErrorText(type));' in branch
    cpp(root, 'context_menu_flood', r'''
#include <cassert>
#include <functional>
#include <iostream>
#include <string>
using QString=std::string;
namespace Api{int ErrorWaitSeconds(QString type){return type=="FLOOD_WAIT_60"?60:0;}QString ErrorText(QString type){return type;}}
struct State{int toasts=0,retries=0;void say(const QString&){++toasts;}};
namespace base{template<class F>void call_delayed(int,F){assert(false);}}
void fail(State *state, QString type){
''' + branch + r'''
}
int main(){State s;fail(&s,"FLOOD_WAIT_60");assert(s.toasts==1&&s.retries==0);std::cout<<"Actual delete-all flood wait PASS: one error report, zero automatic retry/timer; deliberate user retry only, no network\n";}
''')


if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='luxury-context-flood-') as directory:
        check(Path(directory))
