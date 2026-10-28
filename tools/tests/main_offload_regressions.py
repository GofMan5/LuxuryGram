"""Video thumbnails and custom emoji caches decode off the GUI thread.

- Gif::validateVideoThumbnail ran FFmpeg (PrepareForSending) on the GUI
  thread while painting: it now decodes in crl::async and delivers the frame
  through crl::on_main(weak), once per request.
- The custom emoji cache lookup LZ4-decoded every cached sprite sheet on the
  shared database queue: FromSerialized now runs in crl::async.
- The custom emoji frame generator read the sticker file on the main thread
  while building the factory: ReadContent now runs inside the factory, which
  lib_ui invokes in crl::async.

The Qt half is checked statically, each check against a mutant it must
reject; a g++ model of the async/on_main(weak) handoff proves a dropped
owner skips delivery and a repaint loop spawns one job.
Bounded source check; not a native acceptance.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile
from library_regressions import CXX, ROOT, STATIC, function, run, source, standalone

SRC = 'Telegram/SourceFiles/'
GIF = SRC + 'history/view/media/history_view_gif.cpp'
GIF_H = SRC + 'history/view/media/history_view_gif.h'
EMOJI = SRC + 'data/stickers/data_custom_emoji.cpp'
CLIP = SRC + 'media/clip/media_clip_reader.cpp'
CLIP_FFMPEG = SRC + 'media/clip/media_clip_ffmpeg.cpp'
CACHE = 'Telegram/lib_ui/ui/text/custom_emoji_instance.cpp'

# Off-main code must not reach main-thread-only state.
MAIN_ONLY = re.compile(r'\b(Core::App\(\)|session\(\)|_session\b|_owner\b|Main::Session|Data::Session)')


def balanced(block, name):
    assert block.count('(') == block.count(')'), f'{name}: unbalanced parentheses'
    assert block.count('{') == block.count('}'), f'{name}: unbalanced braces'


def check_gif(files):
    assert 'mutable bool _videoThumbnailRequested = false;' in files[GIF_H], 'no requested flag'
    text = files[GIF]
    assert '#include "media/clip/media_clip_reader.h"' in text and '#include "ui/image/image.h"' in text
    fn = function(text, 'void Gif::validateVideoThumbnail() const {')
    balanced(fn, 'validateVideoThumbnail')
    assert 'crl::async([=, weak = base::make_weak(const_cast<Gif*>(this))] {' in fn, 'decode is not async with a weak owner'
    job = function(fn, 'crl::async(')
    outside = fn.replace(job, '')
    assert 'PrepareForSending(' in job and 'PrepareForSending(' not in outside, 'FFmpeg runs outside the async job'
    assert 'crl::on_main(weak, ' in job, 'the frame is not delivered through the weak guard'
    deliver = function(job, 'crl::on_main(weak, ')
    assert 'std::make_unique<Image>' in deliver and fn.count('std::make_unique<Image>') == 1, 'Image is built off main'
    assert 'Image::BlankMedia()' in deliver and 'repaint();' in deliver
    assert 'if (_videoThumbnailFrame || !_dataMedia) {\n\t\t\t\treturn;' in deliver, 'delivery does not re-validate state'
    assert deliver.index('_videoThumbnailRequested = false;') < deliver.index('return;'), 'the flag stays set after delivery'
    for name in ('_dataMedia', '_videoThumbnailFrame', '_videoThumbnailRequested', 'repaint'):
        assert name not in job.replace(deliver, ''), f'the worker touches {name}'
    guard = fn.index('|| _videoThumbnailRequested\n')
    assert guard < fn.index('return;') < fn.index('_videoThumbnailRequested = true;') < fn.index('crl::async('), \
        'the requested flag is not checked and set before dispatch'


def check_emoji(files):
    text = files[EMOJI]
    lookup = function(text, 'void CustomEmojiLoader::startCacheLookup(')
    balanced(lookup, 'startCacheLookup')
    assert 'const auto weak = base::make_weak(&lookup->process->guard);' in lookup, 'the weak guard changed'
    get = function(lookup, 'cacheBigFile().get(key, ')
    job = function(get, 'crl::async(')
    assert 'FromSerialized(' in job and 'FromSerialized(' not in get.replace(job, ''), 'LZ4 decode runs on the DB queue'
    assert 'crl::on_main(weak, ' in job and 'lookupDone(lookup, std::move(result));' in function(job, 'crl::on_main(weak, ')
    assert 'lookupDone' not in job.replace(function(job, 'crl::on_main(weak, '), ''), 'lookupDone runs off main'
    check = function(text, 'void CustomEmojiLoader::check() {')
    balanced(check, 'check')
    start = check.index('auto generator = [')
    captures = check[start:check.index(']', start)]
    assert 'ReadContent' not in captures, 'the sticker file is read on main while capturing'
    assert 'Lottie::ReadContent(data, filepath)' in function(check, 'auto generator = ['), 'the factory does not read the content'


def check_pure(files):
    # PrepareForSending and FromSerialized run on worker threads.
    prepare = function(files[CLIP], 'Ui::PreparedFileInformation PrepareForSending(')
    for name, block in (('PrepareForSending', prepare), ('media_clip_ffmpeg.cpp', files[CLIP_FFMPEG])):
        found = MAIN_ONLY.search(block)
        assert not found, f'{name} reaches main-thread state: {found.group(0)}'
    if CACHE in files:
        parse = function(files[CACHE], 'std::optional<Cache> Cache::FromSerialized(')
        assert not MAIN_ONLY.search(parse), 'FromSerialized reaches main-thread state'


CHECKS = (check_gif, check_emoji, check_pure)

MUTANTS = (
    # PrepareForSending moved back out of async.
    (check_gif, GIF, '''	_videoThumbnailRequested = true;
	crl::async([=, weak = base::make_weak(const_cast<Gif*>(this))] {
		auto info = v::get<Ui::PreparedFileInformation::Video>(
			::Media::Clip::PrepareForSending(QString(), content).media);
''', '''	_videoThumbnailRequested = true;
	auto info = v::get<Ui::PreparedFileInformation::Video>(
		::Media::Clip::PrepareForSending(QString(), content).media);
	crl::async([=, weak = base::make_weak(const_cast<Gif*>(this))] {
'''),
    # on_main without weak.
    (check_gif, GIF, 'crl::on_main(weak, [=, image', 'crl::on_main([=, image'),
    (check_gif, GIF, 'crl::async([=, weak = base::make_weak(const_cast<Gif*>(this))] {', 'crl::async([=, weak = this] {'),
    # Requested flag removed.
    (check_gif, GIF, '\t\t|| _videoThumbnailRequested\n', ''),
    (check_gif, GIF, '\t_videoThumbnailRequested = true;\n', ''),
    (check_gif, GIF, '\t\t\t_videoThumbnailRequested = false;\n', ''),
    (check_gif, GIF_H, '\tmutable bool _videoThumbnailRequested = false;\n', ''),
    (check_gif, GIF, '\t\t\tif (_videoThumbnailFrame || !_dataMedia) {', '\t\t\tif (false) {'),
    (check_gif, GIF, '\t\t\trepaint();\n', ''),
    (check_emoji, EMOJI, '''		crl::async([=, bytes = std::move(value)] {
			auto cache = Ui::CustomEmoji::Cache::FromSerialized(bytes, size);
''', '''		auto cache = Ui::CustomEmoji::Cache::FromSerialized(value, size);
		crl::async([=, bytes = std::move(value)] {
'''),
    (check_emoji, EMOJI, 'crl::on_main(weak, [=, result', 'crl::on_main([=, result'),
    (check_emoji, EMOJI, 'auto generator = [=]() -> std::unique_ptr<Ui::FrameGenerator> {\n\t\tconst auto bytes = Lottie::ReadContent(data, filepath);\n',
     'auto generator = [=, bytes = Lottie::ReadContent(data, filepath)]() -> std::unique_ptr<Ui::FrameGenerator> {\n'),
    (check_pure, CLIP, '\tauto localData = QByteArray(data);\n', '\tauto localData = QByteArray(data);\n\tCore::App().settings();\n'),
)

MODEL = r'''
#include <cstdlib>
#include <deque>
#include <functional>
#include <iostream>
#include <memory>

#define EXPECT(condition) do { if (!(condition)) { \
	std::cerr << "line " << __LINE__ << ": " #condition "\n"; std::abort(); } } while (0)

// crl::async / crl::on_main(weak, ...) with explicit queues.
std::deque<std::function<void()>> Pool, Main;
using Weak = std::weak_ptr<bool>;

void Async(std::function<void()> job) { Pool.push_back(std::move(job)); }
void OnMain([[maybe_unused]] Weak weak, std::function<void()> job) {
	Main.push_back([=] {
#ifndef DELIVER_UNGUARDED
		if (weak.expired()) {
			return;
		}
#endif
		job();
	});
}
void Drain(std::deque<std::function<void()>> &queue) {
	while (!queue.empty()) {
		auto job = std::move(queue.front());
		queue.pop_front();
		job();
	}
}

int Decodes = 0;
int Delivered = 0;

struct Gif {
	std::shared_ptr<bool> alive = std::make_shared<bool>(true);
	bool requested = false;
	bool frame = false;

	void validate() {
		if (frame || requested) {
			return;
		}
		requested = true;
		Async([this, weak = Weak(alive)] {
			++Decodes;
			OnMain(weak, [this] {
				++Delivered;
				requested = false;
				frame = true;
			});
		});
	}
};

int main() {
	{
		auto gif = std::make_unique<Gif>();
		gif->validate();
		gif->validate(); // Repaint before the frame arrives.
		EXPECT(Pool.size() == 1);
		Drain(Pool);
		Drain(Main);
		EXPECT(gif->frame && !gif->requested && Delivered == 1);
		gif->validate();
		EXPECT(Pool.empty());
	}
	{
		auto gif = std::make_unique<Gif>();
		gif->validate();
		Drain(Pool);
		gif->alive = nullptr; // The view is destroyed while the result is queued.
		Drain(Main);
		EXPECT(Decodes == 2 && Delivered == 1);
	}
	std::cout << "main offload model ok\n";
}
'''


def model(root, name, defines=()):
    path = root / (name + '.cpp')
    path.write_text(MODEL, encoding='utf-8')
    exe = root / (name + ('.exe' if os.name == 'nt' else ''))
    run([CXX, '-std=c++20', '-O0', '-Wall', '-Wextra', '-Werror', *defines, str(path), *STATIC, '-o', str(exe)], root)
    standalone(exe)
    return exe


def crlf(text):
    return text.replace('\r\n', '\n')


def check(root, read=source):
    paths = [GIF, GIF_H, EMOJI, CLIP, CLIP_FFMPEG]
    if (ROOT / CACHE).exists():  # lib_ui is a submodule: absent in a bare worktree.
        paths.append(CACHE)
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

    out = run([str(model(root, 'main_offload'))], root)
    assert out.strip() == 'main offload model ok', out
    unguarded = model(root, 'main_offload_unguarded', ('-DDELIVER_UNGUARDED',))
    result = subprocess.run([str(unguarded)], cwd=root, capture_output=True, text=True)
    assert result.returncode != 0, 'the model accepts delivery to a destroyed owner'
    print('Main offload PASS: video thumbnails decode in crl::async once per request and land through a weak guard, '
          'custom emoji caches decode off the DB queue, sticker files are read inside the async factory'
          + ('' if CACHE in files else ' (FromSerialized purity skipped: lib_ui not checked out)'))


if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='luxury-main-offload-') as directory:
        check(Path(directory))
