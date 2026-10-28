"""Blocking luxury jobs run on their own threads, not on crl::async workers.

crl::async is a fixed pool (ConcRT on Windows) that does not grow when a job
blocks on a latch or spins a nested QEventLoop, so one such job stalls every
other async decode in the app. Checked statically, the client is not built:
- the blocking call chain is re-proven from source (FetchCover reaches the
  nested QEventLoop, the forward runners and forwardRichMessage reach the
  WaitUntil latch), so a renamed entrypoint fails here instead of passing;
- every listed site launches its job with std::thread(...).detach(), keeps
  its recorded number of crl::on_main deliveries and includes <thread>;
- no crl::async job anywhere in luxury/ reaches a blocking call;
- a std::thread + latch model builds with -Wall -Wextra -Werror -pthread.
Every source check also runs against a mutant it must reject.

Bounded source check; not a native acceptance.
"""
from pathlib import Path
import os
import re
import tempfile
from library_regressions import CXX, ROOT, STATIC, function, run, source, standalone

LUXURY = 'Telegram/SourceFiles/luxury/'
FORWARD = LUXURY + 'features/forward/luxury_forward.cpp'
RICH = LUXURY + 'features/forward/luxury_forward_rich.cpp'
SYNC = LUXURY + 'features/forward/luxury_sync.cpp'
ITUNES = LUXURY + 'ui/utils/itunes_search.cpp'
MENU = LUXURY + 'ui/context_menu/context_menu.cpp'
MUSIC = LUXURY + 'ui/components/saved_music.cpp'
PLAYER = LUXURY + 'ui/boxes/expanded_player_box.cpp'

# A job reaching any of these blocks its thread on a latch or a nested
# event loop; the chain from each name to the real wait is proven below.
BLOCKING = ('WaitUntil(', '.await(', 'QEventLoop', 'execWithTimeout(', 'getBytesWithTimeout(',
            'FetchCover(', 'forwardRichMessage(', 'RunForward(', 'RunIntelligentForward(', 'LuxurySync::')

# (path, call inside the job, crl::on_main deliveries recorded before the change)
SITES = (
    (FORWARD, 'RunIntelligentForward(', 0),
    (FORWARD, 'RunForward(', 0),
    (MENU, 'forwardRichMessage(', 1),
    (PLAYER, 'FetchCover(', 1),
)
# crl::on_main_sync is the lifetime teardown both forward jobs end with.
SYNC_DELIVERIES = {'RunIntelligentForward(': 1, 'RunForward(': 1}

MUSIC_DISPATCH = ('\tif (cover.noCover) {\n\t\tstd::thread(std::move(job)).detach();\n'
                  '\t} else {\n\t\tcrl::async(std::move(job));\n\t}')
# The only crl::async in luxury/ whose argument is not a lambda literal: the
# saved-music job, proven to block only on the std::thread branch.
NAMED_ASYNC = {(MUSIC, 'std::move(job)')}


def crlf(text):
    return text.replace('\r\n', '\n')


def closing(text, opening):
    # Index of the bracket matching text[opening], skipping literals and comments.
    pairs = {'(': ')', '[': ']', '{': '}'}
    stack = [pairs[text[opening]]]
    i = opening + 1
    while stack:
        c = text[i]
        if text.startswith('//', i):
            i = text.index('\n', i)
            continue
        if text.startswith('/*', i):
            i = text.index('*/', i) + 2
            continue
        if c == '"' or (c == "'" and not text[i - 1].isalnum()):
            i += 1
            while text[i] != c:
                i += 2 if text[i] == '\\' else 1
        elif c in pairs:
            stack.append(pairs[c])
        elif c in ')]}':
            assert c == stack.pop(), f'unbalanced {c} at {i}'
        i += 1
    return i - 1


def launches(text):
    # (kind, argument, detached) for every crl::async( and std::thread( call.
    result = []
    for match in re.finditer(r'\b(crl::async|std::thread)\(', text):
        end = closing(text, match.end() - 1)
        argument = text[match.end():end]
        detached = text.startswith(').detach();', end)
        result.append((match.group(1), argument, detached))
    return result


def blocking(argument):
    return [name for name in BLOCKING if name in argument]


