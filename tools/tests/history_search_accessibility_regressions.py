"""Admin/message-history search controls accessibility contract (source probe).

Both history fixed-bar owners expose icon-only search and cancel actions. This
bounded check verifies their localized names without claiming native Qt/UIA,
rendering, keyboard, or screen-reader acceptance.
"""
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SOURCES = (
    ROOT / 'Telegram/SourceFiles/history/admin_log/history_admin_log_section.cpp',
    ROOT / 'Telegram/SourceFiles/luxury/ui/message_history/history_section.cpp',
)


def check():
    for source_path in SOURCES:
        source = source_path.read_text(encoding='utf-8')
        if source.count('_search->setClickedCallback(') != 1:
            raise AssertionError(
                f'RED: expected one admin/message-history search action in {source_path.name}')
        if source.count('_cancel->setClickedCallback(') != 1:
            raise AssertionError(
                f'RED: expected one admin/message-history cancel action in {source_path.name}')
        if source.count('_search->setAccessibleName(') != 1:
            raise AssertionError(
                f'RED: icon-only history search action has no accessible name in {source_path.name}')
        if source.count('_cancel->setAccessibleName(') != 1:
            raise AssertionError(
                f'RED: icon-only history cancel action has no accessible name in {source_path.name}')
        if 'tr::lng_dlg_filter(tr::now)' not in source:
            raise AssertionError(
                f'RED: history search action is not named with localized filter text in {source_path.name}')
        if 'tr::lng_sr_cancel_search(tr::now)' not in source:
            raise AssertionError(
                f'RED: history cancel action is not named with localized cancel text in {source_path.name}')


if __name__ == '__main__':
    try:
        check()
    except AssertionError as error:
        print(error)
        raise SystemExit(1)
    print(
        'Admin/message-history search accessible-name contract PASS; '
        'source-only, not native Qt/UIA acceptance')
