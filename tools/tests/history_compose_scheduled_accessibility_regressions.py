"""Composer scheduled-message toggle accessibility contract (source probe).

The icon-only scheduled-message toggle appears when a chat has scheduled
messages and opens the existing scheduled-message section. This bounded check
verifies that its owning control exposes the same localized action name as the
existing scheduled-message toggle elsewhere, without claiming native Qt/UIA,
rendering, keyboard, or screen-reader acceptance.
"""
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'Telegram/SourceFiles/history/view/controls/history_view_compose_controls.cpp'


def check():
    source = SOURCE.read_text(encoding='utf-8')
    marker = 'if (!_scheduled && hasScheduled) {'
    start = source.index(marker)
    end = source.index('} else if (_scheduled && !hasScheduled) {', start)
    body = source[start:end]
    if '_scheduled = base::make_unique_q<Ui::IconButton>' not in body:
        raise AssertionError(
            'RED: composer scheduled-message toggle owner is missing')
    if '_scheduled->setAccessibleName(' not in body:
        raise AssertionError(
            'RED: composer scheduled-message toggle has no accessible name')
    if 'tr::lng_scheduled_messages(tr::now)' not in body:
        raise AssertionError(
            'RED: composer scheduled-message toggle is not named with '
            'localized scheduled-messages text')
    if '_showScheduledRequests' not in body:
        raise AssertionError(
            'RED: composer scheduled-message toggle lost its existing callback')


if __name__ == '__main__':
    try:
        check()
    except AssertionError as error:
        print(error)
        raise SystemExit(1)
    print(
        'Composer scheduled-message toggle accessible-name contract PASS; '
        'source-only, not native Qt/UIA acceptance')