def check_chain(files):
    itunes = files[ITUNES]
    loop = function(itunes, 'execWithTimeout(')
    assert 'QEventLoop loop;' in loop and 'loop.exec();' in loop, 'execWithTimeout no longer spins a nested loop'
    assert 'execWithTimeout(' in function(itunes, 'QByteArray getBytesWithTimeout(')
    assert 'getBytesWithTimeout(' in function(itunes, 'QImage FetchCover(')
    sync = files[SYNC]
    assert 'latch->await(' in function(sync, 'bool WaitUntil(')
    assert sync.count('WaitUntil(') > 1, 'LuxurySync no longer waits on its latch'
    forward = files[FORWARD]
    assert 'LuxurySync::' in function(forward, 'bool ForwardItems(')
    for name in ('void RunForward(', 'void RunIntelligentForward('):
        assert 'ForwardItems(job, state,' in function(forward, name), f'{name} no longer reaches LuxurySync'
    assert 'LuxurySync::loadFullRichPageSync(' in function(files[RICH], 'bool forwardRichMessage('), \
        'forwardRichMessage no longer reaches LuxurySync'
    assert 'WaitUntil(' in function(sync, 'std::shared_ptr<const Iv::RichPage> loadFullRichPageSync(')


def check_sites(files):
    for path, call, deliveries in SITES:
        jobs = [launch for launch in launches(files[path]) if call in launch[1]]
        assert len(jobs) == 1, f'{path}: {len(jobs)} jobs call {call}'
        kind, argument, detached = jobs[0]
        assert kind == 'std::thread' and detached, f'{path}: {call} job is not a detached std::thread'
        assert argument.lstrip().startswith('['), f'{path}: {call} job is not a lambda'
        on_main = len(re.findall(r'crl::on_main\(', argument))
        assert on_main == deliveries, f'{path}: {call} job delivers {on_main} times, recorded {deliveries}'
        assert argument.count('crl::on_main_sync(') == SYNC_DELIVERIES.get(call, 0), f'{path}: {call} teardown changed'


def check_music(files):
    text = files[MUSIC]
    body = function(text, 'void LuxuryMusicButton::makeCover() {')
    assert MUSIC_DISPATCH in body, 'saved-music cover lookup must leave the pool when it fetches'
    start = body.index('\tauto job = [')
    job = body[start:body.index(MUSIC_DISPATCH)]
    assert 'noCover = cover.noCover,' in job, 'the job must branch on the same flag as the dispatch'
    fetch = job.index('if (noCover) {')
    lookup = job[fetch:closing(job, job.index('{', fetch))]
    assert 'Luxury::Ui::Itunes::FetchCover(' in lookup, 'FetchCover must stay behind the noCover branch'
    assert job.count('FetchCover(') == 1 and len(re.findall(r'crl::on_main\(', job)) == 1


def check_global(files):
    for path, text in files.items():
        if not path.startswith(LUXURY):
            continue
        uses = launches(text)
        for kind, argument, detached in uses:
            if kind == 'crl::async':
                found = blocking(argument)
                assert not found, f'{path}: crl::async job blocks on {found}'
                assert argument.lstrip().startswith('[') or (path, argument.strip()) in NAMED_ASYNC, \
                    f'{path}: unchecked named crl::async job {argument.strip()}'
            else:
                assert detached, f'{path}: std::thread is neither detached nor checked'
        if any(kind == 'std::thread' for kind, _, _ in uses):
            assert '#include <thread>' in text, f'{path}: uses std::thread without <thread>'


CHECKS = (check_chain, check_sites, check_music, check_global)

