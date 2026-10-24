"""ComposeControls edit-stars accessibility contract (source probe).

The icon-only edit-stars control changes the paid-message amount for live/video
stream composition. This bounded check verifies that its owning control exposes
an existing localized action name without claiming native Qt/UIA, rendering,
keyboard, or screen-reader acceptance.
"""
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'Telegram/SourceFiles/history/view/controls/history_view_compose_controls.cpp'


def check():
    source = SOURCE.read_text(encoding='utf-8')
    start = source.index('void ComposeControls::initEditStarsButton()')
    end = source.index('void ComposeControls::editStarsFrom(', start)
    body = source[start:end]
    if '_editStars = Ui::CreateChild<Ui::IconButton>' not in body:
        raise AssertionError(
            'RED: compose edit-stars action owner is missing')
    if 'setAccessibleName(' not in body:
        raise AssertionError(
            'RED: compose edit-stars action has no accessible name')
    if 'tr::lng_video_stream_edit_stars(tr::now)' not in body:
        raise AssertionError(
            'RED: compose edit-stars action is not named with localized '
            'Edit Stars text')
    if '_editStars->setClickedCallback' not in body:
        raise AssertionError(
            'RED: compose edit-stars action has no existing callback')


if __name__ == '__main__':
    try:
        check()
    except AssertionError as error:
        print(error)
        raise SystemExit(1)
    print(
        'Compose edit-stars accessible-name contract PASS; '
        'source-only, not native Qt/UIA acceptance')
