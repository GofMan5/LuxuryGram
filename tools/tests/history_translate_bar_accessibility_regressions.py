"""History translate-bar settings action accessibility contract (source probe).

The translation bar's icon-only settings action opens a menu. This bounded
check verifies its localized accessible name without claiming native Qt/UIA,
rendering, keyboard, or screen-reader acceptance.
"""
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'Telegram/SourceFiles/history/view/history_view_translate_bar.cpp'


def check():
    source = SOURCE.read_text(encoding='utf-8')
    start = source.index(
        'const auto settings = Ui::CreateChild<Ui::IconButton>('
    )
    end = source.index('\n\t_wrap.sizeValue()', start)
    owner = source[start:end]
    if 'settings->setClickedCallback(' not in owner:
        raise AssertionError(
            'RED: history translate-bar settings action has no click callback')
    if 'settings->setAccessibleName(' not in owner:
        raise AssertionError(
            'RED: history translate-bar settings action has no accessible name')
    if 'tr::lng_translate_settings(tr::now)' not in owner:
        raise AssertionError(
            'RED: history translate-bar settings action is not named with '
            'localized translation-settings text')


if __name__ == '__main__':
    try:
        check()
    except AssertionError as error:
        print(error)
        raise SystemExit(1)
    print(
        'History translate-bar settings accessible-name contract PASS; '
        'source-only, not native Qt/UIA acceptance')
