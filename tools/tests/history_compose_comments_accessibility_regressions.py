"""ComposeControls comments toggle accessibility contract (source probe).

The icon-only comments toggle is used by the live/video stream reply composer.
This bounded check verifies that its owning control exposes an existing
localized action name without claiming native Qt/UIA, rendering, keyboard, or
screen-reader acceptance.
"""
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'Telegram/SourceFiles/history/view/controls/history_view_compose_controls.cpp'


def check():
    source = SOURCE.read_text(encoding='utf-8')
    start = source.index('void ComposeControls::setToggleCommentsButton(')
    end = source.index('rpl::producer<> ComposeControls::commentsShownToggles()', start)
    body = source[start:end]
    if '_commentsShown = Ui::CreateChild<Ui::IconButton>' not in body:
        raise AssertionError(
            'RED: compose comments toggle owner is missing')
    if 'setAccessibleName(' not in body:
        raise AssertionError(
            'RED: compose comments toggle has no accessible name')
    if 'setAccessibleName(tr::lng_comments_header_none(tr::now))' not in body:
        raise AssertionError(
            'RED: compose comments toggle is not named with localized '
            'Comments text')
    if '_commentsShown->setClickedCallback' not in body:
        raise AssertionError(
            'RED: compose comments toggle has no existing callback')


if __name__ == '__main__':
    try:
        check()
    except AssertionError as error:
        print(error)
        raise SystemExit(1)
    print(
        'Compose comments-toggle accessible-name contract PASS; '
        'source-only, not native Qt/UIA acceptance')
