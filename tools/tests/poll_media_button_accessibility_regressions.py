"""Poll media button accessibility contract (source probe).

PollMediaButton is the shared icon-only media-action owner used by poll
creation and poll-option editing. This bounded check verifies that its
existing localized Attach action name is exposed without claiming native
Qt/UIA, rendering, keyboard, or screen-reader acceptance.
"""
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'Telegram/SourceFiles/poll/poll_media_upload.cpp'


def check():
    source = SOURCE.read_text(encoding='utf-8')
    start = source.index('PollMediaButton::PollMediaButton(')
    end = source.index('\n\nvoid PollMediaButton::setIconColorOverride', start)
    owner = source[start:end]
    if 'setPointerCursor(true)' not in owner:
        raise AssertionError(
            'RED: poll media button action has no pointer/click setup')
    if 'setAccessibleName(' not in owner:
        raise AssertionError(
            'RED: poll media button action has no accessible name')
    if 'tr::lng_attach(tr::now)' not in owner:
        raise AssertionError(
            'RED: poll media button action is not named with localized '
            'Attach text')


if __name__ == '__main__':
    try:
        check()
    except AssertionError as error:
        print(error)
        raise SystemExit(1)
    print(
        'Poll media button accessible-name contract PASS; '
        'source-only, not native Qt/UIA acceptance')
