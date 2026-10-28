"""Every thread pool of the app is sized from one CPU budget.

Core::Threads::ComputeBudget turns the detected topology into pool sizes:
clip readers, lottie renderer queues, streaming / transcode / encoder threads
and MTP file session threads per direction. One logical CPU always stays free
for the GUI thread, an efficiency core counts as half, and every pool has a
cap so a 64-thread or 256-thread machine does not spawn hundreds of threads.

Compiles the real header with g++ -Wall -Wextra -Werror and sweeps 1..64 and
256 logical CPUs plus two hybrid layouts; checks the call sites statically
(policy applied first in main, the old fixed sizes gone, the header Qt-free,
lottie shards used where no renderer was passed); mutants must be rejected.
Run: python tools/tests/threads_core_regressions.py
"""
from pathlib import Path
import re
import tempfile

from library_regressions import CXX, ROOT, STATIC, function, run, source, standalone

HEADER = 'Telegram/SourceFiles/core/core_threads.h'
MAIN = 'Telegram/SourceFiles/main.cpp'
MTP = 'Telegram/SourceFiles/mtproto/mtp_instance.cpp'
CLIP = 'Telegram/SourceFiles/media/clip/media_clip_reader.cpp'
LOTTIE = 'Telegram/SourceFiles/chat_helpers/stickers_lottie.cpp'
CMAKE = 'Telegram/CMakeLists.txt'
SOURCE = 'Telegram/SourceFiles/core/core_threads.cpp'

