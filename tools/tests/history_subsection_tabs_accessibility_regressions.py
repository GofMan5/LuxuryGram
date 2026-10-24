"""History subsection-tabs accessibility contract (source RED/GREEN probe).

The tabs-layout toggle is an icon-only actionable control. This bounded
source check verifies that both horizontal and vertical owning-model creation
paths give that button a localized accessible name. It does not exercise Qt,
UIA, rendering, keyboard input, or native screen readers.
"""
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'Telegram/SourceFiles/history/view/history_view_subsection_tabs.cpp'


def check():
    source = SOURCE.read_text(encoding='utf-8')
    creation = 'Ui::CreateChild<Ui::IconButton>'
    if source.count(creation) != 2:
        raise AssertionError(
            'RED: expected horizontal and vertical subsection-tab toggle owners')
    if source.count('toggle->setAccessibleName(') != 2:
        raise AssertionError(
            'RED: icon-only subsection-tab layout toggles have no accessible names')
    if 'tr::lng_edit_topics_layout(tr::now)' not in source:
        raise AssertionError(
            'RED: subsection-tab layout toggle is not named with localized text')


if __name__ == '__main__':
    try:
        check()
    except AssertionError as error:
        print(error)
        raise SystemExit(1)
    print(
        'History subsection-tabs accessible-name contract PASS; '
        'source-only, not native Qt/UIA acceptance')
