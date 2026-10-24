"""Screen-reader mode bar must expose its disable control.

The bar is shown specifically while screen-reader mode is active and its only
control opens the confirmation flow that disables the mode. Hiding that painted
button from accessibility leaves the user with no discoverable exit. This is a
source contract, not native Qt/UIA acceptance.
"""
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "Telegram/SourceFiles/ui/controls/window_screen_reader_bar.cpp"


def check():
    source = SOURCE.read_text(encoding="utf-8")
    required = (
        "QAccessible::Role::AlertMessage",
        "return tr::lng_screen_reader_bar_text(tr::now);",
        "accessibilityChildWidgets() const override",
        "return { _disable.data() };",
        "QAccessible::Role::Button",
        "return _text;",
    )
    missing = [marker for marker in required if marker not in source]
    if missing:
        raise AssertionError(
            "RED: screen-reader bar owner does not expose its disable control: "
            + ", ".join(missing))
    if ("int Bar::accessibilityChildCount() const" in source
            or "int accessibilityChildCount() const override;" in source):
        raise AssertionError(
            "RED: screen-reader bar owner declares a child-count override "
            "instead of exposing its actionable child")


if __name__ == "__main__":
    try:
        check()
    except AssertionError as error:
        print(error)
        raise SystemExit(1)
    print(
        "Screen-reader bar accessibility GREEN contract PASS; "
        "source-only, not native Qt/UIA acceptance")
