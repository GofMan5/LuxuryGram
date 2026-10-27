"""Expanded player cover opens externally; Space pauses and resumes playback.

Issues #1 and #2. The cover in the expanded player was a plain picture: the
only way to look at it full size was Save Cover and a file manager. Space did
nothing for the player anywhere in the window.

The cover is now a button (pointer cursor, disabled while only the "no cover"
placeholder shows) that writes the image as PNG into the account temp folder
and hands it to the system viewer. Space goes through the application event
filter next to the "?" keys legend hotkey and toggles the active track, but
only for the first receiver of the key, in the active primary window, with no
modifiers, no passcode lock, no screen reader mode, and focus not in a text
input, a Tab-focusable control or a Settings section (its arrow-key
navigation activates rows with Space), and only when a track is loaded: the compose
field and every other input still get their space.

Runs the real Space decision over Qt/player doubles; the cover part is a
source contract. Not a native acceptance.
"""
from pathlib import Path
import tempfile
from library_regressions import ROOT, cpp, function, source


APPLICATION = 'Telegram/SourceFiles/core/application.cpp'
BOX = 'Telegram/SourceFiles/luxury/ui/boxes/expanded_player_box.cpp'
BOX_HEADER = 'Telegram/SourceFiles/luxury/ui/boxes/expanded_player_box.h'

PROGRAM = r'''
#include <cstdio>
#include <cstring>
#include <string>
template <typename T>
struct not_null {
    T ptr;
    not_null(T p) : ptr(p) {}
    T get() const { return ptr; }
    T operator->() const { return ptr; }
};
namespace Qt {
enum Key { Key_Space = 0x20, Key_A = 0x41 };
enum KeyboardModifier { NoModifier = 0, ShiftModifier = 1, ControlModifier = 2, KeypadModifier = 4, GroupSwitchModifier = 8 };
enum FocusPolicy { NoFocus = 0, TabFocus = 1, ClickFocus = 2, StrongFocus = 3 };
} // namespace Qt
struct QObject {
    std::string cls = "QWidget";
    virtual ~QObject() = default;
    bool inherits(const char *name) const { return cls == name; }
};
struct QWidget : QObject {
    QWidget *top = nullptr;
    Qt::FocusPolicy policy = Qt::NoFocus;
    QWidget *window() { return top ? top : this; }
    Qt::FocusPolicy focusPolicy() const { return policy; }
};
namespace Settings {
struct AbstractSection : QWidget {};
} // namespace Settings
struct QKeyEvent {
    int k = Qt::Key_Space;
    int mods = Qt::NoModifier;
    bool repeat = false;
    int key() const { return k; }
    int modifiers() const { return mods; }
    bool isAutoRepeat() const { return repeat; }
};
QWidget *Focus = nullptr;
QWidget *ActiveWindow = nullptr;
struct QApplication {
    static QWidget *focusWidget() { return Focus; }
    static QWidget *activeWindow() { return ActiveWindow; }
};
bool ScreenReader = false;
namespace Ui {
bool ScreenReaderModeActive() { return ScreenReader; }
} // namespace Ui
namespace Window {
struct Controller {
    QWidget *main = nullptr;
    QWidget *widget() { return main; }
};
} // namespace Window
namespace Media::Player {
enum class State { Stopped, Stopping, Playing, Paused };
inline bool IsStoppedOrStopping(State state) {
    return state == State::Stopped || state == State::Stopping;
}
struct TrackState { State state = State::Stopped; };
struct Instance {
    State state = State::Stopped;
    int toggles = 0;
    int getActiveType() const { return 1; }
    TrackState getState(int) const { return { state }; }
    void playPause(int) { ++toggles; }
};
Instance Player;
not_null<Instance*> instance() { return &Player; }
} // namespace Media::Player
namespace Core {
// The real Core has a Settings class that hides ::Settings.
class Settings {};
struct Application {
    bool locked = false;
    Window::Controller *window = nullptr;
    bool passcodeLocked() const { return locked; }
    Window::Controller *activeWindow() const { return window; }
};
HANDLER
} // namespace Core

int failures = 0;
void Expect(bool ok, const char *what) {
    if (!ok) {
        std::printf("FAIL %s\n", what);
        ++failures;
    }
}

int main() {
    using Media::Player::Player;
    using Media::Player::State;
    QWidget main, list, field, button, viewer, viewerChild;
    Settings::AbstractSection settings;
    list.top = field.top = button.top = settings.top = &main;
    viewerChild.top = &viewer;
    field.cls = "QTextEdit";
    field.policy = Qt::StrongFocus;
    button.policy = Qt::TabFocus;
    auto controller = Window::Controller{ &main };
    auto app = Core::Application{ false, &controller };
    const auto press = [&](QObject *receiver, QKeyEvent event = {}) {
        const auto before = Player.toggles;
        const auto eaten = Core::HandleSpacePlayPause(&app, receiver, &event);
        return std::pair(eaten, Player.toggles - before);
    };
    const auto reset = [&] {
        Focus = &list;
        ActiveWindow = &main;
        ScreenReader = false;
        app.locked = false;
        Player.state = State::Playing;
    };

    reset();
    Expect(press(&list) == std::pair(true, 1), "Space on the message list toggles a playing track");
    Player.state = State::Paused;
    Expect(press(&list) == std::pair(true, 1), "Space resumes a paused track");
    reset();
    Expect(press(&main) == std::pair(false, 0), "the same Space propagated to the window is not toggled twice");
    Focus = nullptr;
    Expect(press(&main) == std::pair(true, 1), "with nothing focused the window receives Space and toggles");

    reset();
    Focus = &field;
    Expect(press(&field) == std::pair(false, 0), "a space typed in the compose field is never eaten");
    field.cls = "QLineEdit";
    Expect(press(&field) == std::pair(false, 0), "a space typed in a line edit is never eaten");
    field.cls = "QTextEdit";
    Focus = &button;
    Expect(press(&button) == std::pair(false, 0), "a Tab-focused control keeps Space to activate itself");
    Focus = &settings;
    Expect(press(&settings) == std::pair(false, 0), "Settings keyboard navigation keeps Space to activate the selected row");

    reset();
    Player.state = State::Stopped;
    Expect(press(&list) == std::pair(false, 0), "without a loaded track Space keeps its usual meaning");
    Player.state = State::Stopping;
    Expect(press(&list) == std::pair(false, 0), "a stopping track is not restarted");

    reset();
    Expect(press(&list, { Qt::Key_Space, Qt::ShiftModifier }) == std::pair(false, 0), "Shift+Space is left alone");
    Expect(press(&list, { Qt::Key_Space, Qt::ControlModifier }) == std::pair(false, 0), "Ctrl+Space is left alone");
    Expect(press(&list, { Qt::Key_A }) == std::pair(false, 0), "other keys are left alone");
    Expect(press(&list, { Qt::Key_Space, Qt::KeypadModifier }) == std::pair(true, 1), "a keypad or layout-group flag on Space still toggles");
    Expect(press(&list, { Qt::Key_Space, Qt::NoModifier, true }) == std::pair(true, 0), "held Space is swallowed without toggling again");

    reset();
    Focus = &viewerChild;
    ActiveWindow = &viewer;
    Expect(press(&viewerChild) == std::pair(false, 0), "the media viewer and other windows keep their own Space");
    reset();
    ScreenReader = true;
    Expect(press(&list) == std::pair(false, 0), "screen reader navigation keeps Space");
    reset();
    app.locked = true;
    Expect(press(&list) == std::pair(false, 0), "the passcode lock screen keeps Space");
    reset();
    app.window = nullptr;
    Expect(press(&list) == std::pair(false, 0), "no active primary window, no toggle");

    if (failures) {
        return 1;
    }
    std::printf("Space play/pause PASS: toggles a loaded track outside text fields and controls, once per press, only in the main window\n");
}
'''


