"""Poll add-option emoji button accessibility contract (source probe).

The add-option overlay's emoji icon opens the existing emoji-only panel. This
bounded check verifies that the actionable owner exposes an existing localized
name without claiming native Qt/UIA, rendering, keyboard, or screen-reader
acceptance.
"""
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'Telegram/SourceFiles/history/view/history_view_add_poll_option.cpp'


def check():
    source = SOURCE.read_text(encoding='utf-8')
    field_start = source.index('void AddPollOptionWidget::setupField()')
    panel_start = source.index('void AddPollOptionWidget::setupEmojiPanel()', field_start)
    field = source[field_start:panel_start]
    panel_end = source.index('void AddPollOptionWidget::setupAttach()', panel_start)
    panel = source[panel_start:panel_end]
    if '_emoji = Ui::CreateChild<Ui::IconButton>' not in field:
        raise AssertionError(
            'RED: poll add-option emoji action owner is missing')
    if 'setAccessibleName(' not in field:
        raise AssertionError(
            'RED: poll add-option emoji action has no accessible name')
    if 'tr::lng_article_insert_emoji(tr::now)' not in field:
        raise AssertionError(
            'RED: poll add-option emoji action is not named with localized '
            'Emoji text')
    if '_emoji->addClickHandler' not in panel:
        raise AssertionError(
            'RED: poll add-option emoji action has no existing panel callback')


if __name__ == '__main__':
    try:
        check()
    except AssertionError as error:
        print(error)
        raise SystemExit(1)
    print(
        'Poll add-option emoji accessible-name contract PASS; '
        'source-only, not native Qt/UIA acceptance')
