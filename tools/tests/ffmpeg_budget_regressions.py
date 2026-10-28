"""FFmpeg thread budget: every decoder and encoder gets a per-role count.

The client is not built here, so:
- ResolveThreads is extracted from the real source and compiled with
  -Wall -Wextra -Werror against stubs: the user override wins, then the
  caller's role count, libvpx-vp9 "auto" is capped to 1..8 instead of
  min(cpus, 64) threads per decoder, native codecs keep FFmpeg's auto (0);
- the Qt half is checked statically: every MakeCodecPointer call site
  passes its role's `.threads =`, lib_ffmpeg includes no Telegram core
  header, CreateVideoEncoder sets thread_count before setup() and
  avcodec_open2, the VP9 setup asks for row-mt and leaves tile-columns to
  libvpx's width-clamped default, the H.264 setup asks for up to 4 slices
  (libopenh264 threads only across slices), and
  MakeCodecPointer resolves threads only under the multi-thread option.
Every check is also run against a mutant it must reject.

Bounded source check; not a native acceptance.
"""
from pathlib import Path
import os
import subprocess
import tempfile
from library_regressions import CXX, STATIC, function, run, source, standalone

SRC = 'Telegram/SourceFiles/'
HEADER = SRC + 'ffmpeg/ffmpeg_utility.h'
UTILITY = SRC + 'ffmpeg/ffmpeg_utility.cpp'
GENERATOR = SRC + 'ffmpeg/ffmpeg_frame_generator.cpp'
STREAMING = SRC + 'media/streaming/media_streaming_file.cpp'
FRAMES = SRC + 'media/media_video_frames.cpp'
WAVEFORM = SRC + 'media/media_audio_waveform.cpp'
ENCODE = SRC + 'media/media_video_encode.cpp'

# Each MakeCodecPointer call site in source order and the count it passes.
ROLES = {
    GENERATOR: ['1'],
    STREAMING: ['Core::Threads::Get().streamingVideoDecode', '1'],
    FRAMES: ['Core::Threads::Get().transcodeDecode'],
    WAVEFORM: ['1'],
    ENCODE: ['1', '1', 'Core::Threads::Get().transcodeDecode'],
}

RESOLVE = '[[nodiscard]] int ResolveThreads(const AVCodec *codec, int requested) {'

MAIN = r'''
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>

#define EXPECT(condition) do { if (!(condition)) { \
	std::cerr << "line " << __LINE__ << ": " #condition "\n"; std::abort(); } } while (0)

struct AVCodec {
	const char *name;
};

int Cpus = 1;
int av_cpu_count() {
	return Cpus;
}

struct {
	int user = 0;
	int value() const {
		return user;
	}
} OptionFFmpegThreadCount;

namespace {

@RESOLVE@

} // namespace

int main() {
	const auto vpx = AVCodec{ "libvpx-vp9" };
	const auto h264 = AVCodec{ "h264" };
	const auto vp9 = AVCodec{ "vp9" };
	for (auto cpus = 1; cpus <= 256; ++cpus) {
		Cpus = cpus;
		const auto capped = ResolveThreads(&vpx, 0);
		EXPECT(capped >= 1 && capped <= 8);
		EXPECT(capped == std::clamp(cpus - 1, 1, 8));
		EXPECT(ResolveThreads(&h264, 0) == 0);
		EXPECT(ResolveThreads(&vp9, 0) == 0);
		for (const auto requested : { 1, 2, 7, 16 }) {
			EXPECT(ResolveThreads(&vpx, requested) == requested);
			EXPECT(ResolveThreads(&h264, requested) == requested);
		}
	}
	Cpus = 64;
	EXPECT(ResolveThreads(&vpx, 0) == 8);
	Cpus = 1;
	EXPECT(ResolveThreads(&vpx, 0) == 1);
	Cpus = 2;
	EXPECT(ResolveThreads(&vpx, 0) == 1);

	// The user's thread count beats every role, the libvpx cap included.
	OptionFFmpegThreadCount.user = 3;
	EXPECT(ResolveThreads(&vpx, 0) == 3);
	EXPECT(ResolveThreads(&vpx, 1) == 3);
	EXPECT(ResolveThreads(&h264, 16) == 3);
	OptionFFmpegThreadCount.user = -1;
	EXPECT(ResolveThreads(&h264, 5) == 5);
	OptionFFmpegThreadCount.user = 0;
	std::cout << "ffmpeg budget fixture ok\n";
	return 0;
}
'''

