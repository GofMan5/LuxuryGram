"""Suggested-message price box close-button accessibility contract (source probe).

The non-admin suggested-message price box creates an icon-only close button
for its custom title row. This bounded check verifies the existing close action
is named without claiming native Qt/UIA, rendering, keyboard, or screen-reader
acceptance.
"""
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'Telegram/SourceFiles/history/view/controls/history_view_suggest_options.cpp'


def check():
    source = SOURCE.read_text(encoding='utf-8')
    start = source.index('void ChooseSuggestPriceBox(')
    end = source.index('\nbool CanEditSuggestedMessage', start)
    body = source[start:end]
    if 'const auto close = Ui::CreateChild<Ui::IconButton>' not in body:
        raise AssertionError(
            'RED: suggested-options close owner is missing')
    if 'close->setClickedCallback([=] { box->closeBox(); });' not in body:
        raise AssertionError(
            'RED: suggested-options close action callback is missing')
    if 'close->setAccessibleName(tr::lng_close(tr::now));' not in body:
        raise AssertionError(
            'RED: suggested-options close action has no accessible name')


if __name__ == '__main__':
    try:
        check()
    except AssertionError as error:
        print(error)
        raise SystemExit(1)
    print(
        'Suggested-options close accessible-name contract PASS; '
        'source-only, not native Qt/UIA acceptance')