MUTANTS = (
    (check_sites, FORWARD, '\tstd::thread([\n\t\tjob = std::move(job),\n\t\tchunks', '\tcrl::async([\n\t\tjob = std::move(job),\n\t\tchunks'),
    (check_global, FORWARD, '\tstd::thread([\n\t\tjob = std::move(job),\n\t\titems', '\tcrl::async([\n\t\tjob = std::move(job),\n\t\titems'),
    (check_sites, FORWARD, '\t\tcrl::on_main_sync([&] { lifetime->destroy(); });\n\t}).detach();\n}\n\nvoid forwardMessages(',
     '\t}).detach();\n}\n\nvoid forwardMessages('),
    (check_global, MENU, 'std::thread([=]\n', 'crl::async([=]\n'),
    (check_sites, MENU, 'crl::on_main(weakSession, forwardPlain);', 'forwardPlain();'),
    (check_sites, PLAYER, '\tstd::thread([=] {\n\t\tauto image', '\tcrl::async([=] {\n\t\tauto image'),
    (check_global, PLAYER, '\tstd::thread([=] {\n\t\tauto image', '\tcrl::async([=] {\n\t\tauto image'),
    (check_sites, PLAYER, '\t\t\tsetCover(std::move(image));\n\t\t});\n\t}).detach();', '\t\t\tsetCover(std::move(image));\n\t\t});\n\t});'),
    (check_global, PLAYER, '#include <thread>\n', ''),
    (check_global, FORWARD, '#include <thread>\n', ''),
    (check_music, MUSIC, '\t\tstd::thread(std::move(job)).detach();', '\t\tcrl::async(std::move(job));'),
    (check_music, MUSIC, '\tif (cover.noCover) {\n\t\tstd::thread', '\tif (!cover.noCover) {\n\t\tstd::thread'),
    (check_global, MUSIC, '\t\tcrl::async(std::move(job));', '\t\tcrl::async(std::move(other));'),
    (check_global, MUSIC, '#include <thread>\n', ''),
    (check_global, LUXURY + 'features/watch/watched_media.cpp', 'crl::async([paths = std::move(paths)] {',
     'crl::async([paths = std::move(paths)] {\n\t\tQEventLoop().exec();'),
    (check_chain, ITUNES, 'loop.exec();', 'loop.processEvents();'),
    (check_chain, RICH, 'LuxurySync::loadFullRichPageSync(', 'loadFullRichPageSync('),
)

MODEL = r'''
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>

class Latch {
public:
	explicit Latch(int count) : _count(count) {
	}
	void countDown() {
		const auto lock = std::lock_guard(_mutex);
		if (_count > 0 && --_count == 0) {
			_cv.notify_all();
		}
	}
	bool await(std::chrono::milliseconds timeout) {
		auto lock = std::unique_lock(_mutex);
		return _cv.wait_for(lock, timeout, [this] { return _count == 0; });
	}

private:
	std::mutex _mutex;
	std::condition_variable _cv;
	int _count = 0;
};

int main() {
	const auto latch = std::make_shared<Latch>(1);
	const auto delivered = std::make_shared<std::atomic<int>>(0);
	const auto done = std::make_shared<Latch>(1);
	const auto onMain = [=](std::function<void()> callback) {
		callback();
	};
	std::thread([=] {
		if (latch->await(std::chrono::seconds(5))) {
			onMain([=] { delivered->fetch_add(1); });
		}
		done->countDown();
	}).detach();
	latch->countDown();
	if (!done->await(std::chrono::seconds(5)) || delivered->load() != 1) {
		return 1;
	}
	std::cout << "pool blocking model ok\n";
	return 0;
}
'''


def model(root):
    path = root / 'pool_blocking_model.cpp'
    path.write_text(MODEL, encoding='utf-8')
    exe = root / ('pool_blocking_model' + ('.exe' if os.name == 'nt' else ''))
    run([CXX, '-std=c++20', '-Wall', '-Wextra', '-Werror', '-pthread', str(path), *STATIC, '-o', str(exe)], root)
    standalone(exe)
    out = run([str(exe)], root)
    assert out.strip() == 'pool blocking model ok', out


def check(root, read=source):
    paths = sorted(str(p.relative_to(ROOT)).replace('\\', '/')
                   for p in (ROOT / LUXURY).rglob('*') if p.suffix in ('.cpp', '.h'))
    files = {path: crlf(read(path)) for path in paths}
    for test in CHECKS:
        test(files)
    for test, path, before, after in MUTANTS:
        assert before in files[path], f'mutant anchor moved: {before}'
        mutated = dict(files)
        mutated[path] = files[path].replace(before, after, 1)
        try:
            test(mutated)
        except (AssertionError, ValueError):
            continue
        raise AssertionError(f'{test.__name__} accepts the mutant: {after}')
    model(root)
    print('Pool blocking PASS: latch and nested-loop jobs run on detached threads, crl::async stays CPU-only')


if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='luxury-pool-blocking-') as directory:
        check(Path(directory))