LOGIC_MUTANTS = (
    (UTILITY, 'return std::clamp(av_cpu_count() - 1, 1, 8);', 'return 0;'),
    (UTILITY, 'return std::clamp(av_cpu_count() - 1, 1, 8);', 'return std::max(av_cpu_count() - 1, 1);'),
    (UTILITY, 'return std::clamp(av_cpu_count() - 1, 1, 8);', 'return std::clamp(av_cpu_count(), 1, 8);'),
    (UTILITY, '!strcmp(codec->name, "libvpx-vp9")', 'strcmp(codec->name, "libvpx-vp9")'),
    (UTILITY, 'user > 0) {\n\t\treturn user;\n\t} else if (requested > 0) {\n\t\treturn requested;',
     'user > 0 && requested <= 0) {\n\t\treturn user;\n\t} else if (requested > 0) {\n\t\treturn requested;'),
    (UTILITY, 'user > 0) {', 'user != 0) {'),
)


def crlf(text):
    return text.replace('\r\n', '\n')


def body(text, signature):
    return function(text, signature)


def fixture(root, files, name):
    text = MAIN.replace('@RESOLVE@', body(files[UTILITY], RESOLVE))
    main = root / (name + '.cpp')
    main.write_text(text, encoding='utf-8')
    exe = root / (name + ('.exe' if os.name == 'nt' else ''))
    run([CXX, '-std=c++20', '-O0', '-Wall', '-Wextra', '-Werror', str(main), *STATIC, '-o', str(exe)], root)
    standalone(exe)
    result = subprocess.run([str(exe)], cwd=root, capture_output=True, text=True, errors='replace')
    return result.returncode, result.stdout + result.stderr


# --- Static checks of the Qt half.

def initializers(text):
    result = []
    at = 0
    while True:
        at = text.find('MakeCodecPointer({', at)
        if at < 0:
            return result
        result.append(body(text[at:], 'MakeCodecPointer({'))
        at += 1


def check_roles(files):
    for path, roles in ROLES.items():
        found = initializers(files[path])
        assert len(found) == len(roles), f'{path}: {len(found)} MakeCodecPointer calls, expected {len(roles)}'
        for call, role in zip(found, roles):
            assert f'.threads = {role}' in call, f'{path}: {call} must pass .threads = {role}'
        if any(role.startswith('Core::') for role in roles):
            assert '#include "core/core_threads.h"' in files[path], f'{path} must include core/core_threads.h'
    # lib_ffmpeg is an object library without Telegram headers.
    assert '"core/' not in files[GENERATOR] and 'Core::' not in files[GENERATOR], \
        'ffmpeg_frame_generator.cpp must not reach Telegram core'
    assert '"core/' not in files[UTILITY] and 'Core::' not in files[UTILITY], \
        'ffmpeg_utility.cpp must not reach Telegram core'


def check_descriptor(files):
    descriptor = body(files[HEADER], 'struct CodecDescriptor {')
    assert descriptor.rstrip().endswith('\tint threads = 0; // 0 = auto (capped for libvpx)\n}'), \
        'CodecDescriptor::threads must default to 0 and stay the last field'


def check_resolve(files):
    resolve = body(files[UTILITY], RESOLVE)
    user = resolve.index('OptionFFmpegThreadCount.value()')
    assert user < resolve.index('requested > 0') < resolve.index('libvpx-vp9'), \
        'the user override must precede the role count, which precedes the libvpx cap'
    make = body(files[UTILITY], 'CodecPointer MakeCodecPointer(CodecDescriptor descriptor) {')
    assert make.count('"threads"') == 1, 'MakeCodecPointer must set "threads" once'
    option = make.index('if (OptionFFmpegMultiThread.value()) {')
    multi = body(make[option:], 'if (OptionFFmpegMultiThread.value()) {')
    assert 'ResolveThreads(codec, descriptor.threads)' in multi and '"threads", threads' in multi, \
        'threads must be resolved per role under the multi-thread option'
    single = body(multi, 'if (threads == 1) {')
    assert 'context->thread_type = FF_THREAD_SLICE;' in single, 'one thread must use slice threading'
    assert make.index('= FindDecoder(context);') < option < make.index('avcodec_open2('), \
        'threads must be resolved after the decoder is known and before it opens'