PROGRAM = r'''
#include "core/core_threads.h"

#include <array>
#include <cstdio>
#include <cstdlib>

using namespace Core::Threads;

static_assert(ComputeBudget({ .logical = 8 }).mtpFilePerDirection == 4);
static_assert(ComputeBudget({ .logical = 1 }).workers == 1);
static_assert(ComputeBudget({ .logical = 0 }).workers == 1);
static_assert(ComputeBudget({ .logical = 2 }).concrtMinWorkers == 4);

namespace {

int Failures = 0;

void Expect(bool condition, const char *what, int logical) {
	if (!condition) {
		std::printf("FAIL at %d logical: %s\n", logical, what);
		++Failures;
	}
}

int Effective(const Topology &t) {
	return (t.efficiencyLogical > 0)
		? std::clamp(t.performanceLogical + t.efficiencyLogical / 2, 1, t.logical)
		: t.logical;
}

void Bounds(const Topology &t) {
	const auto b = ComputeBudget(t);
	const auto l = t.logical;
	Expect(b.workers == std::max(1, Effective(t) - 1), "workers == max(1, E - 1)", l);
	Expect(b.workers >= 1 && b.workers < std::max(l, 2), "workers leave the GUI CPU", l);
	Expect(b.clipThreads >= 8 && b.clipThreads <= 16, "clip in 8..16, never below the old 8", l);
	Expect(b.lottieRenderers >= 1 && b.lottieRenderers <= 8, "lottie in 1..8", l);
	Expect(b.streamingVideoDecode >= 1 && b.streamingVideoDecode <= 16, "streaming in 1..16", l);
	Expect(b.transcodeDecode >= 1 && b.transcodeDecode <= 8, "transcode in 1..8", l);
	Expect(b.encoderThreads >= 1 && b.encoderThreads <= 8, "encoder in 1..8", l);
	Expect(b.mtpFilePerDirection >= 1 && b.mtpFilePerDirection <= 4, "mtp in 1..4", l);
	Expect(b.concrtMinWorkers >= 0, "concrt >= 0", l);
	Expect(b.concrtMinWorkers == ((l < 4) ? 4 : 0), "concrt only below 4 CPUs", l);
	Expect(
		b.streamingVideoDecode + b.transcodeDecode + b.encoderThreads
			<= 2 * b.workers + 4,
		"media threads <= 2 * workers + 4",
		l);
}

void Monotonic(const Budget &a, const Budget &b, int l) {
	Expect(a.workers <= b.workers, "workers monotonic", l);
	Expect(a.clipThreads <= b.clipThreads, "clip monotonic", l);
	Expect(a.lottieRenderers <= b.lottieRenderers, "lottie monotonic", l);
	Expect(a.streamingVideoDecode <= b.streamingVideoDecode, "streaming monotonic", l);
	Expect(a.transcodeDecode <= b.transcodeDecode, "transcode monotonic", l);
	Expect(a.encoderThreads <= b.encoderThreads, "encoder monotonic", l);
	Expect(a.mtpFilePerDirection <= b.mtpFilePerDirection, "mtp monotonic", l);
}

} // namespace

int main() {
	auto previous = ComputeBudget({ .logical = 1 });
	for (auto l = 1; l <= 64; ++l) {
		const auto t = Topology{ .logical = l, .physical = l };
		Bounds(t);
		const auto current = ComputeBudget(t);
		Monotonic(previous, current, l);
		previous = current;
	}
	const auto huge = Topology{ .logical = 256, .physical = 128 };
	Bounds(huge);
	Monotonic(previous, ComputeBudget(huge), 256);
	const auto top = ComputeBudget(huge);
	Expect(top.clipThreads == 16 && top.streamingVideoDecode == 16, "16 caps reached", 256);
	Expect(top.lottieRenderers == 8 && top.transcodeDecode == 8 && top.encoderThreads == 8, "8 caps reached", 256);
	Expect(top.mtpFilePerDirection == 4, "mtp cap reached", 256);
	Expect(ComputeBudget({ .logical = 8 }).mtpFilePerDirection == 4, "mtp at 8 logical", 8);

	// 8P+16E: 8 hyperthreaded performance cores and 16 efficiency cores.
	const auto big = Topology{
		.logical = 32,
		.physical = 24,
		.performanceLogical = 16,
		.efficiencyLogical = 16,
	};
	// 6P+8E.
	const auto mid = Topology{
		.logical = 20,
		.physical = 14,
		.performanceLogical = 12,
		.efficiencyLogical = 8,
	};
	Bounds(big);
	Bounds(mid);
	Expect(ComputeBudget(big).workers == 23, "8P+16E has 23 workers", 32);
	Expect(ComputeBudget({ .logical = 8 }).clipThreads == 8, "clip keeps 8 at 8 logical", 8);
	Expect(ComputeBudget({ .logical = 2 }).clipThreads == 8, "clip keeps 8 at 2 logical", 2);

	// Summarize: 8P (2 logical each, class 1) + 16E (1 logical, class 0),
	// with masks intersected with the process affinity.
	auto cores = std::array<CoreRecord, 24>();
	const auto pin = [&](int pThreads, int eThreads) {
		for (auto i = 0; i != 8; ++i) {
			cores[i] = { .logical = std::clamp(pThreads - 2 * i, 0, 2), .efficiencyClass = 1 };
		}
		for (auto i = 0; i != 16; ++i) {
			cores[8 + i] = { .logical = (i < eThreads) ? 1 : 0, .efficiencyClass = 0 };
		}
		return Summarize(cores);
	};
	const auto all = pin(16, 16);
	Expect(all.logical == 32 && all.physical == 24, "8P+16E summary counts", 32);
	Expect(all.performanceLogical == 16 && all.efficiencyLogical == 16, "8P+16E summary classes", 32);
	Expect(ComputeBudget(all).workers == ComputeBudget(big).workers, "summary matches the hybrid budget", 32);
	const auto eOnly = pin(0, 4);
	Expect(eOnly.logical == 4 && eOnly.physical == 4, "pinned to 4 E threads: 4 logical", 4);
	Expect(eOnly.performanceLogical == 0 && eOnly.efficiencyLogical == 4, "pinned E threads stay efficiency", 4);
	Expect(ComputeBudget(eOnly).workers == 1, "4 E threads give 1 worker", 4);
	const auto pOnly = pin(4, 0);
	Expect(pOnly.logical == 4 && pOnly.physical == 2, "pinned to 2 P cores", 4);
	Expect(pOnly.performanceLogical == 4 && pOnly.efficiencyLogical == 0, "pinned P threads stay performance", 4);
	Expect(ComputeBudget(pOnly).workers == 3, "4 P threads give 3 workers", 4);
	const auto flat = std::array<CoreRecord, 4>{ {
		{ .logical = 2 }, { .logical = 2 }, { .logical = 0 }, { .logical = 2 } } };
	const auto uniform = Summarize(flat);
	Expect(uniform.logical == 6 && uniform.physical == 3, "uniform cores skip unusable ones", 6);
	Expect(uniform.performanceLogical == 6 && uniform.efficiencyLogical == 0, "uniform cores are all performance", 6);
	Expect(ComputeBudget(mid).workers == 15, "6P+8E has 15 workers", 20);
	Expect(ComputeBudget(mid).workers <= ComputeBudget(big).workers, "hybrid monotonic", 20);
	Expect(
		ComputeBudget(big).workers < ComputeBudget({ .logical = 32 }).workers,
		"efficiency cores count as half",
		32);

	if (Failures) {
		return EXIT_FAILURE;
	}
	std::puts("threads core PASS: 1..64 and 256 logical, 8P+16E and 6P+8E; caps, GUI reserve, monotonic, media sum bound, mtp 4 at 8 logical, clip floor 8; affinity summary: all, 4 E, 4 P, uniform");
	return EXIT_SUCCESS;
}
'''


