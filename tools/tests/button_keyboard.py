"""Compile the production submit/press/release methods against a state seam.
Not Qt integration: setDown and lifetime invalidation use small doubles.
Run: python tools/tests/button_keyboard.py (requires g++ on PATH).
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'Telegram/lib_ui/ui/abstract_button.cpp').read_text('utf-8')
start = 'bool AbstractButton::isSubmitEvent(not_null<QKeyEvent*> e) const'
method = start + source.split(start, 1)[1].split('\nvoid AbstractButton::clicked(', 1)[0]
program = r'''
#include <cassert>
#include <iostream>
namespace Qt {
    constexpr auto LeftButton = 1;
    constexpr auto Key_Space = 32;
    constexpr auto Key_Return = 13;
    constexpr auto Key_Enter = 10;
}
template <typename T> using not_null = T;
struct QKeyEvent {
    bool accepted = false;
    bool repeat = false;
    int code = Qt::Key_Space;
    void accept() { accepted = true; }
    int modifiers() const { return 0; }
    bool isAutoRepeat() const { return repeat; }
    int key() const { return code; }
};
struct RpWidget {
    void keyPressEvent(QKeyEvent*) {}
    void keyReleaseEvent(QKeyEvent*) {}
};
struct AbstractButton;
namespace base {
    struct Weak {
        const bool *alive = nullptr;
        explicit operator bool() const { return *alive; }
    };
    Weak make_weak(AbstractButton *button);
}
struct AbstractButton : RpWidget {
    enum class StateChangeSource { ByPress };
    bool alive = true;
    bool over = false;
    bool down = false;
    bool destroyOnClick = false;
    int clicks = 0;
    bool isSubmitEvent(not_null<QKeyEvent*> e) const;
    bool isDown() const { return down; }
    bool isOver() const { return over; }
    void clicked(int, int) {
        ++clicks;
        if (destroyOnClick) alive = false;
    }
    // setDown's actual contract: hovered release calls clicked itself.
    void setDown(bool value, StateChangeSource, int mods, int button) {
        down = value;
        if (!value && over) clicked(mods, button);
    }
    void keyPressEvent(QKeyEvent*);
    void keyReleaseEvent(QKeyEvent*);
};
base::Weak base::make_weak(AbstractButton *button) { return { &button->alive }; }
'''
program += method + r'''
int main() {
    for (const auto key : { Qt::Key_Space, Qt::Key_Return, Qt::Key_Enter }) {
        for (const auto hovered : { false, true }) {
            for (const auto destroys : { false, true }) {
                AbstractButton button;
                button.over = hovered;
                button.destroyOnClick = destroys;
                QKeyEvent event;
                event.code = key;
                button.keyPressEvent(&event);
                assert(button.down && event.accepted && button.clicks == 0);
                QKeyEvent repeat;
                repeat.code = key;
                repeat.repeat = true;
                button.keyPressEvent(&repeat);
                button.keyReleaseEvent(&repeat);
                assert(button.down && button.clicks == 0 && !repeat.accepted);
                button.keyReleaseEvent(&event);
                assert(button.clicks == 1);
                assert(!button.down && event.accepted);
            }
        }
    }
    AbstractButton released;
    QKeyEvent event;
    released.keyReleaseEvent(&event);
    assert(released.clicks == 0);
    AbstractButton other;
    event.accepted = false;
    event.code = 65;
    other.keyPressEvent(&event);
    other.keyReleaseEvent(&event);
    assert(other.clicks == 0 && !other.down && !event.accepted);
    std::cout << "Production keyboard methods: PASS (Space/Enter/Return, hover, no hover, callback invalidation, repeat, not pressed, other key)\n";
}
'''
with tempfile.TemporaryDirectory(prefix='luxury-button-test-') as tmp:
    cpp = Path(tmp) / 'check.cpp'
    exe = Path(tmp) / 'check.exe'
    cpp.write_text(program, encoding='utf-8')
    subprocess.run(['g++', '-std=c++20', '-Wall', '-Wextra', '-Werror', str(cpp), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