def check_encoder(files):
    create = body(files[ENCODE], '[[nodiscard]] VideoEncoder CreateVideoEncoder(')
    count = create.index('\tencoder->thread_count = Core::Threads::Get().encoderThreads;\n')
    assert count < create.index('if (setup && !setup(encoder.get())) {') < create.index('avcodec_open2('), \
        'thread_count must be set before setup() can override it and before avcodec_open2'
    vp9 = body(files[ENCODE], '[[nodiscard]] bool SetupVp9StickerOptions(')
    assert 'av_opt_set_int(encoder->priv_data, "row-mt", 1, 0)' in vp9, 'VP9 must enable row-mt'
    # Stickers are 512px at most: libvpx's own width-clamped default keeps
    # their 2 tile columns, an explicit count would only lose them.
    assert 'tile-columns' not in vp9, 'VP9 must keep the libvpx tile-columns default'
    h264 = body(files[ENCODE], '[[nodiscard]] VideoEncoder CreateH264Encoder(')
    assert '\t\tnullptr);' not in h264, 'H.264 must pass a setup for slices'
    slices = body(h264, 'if (encoder->thread_count > 1) {')
    assert slices == 'if (encoder->thread_count > 1) {\n\t\t\t\tencoder->slices = std::min(encoder->thread_count, 4);\n\t\t\t}', \
        'H.264 must ask for min(thread_count, 4) slices when threaded'


# thread_count moved after avcodec_open2, where the encoder ignores it.
MOVE_BEFORE = (
    '\tencoder->thread_count = Core::Threads::Get().encoderThreads;\n'
    '\tif (setup && !setup(encoder.get())) {\n\t\treturn {};\n\t}\n'
    '\tif (output->oformat->flags & AVFMT_GLOBALHEADER) {\n'
    '\t\tencoder->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;\n\t}\n'
    '\tauto error = AvErrorWrap(avcodec_open2(\n\t\tencoder.get(),\n\t\tencoderCodec,\n\t\tnullptr));\n')
MOVE_AFTER = (
    '\tif (setup && !setup(encoder.get())) {\n\t\treturn {};\n\t}\n'
    '\tif (output->oformat->flags & AVFMT_GLOBALHEADER) {\n'
    '\t\tencoder->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;\n\t}\n'
    '\tauto error = AvErrorWrap(avcodec_open2(\n\t\tencoder.get(),\n\t\tencoderCodec,\n\t\tnullptr));\n'
    '\tencoder->thread_count = Core::Threads::Get().encoderThreads;\n')

CHECKS = (check_roles, check_descriptor, check_resolve, check_encoder)

