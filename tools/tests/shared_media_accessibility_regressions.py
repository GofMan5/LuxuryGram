"""Shared-media accessibility ownership contract (source RED/GREEN probe).

This is a bounded source-only check. It verifies that the painted shared-media
list owns a real accessible list model and keyboard navigation, rather than
relying on host-only role/focus patches. It does not exercise Qt, UIA, paint,
Narrator, a profile, network, selection, or media opening.
"""
from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / 'Telegram/SourceFiles/info/media/info_media_list_widget.h'
SOURCE = ROOT / 'Telegram/SourceFiles/info/media/info_media_list_widget.cpp'


def check():
    header = HEADER.read_text(encoding='utf-8')
    source = SOURCE.read_text(encoding='utf-8')
    required_header = (
        r'QAccessible::Role accessibilityRole\(\) override',
        r'Qt::FocusPolicy accessibilityFocusPolicy\(\) override',
        r'int accessibilityChildCount\(\) const override',
        r'QString accessibilityChildName\(int index\) const override',
        r'QAccessible::State accessibilityChildState\(int index\) const override',
        r'QRect accessibilityChildRect\(int index\) const override',
        r'bool accessibilityChildSupportsActions\(int index\) const override',
        r'quintptr accessibilityChildIdentity\(int index\) const override',
        r'int accessibilityChildIndexByIdentity\(quintptr identity\) const override',
        r'void accessibilityChildSetFocus\(quintptr identity\) override',
        r'void accessibilityChildActivate\(quintptr identity\) override',
        r'void focusInEvent\(QFocusEvent \*e\) override',
        r'void keyPressEvent\(QKeyEvent \*e\) override',
    )
    missing = [pattern for pattern in required_header
               if not re.search(pattern, header)]
    if missing:
        raise AssertionError(
            'RED: ListWidget lacks owning shared-media accessibility/keyboard '
            f'contract ({len(missing)} declarations missing)')
    for marker in (
        'accessibilityChildIndexByIdentity(identity)',
        'crl::on_main(this',
        'openPhoto(',
        'openDocument(',
    ):
        if marker not in source:
            raise AssertionError(f'GREEN contract missing live action marker: {marker}')


if __name__ == '__main__':
    try:
        check()
    except AssertionError as error:
        print(error)
        raise SystemExit(1)
    print('Shared-media owning accessibility contract PASS; source-only, not native Qt/UIA acceptance')