def check(root):
    application = source(APPLICATION)
    handler = function(application, 'bool HandleSpacePlayPause(')
    filter_body = function(application, 'bool Application::eventFilter(')
    legend = filter_body.index('HandleKeysLegendHotkey(this, object, event)')
    space = filter_body.index('HandleSpacePlayPause(this, object, event)')
    assert legend < space, 'Space play/pause is not wired into the app key filter after the keys legend'

    box = source(BOX)
    cover = box[box.index('class ExpandedPlayerBox::CoverWidget final'):box.index('class ExpandedPlayerBox::TimeWidget')]
    assert 'public Ui::AbstractButton' in cover, 'the cover is not clickable (no pointer cursor, no click)'
    assert 'setDisabled(_image.isNull());' in cover, 'the "no cover" placeholder would still look clickable'
    assert '_cover->setClickedCallback([=] { openCover(); });' in box, 'a click on the cover opens nothing'
    open_cover = function(box, 'void ExpandedPlayerBox::openCover()')
    for needle in ('_cover->image()', 'tempDirectory()', 'image.save(path)', 'File::Launch(path)', 'luxury_PlayerSaveCoverFailed'):
        assert needle in open_cover, f'openCover lost {needle}'
    assert open_cover.index('image.save(path)') < open_cover.index('File::Launch(path)'), 'the viewer is launched before the file exists'
    assert 'void openCover();' in source(BOX_HEADER)

    cpp(root, 'space_play_pause', PROGRAM.replace('HANDLER', handler))


if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='luxury-player-cover-space-') as directory:
        check(Path(directory))