def crlf(text):
    return text.replace('\r\n', '\n')


def compile_budget(root, header, name='threads_core'):
    tree = root / name
    (tree / 'core').mkdir(parents=True, exist_ok=True)
    (tree / 'core' / 'core_threads.h').write_text(header, encoding='utf-8')
    path = tree / 'main.cpp'
    path.write_text(PROGRAM, encoding='utf-8')
    exe = tree / 'threads_core.exe'
    run([CXX, '-std=c++20', '-O0', '-Wall', '-Wextra', '-Werror', '-I', str(tree),
         str(path), *STATIC, '-o', str(exe)], tree)
    standalone(exe)
    return run([str(exe)], tree).strip()


def check_main(files):
    main = function(files[MAIN], 'int main(int argc, char *argv[]) {')
    policy = main.index('Core::Threads::ApplyProcessPolicy();')
    assert policy < main.index('Core::Launcher::Create('), 'the thread policy must be applied before the launcher'
    assert '#include "core/core_threads.h"' in files[MAIN]


def check_header(files):
    header = files[HEADER]
    assert not re.search(r'#include\s*<Q', header), 'core_threads.h must stay Qt-free'
    assert not re.search(r'\bQ[A-Z]\w*', header), 'core_threads.h must stay Qt-free'


def check_pools(files):
    assert 'idealThreadCount' not in files[MTP], 'MTP file sessions must be sized from Core::Threads'
    assert '_fileSessionThreads.resize(2 * Core::Threads::Get().mtpFilePerDirection);' in files[MTP], \
        'MTP needs an even number of file session threads'
    assert 'kClipThreadsCount' not in files[CLIP], 'clip readers must be sized from Core::Threads'
    assert 'Core::Threads::Get().clipThreads' in files[CLIP]


def check_source(files):
    source = files[SOURCE]
    assert 'Popcount(processor.GroupMask[i].Mask & allowed)' in source,         'core masks must be intersected with the process affinity'
    assert 'auto result = Summarize(cores);' in source, 'Windows topology must come from Summarize'
    assert 'std::min(result.logical, allowed)' not in source, 'the separate affinity clamp is gone'


def check_cmake(files, raw_cmake):
    # The checkout picks CRLF or LF (autocrlf); an edit must not mix them.
    assert raw_cmake.count(b'\r\n') in (0, raw_cmake.count(b'\n')), 'CMakeLists.txt has mixed line endings'
    cmake = files[CMAKE]
    assert '    core/core_threads.cpp\n    core/core_threads.h\n' in cmake, 'core_threads is not built'