MUTANTS = (
    (check_roles, GENERATOR, '\t\t.threads = 1,\n', ''),
    (check_roles, GENERATOR, '#include "ffmpeg/ffmpeg_utility.h"\n', '#include "ffmpeg/ffmpeg_utility.h"\n#include "core/core_threads.h"\n'),
    (check_roles, STREAMING, '.threads = Core::Threads::Get().streamingVideoDecode,', '.threads = 1,'),
    (check_roles, STREAMING, '\t\t\t.stream = info,\n\t\t\t.threads = 1,\n', '\t\t\t.stream = info,\n'),
    (check_roles, STREAMING, '#include "core/core_threads.h"\n', ''),
    (check_roles, FRAMES, '\t\t\t.threads = Core::Threads::Get().transcodeDecode,\n', ''),
    (check_roles, WAVEFORM, '\t\t.threads = 1,\n', ''),
    (check_roles, ENCODE, '{ .stream = stream, .threads = 1 }', '{ .stream = stream }'),
    (check_roles, ENCODE, '{ .stream = inStream, .threads = 1 }', '{ .stream = inStream }'),
    (check_roles, ENCODE, '\t\t\t.threads = Core::Threads::Get().transcodeDecode,\n', ''),
    (check_descriptor, HEADER, '\tint threads = 0; // 0 = auto (capped for libvpx)\n', ''),
    (check_resolve, UTILITY, 'if (const auto user = OptionFFmpegThreadCount.value(); user > 0) {\n\t\treturn user;\n\t} else if (requested > 0) {\n\t\treturn requested;\n\t}',
     'if (requested > 0) {\n\t\treturn requested;\n\t} else if (const auto user = OptionFFmpegThreadCount.value(); user > 0) {\n\t\treturn user;\n\t}'),
    (check_resolve, UTILITY, 'ResolveThreads(codec, descriptor.threads)', 'ResolveThreads(codec, 0)'),
    (check_resolve, UTILITY, '\t\t\tcontext->thread_type = FF_THREAD_SLICE;\n', ''),
    (check_resolve, UTILITY, '\tif (OptionFFmpegMultiThread.value()) {\n\t\tconst auto threads', '\t{\n\t\tconst auto threads'),
    (check_encoder, ENCODE, '\tencoder->thread_count = Core::Threads::Get().encoderThreads;\n\tif (setup && !setup(encoder.get())) {',
     '\tif (setup && !setup(encoder.get())) {'),
    (check_encoder, ENCODE, MOVE_BEFORE, MOVE_AFTER),
    (check_encoder, ENCODE, 'av_opt_set_int(encoder->priv_data, "row-mt", 1, 0)', 'av_opt_set_int(encoder->priv_data, "row-mt", 0, 0)'),
    (check_encoder, ENCODE, '\t\treturn false;\n\t}\n\treturn true;\n}\n\n[[nodiscard]] VideoEncoder CreateVp9WebmEncoder(',
     '\t\treturn false;\n\t}\n\tav_opt_set_int(encoder->priv_data, "tile-columns", 0, 0);\n\treturn true;\n}\n\n[[nodiscard]] VideoEncoder CreateVp9WebmEncoder('),
    (check_encoder, ENCODE, 'encoder->slices = std::min(encoder->thread_count, 4);', 'encoder->slices = encoder->thread_count;'),
    (check_encoder, ENCODE, 'encoder->slices = std::min(encoder->thread_count, 4);', 'encoder->slices = std::min(encoder->thread_count, 1);'),
    (check_encoder, ENCODE, 'if (encoder->thread_count > 1) {', 'if (encoder->thread_count > 4) {'),
    (check_encoder, ENCODE, '\t\t0.,\n\t\t[](not_null<AVCodecContext*> encoder) {\n\t\t\t// libopenh264',
     '\t\t0.,\n\t\tnullptr);\n\t[](not_null<AVCodecContext*> encoder) {\n\t\t\t// libopenh264'),
)


def check(root, read=source):
    files = {path: crlf(read(path)) for path in (HEADER, UTILITY, GENERATOR, STREAMING, FRAMES, WAVEFORM, ENCODE)}
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

    code, out = fixture(root, files, 'ffmpeg_budget')
    assert code == 0 and out.strip() == 'ffmpeg budget fixture ok', out
    for index, (path, before, after) in enumerate(LOGIC_MUTANTS):
        assert files[path].count(before) == 1, f'logic mutant anchor moved: {before}'
        mutated = dict(files)
        mutated[path] = files[path].replace(before, after)
        try:
            code, out = fixture(root, mutated, f'ffmpeg_budget_m{index}')
        except AssertionError:
            continue  # Did not compile: rejected as well.
        if code == 0:
            raise AssertionError(f'the budget fixture accepts the mutant: {after}')
    print('FFmpeg budget PASS: user override, role counts, libvpx cap 1..8, native auto, '
          'slice threading for one thread, VP9 row-mt with libvpx tiles, H.264 slices; every call site '
          'passes its role, lib_ffmpeg stays free of core, encoders get thread_count before setup')


if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='luxury-ffmpeg-budget-') as directory:
        check(Path(directory))
