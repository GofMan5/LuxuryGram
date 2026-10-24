"""Focused source contract for the clickable connection-state status widget.

The widget opens proxy settings on press. A StatusBar role is not keyboard/
AT-focusable in RpWidget's shared accessibility policy, so that action was
hidden from screen-reader keyboard users despite the existing click handler.
Source-only; native Qt/UIA acceptance is still required.
"""
from pathlib import Path

SOURCE = Path(__file__).parents[2] / "Telegram/SourceFiles/window/window_connecting_widget.cpp"


def check():
    source = SOURCE.read_text(encoding="utf-8")
    widget = source[source.index("class ConnectionState::Widget") : source.index("class ConnectionState::Widget::ProxyIcon")]
    if "QAccessible::Role::Button" not in widget:
        raise AssertionError(
            "RED: clickable connection-state owner is not exposed as a focusable Button"
        )
    if "QAccessible::Role::StatusBar" in widget:
        raise AssertionError(
            "RED: clickable connection-state owner still reports non-focusable StatusBar"
        )
    if "addClickHandler([=]" not in source or "CreateOwningBox(account)" not in source:
        raise AssertionError(
            "RED: connection-state proxy action contract is not present"
        )
    if "setAccessibleName(_currentLayout.text);" not in source:
        raise AssertionError(
            "RED: connection-state owner has no current localized status name"
        )


if __name__ == "__main__":
    try:
        check()
    except AssertionError as error:
        print(error)
        raise SystemExit(1)
    print(
        "Connection-state accessibility GREEN contract PASS; "
        "source-only, not native Qt/UIA acceptance"
    )
