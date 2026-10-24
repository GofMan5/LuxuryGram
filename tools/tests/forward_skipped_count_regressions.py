"""Forward progress bar: "skipped N" belongs to the chunk it was counted in.

Compiles the real ForwardState class and its counter methods with g++ and replays
the counter calls in the order ForwardItems / RunIntelligentForward make them.
Source-extracted fixture; not a native forward of real messages.
"""
from pathlib import Path
import re
import tempfile

from library_regressions import cpp, function, source

HEADER = 'Telegram/SourceFiles/luxury/features/forward/luxury_forward.h'
BODY = 'Telegram/SourceFiles/luxury/features/forward/luxury_forward.cpp'


def fixture(header, body):
    start = header.index('class ForwardState')
    klass = header[start:header.index('\n};', start) + 3]
    methods = '\n'.join(function(body, f'{name}') for name in (
        'ForwardState::ForwardState(int totalChunks)',
        'ForwardState::Snapshot ForwardState::snapshot() const',
        'void ForwardState::setMessages(int total, int sent)',
        'void ForwardState::setSentMessages(int sent)',
        'void ForwardState::setSkippedMessages(int skipped)',
        'void ForwardState::advanceChunk()',
    ))
    items = function(body, 'bool ForwardItems(')
    failed = items[items.index('if (!result.failed.empty())'):]
    failed = function(failed, 'if (!result.failed.empty())')
    calls = re.findall(r'state->(set\w+)\(', failed)
    replay = {
        'setSkippedMessages': 'state.setSkippedMessages(failed);',
        'setMessages': 'state.setMessages(total - failed, 0);',
    }
    failure = '\n'.join(replay[call] for call in calls)
    return '''#include <cassert>
#include <cstdio>
#include <mutex>
struct PeerId {};
namespace base { template <typename T> struct weak_ptr {}; }
namespace Main { struct Session; }
''' + klass + '\n' + methods + '''
void luxuryChunk(ForwardState &state, int total, int failed) {
	state.setMessages(total, 0);
	if (failed) {
''' + failure + '''
	}
}
int main() {
	ForwardState state(2);
	luxuryChunk(state, 5, 2);
	auto s = state.snapshot();
	assert(s.totalMessages == 3 && s.skippedMessages == 2);
	state.advanceChunk();
	state.setMessages(4, 0);
	s = state.snapshot();
	assert(s.totalMessages == 4 && s.skippedMessages == 0);
	state.setSentMessages(4);
	assert(state.snapshot().skippedMessages == 0);
	ForwardState mixed(3);
	luxuryChunk(mixed, 6, 1);
	mixed.advanceChunk();
	luxuryChunk(mixed, 2, 0);
	assert(mixed.snapshot().skippedMessages == 0 && mixed.snapshot().totalMessages == 2);
	std::puts("forward skipped counter PASS: skipped resets with each chunk, failure count survives its own chunk; g++ fixture of real ForwardState, not a native forward");
}
'''


def check(header=None, body=None):
    header = header if header is not None else source(HEADER)
    body = body if body is not None else source(BODY)
    with tempfile.TemporaryDirectory() as root:
        cpp(Path(root), 'forward_skipped', fixture(header, body))


if __name__ == '__main__':
    check()
