"""Paused voice drafts survive the automatic app lock.

Behind the lock every section close (LockWidget::showFinished and
MainWindow::hideNoQuit call clearSectionStack) went through, because
MainWidget::preventsCloseSection answered "no" while locked, and closing the
chat threw the record bar's draft away. Runs the real bodies against doubles;
no Qt, no capture device.
"""
import re
import sys
from pathlib import Path
import tempfile
from library_regressions import ROOT, cpp, function


MAIN = 'Telegram/SourceFiles/mainwidget.cpp'
BAR = 'Telegram/SourceFiles/history/view/controls/history_view_voice_record_bar.cpp'


def check(root, base=ROOT):
    base = Path(base)
    main = (base / MAIN).read_text(encoding='utf-8')
    bar = (base / BAR).read_text(encoding='utf-8')

    prevents = function(main, 'bool MainWidget::preventsCloseSection(Fn<void()> callback) const {')
    prevents_body = prevents[prevents.index('{') + 1:prevents.rindex('}')]
    if 'started()' not in prevents_body:
        raise AssertionError('RED: a locked app closes the chat even while a voice draft is alive')
    if '#include "media/audio/media_audio_capture.h"' not in main:
        raise AssertionError('RED: mainwidget.cpp does not include the capture header')

    start = bar.index('Core::App().passcodeLockChanges(')
    subscription = bar[start:bar.index('}, lifetime());', start) + len('}, lifetime());')]
    if 'stopRecording(StopType::Listen)' not in subscription:
        raise AssertionError('RED: a recording running at lock time is not paused into a draft')
    if '_draftGuardUntil' in bar:
        raise AssertionError('RED: the useless 1 s post-unlock hideFast guard is still there')

    stop = function(bar, 'void VoiceRecordBar::stopRecording(')
    listen = stop[stop.index('type == StopType::Listen'):stop.index('type == StopType::Send')]
    guarded = re.escape('if (!Core::App().passcodeLocked()) {') + r'\s*window\(\)->raise\(\);'
    if listen.count('window()->raise();') != 2 or len(re.findall(guarded, listen)) != 2:
        raise AssertionError('RED: pausing behind the lock raises the window over other apps')

    filters = [
        bar[bar.index('auto keyFilterCallback', bar.index('_listenChanges.events_starting_with(')):],
        function(bar, 'void VoiceRecordBar::installListenStateFilter('),
    ]
    for body in filters:
        head = body[:body.index('switch(e->type())')]
        if 'Core::App().passcodeLocked()' not in head:
            raise AssertionError('RED: a key typed into the passcode field reaches the voice draft behind the lock')

    cpp(root, 'voice_draft_autolock', r'''
#include <cassert>
#include <functional>
#include <iostream>
#include <utility>
template <class T> using Fn = std::function<T>;
namespace Media::Capture {
struct Instance { bool on = false; bool started() const { return on; } };
Instance *Current = nullptr;
inline Instance *instance() { return Current; }
}
struct Lifetime {};
namespace rpl {
template <class F> struct OnNext { F f; };
template <class F> OnNext<F> on_next(F f, Lifetime &) { return { f }; }
}
struct Changes { std::function<void(bool)> *sink; };
template <class F> void operator|(Changes c, rpl::OnNext<F> n) { *c.sink = n.f; }
struct App {
	bool locked = false; std::function<void(bool)> sink;
	bool passcodeLocked() const { return locked; }
	Changes passcodeLockChanges() { return { &sink }; }
};
App TheApp;
namespace Core { inline App &App() { return TheApp; } }
struct Section { int asks = 0; bool preventsClose(Fn<void()>) { ++asks; return true; } };
struct Main {
	Section *_mainSection = nullptr; Section *_history = nullptr;
	bool preventsCloseSection(Fn<void()> callback) const {
''' + prevents_body + r'''
	}
	int closes = 0;
	void clearSectionStack() { if (!preventsCloseSection([] {})) ++closes; }
};
enum class StopType { Cancel, Send, Listen };
struct Bar {
	bool recording = false, _fullRecord = false, _lockShowing = true;
	int listens = 0, cancels = 0; Lifetime life;
	Lifetime &lifetime() { return life; }
	bool isRecording() const { return recording; }
	void stopRecording(StopType type) {
		if (type == StopType::Listen) { ++listens; recording = false; } else { ++cancels; }
	}
	void subscribe() {
''' + subscription + r'''
	}
};
int main() {
	Section history;
	Main main; main._history = &history;
	Media::Capture::Instance capture;

	main.clearSectionStack();
	assert(main.closes == 0 && history.asks == 1);  // unlocked: the chat decides

	TheApp.locked = true;
	main.clearSectionStack();
	assert(main.closes == 1);  // locked, no capture device: closes as before
	Media::Capture::Current = &capture;
	main.clearSectionStack();
	assert(main.closes == 2);  // locked, nothing recorded: closes as before
	capture.on = true;         // recording or paused draft
	main.clearSectionStack();
	assert(main.closes == 2 && history.asks == 1);  // kept, and no discard box

	Bar bar; bar.subscribe();
	bar.recording = true;
	TheApp.sink(true);
	assert(bar.listens == 1 && bar.cancels == 0 && bar._fullRecord && !bar._lockShowing);
	TheApp.sink(true);
	assert(bar.listens == 1);  // already paused: untouched
	TheApp.sink(false);
	assert(bar.listens == 1 && bar.cancels == 0);
	std::cout << "Voice draft autolock PASS: locked closes keep the chat while capture is alive, running recording paused into a draft\n";
}
''')


if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='luxury-voice-autolock-') as directory:
        try:
            check(Path(directory), *sys.argv[1:2])
        except AssertionError as error:
            print(error)
            raise SystemExit(1)
