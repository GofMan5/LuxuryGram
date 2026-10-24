"""Scheduled-send action accessibility contract (source probe).

The scheduled-to-user box exposes an icon-only top action that opens the
existing Send until online menu. This bounded check verifies its localized
accessible name without claiming native Qt/UIA, rendering, keyboard, or
screen-reader acceptance.
"""
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'Telegram/SourceFiles/history/view/history_view_schedule_box.cpp'


def check():
    source = SOURCE.read_text(encoding='utf-8')
    start = source.index('void FillSendUntilOnlineMenu(')
    end = source.index('\nconstexpr auto kNotifyRestFrame', start)
    owner = source[start:end]
    if 'button->setClickedCallback(' not in owner:
        raise AssertionError(
            'RED: scheduled send-until-online action has no click callback')
    if 'button->setAccessibleName(' not in owner:
        raise AssertionError(
            'RED: scheduled send-until-online action has no accessible name')
    if 'tr::lng_scheduled_send_until_online(tr::now)' not in owner:
        raise AssertionError(
            'RED: scheduled send-until-online action is not named with '
            'localized menu text')


if __name__ == '__main__':
    try:
        check()
    except AssertionError as error:
        print(error)
        raise SystemExit(1)
    print(
        'Scheduled send-until-online accessible-name contract PASS; '
        'source-only, not native Qt/UIA acceptance')