def check_lottie(files):
    lottie = files[LOTTIE]
    shard = '\tif (!renderer) {\n\t\trenderer = LottieRenderer();\n\t}\n'
    assert lottie.count(shard) == 2, 'both renderer-less lottie entry points must use a shard'
    player = function(lottie, 'std::unique_ptr<Lottie::SinglePlayer> LottiePlayerFromDocument(\n\t\tnot_null<Data::DocumentMedia*> media,\n\t\tconst Lottie::ColorReplacements *replacements,')
    assert shard in player
    thumbnail = function(lottie, 'std::unique_ptr<Lottie::SinglePlayer> LottieThumbnail(')
    assert shard in thumbnail and 'std::move(renderer)' in thumbnail, 'LottieThumbnail must pass its renderer on'


CHECKS = (check_main, check_header, check_pools, check_lottie, check_source)

MUTANTS = (
    (check_main, MAIN, '\tCore::Threads::ApplyProcessPolicy();\n', ''),
    (check_pools, MTP, '2 * Core::Threads::Get().mtpFilePerDirection', 'Core::Threads::Get().mtpFilePerDirection'),
    (check_pools, CLIP, 'Core::Threads::Get().clipThreads', 'kClipThreadsCount'),
    (check_lottie, LOTTIE, '\tif (!renderer) {\n\t\trenderer = LottieRenderer();\n\t}\n', ''),
    (check_header, HEADER, '#include <algorithm>', '#include <QtCore/QThread>'),
    (check_source, SOURCE, 'GroupMask[i].Mask & allowed', 'GroupMask[i].Mask'),
    (check_source, SOURCE, 'auto result = Summarize(cores);', 'auto result = Topology();'),
)

HEADER_MUTANTS = (
    ('std::clamp(workers / 2, 8, 16)', 'std::clamp(workers / 2, 8, 160)'),
    ('std::clamp(workers / 2, 8, 16)', 'std::clamp(workers / 2, 2, 16)'),
    ('std::max(1, effective - 1)', 'std::max(1, effective)'),
    ('std::clamp((workers + 1) / 2, 1, 4)', 'std::clamp((workers + 1) / 3, 1, 4)'),
    ('std::clamp(workers / 4, 1, 8)', 'std::clamp(workers / 4, 0, 8)'),
    ('t.efficiencyLogical / 2', 't.efficiencyLogical'),
    ('(logical < 4) ? 4 : 0', '(logical < 4) ? 4 : -1'),
    # Ranking classes only over usable cores makes pinned E cores performance.
    ('maxClass = std::max(maxClass, core.efficiencyClass);',
     'maxClass = std::max(maxClass, core.logical ? core.efficiencyClass : 0);'),
    ('if (core.logical <= 0) {', 'if (core.logical < 0) {'),
)


def check(root):
    files = {path: crlf(source(path)) for path in (HEADER, MAIN, MTP, CLIP, LOTTIE, CMAKE, SOURCE)}
    raw_cmake = (ROOT / CMAKE).read_bytes()
    for test in CHECKS:
        test(files)
    check_cmake(files, raw_cmake)
    for test, path, before, after in MUTANTS:
        assert before in files[path], f'mutant anchor moved: {before}'
        mutated = dict(files)
        mutated[path] = files[path].replace(before, after, 1)
        try:
            test(mutated)
        except (AssertionError, ValueError):
            continue
        raise AssertionError(f'{test.__name__} accepts the mutant: {after}')
    for cmake, raw in (
            (files[CMAKE], raw_cmake.replace(b'\r\n', b'\n') + b'mixed\r\n'),
            (files[CMAKE].replace('    core/core_threads.cpp\n', ''), raw_cmake)):
        try:
            check_cmake({CMAKE: cmake}, raw)
        except AssertionError:
            continue
        raise AssertionError('check_cmake accepts a broken CMakeLists.txt')

    header = files[HEADER]
    print(compile_budget(root, header))
    for index, (before, after) in enumerate(HEADER_MUTANTS):
        assert before in header, f'header mutant anchor moved: {before}'
        try:
            compile_budget(root, header.replace(before, after, 1), f'threads_core_m{index}')
        except AssertionError:
            continue  # Did not compile or failed at runtime: rejected.
        raise AssertionError(f'the threads core fixture accepts the mutant: {after}')


if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='luxury-threads-core-') as directory:
        check(Path(directory))
