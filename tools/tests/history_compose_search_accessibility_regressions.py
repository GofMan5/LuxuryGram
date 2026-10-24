"""Compose-search icon-only controls expose localized accessible names.

The search surface paints a cancel action and a result-navigation bar with
icon-only buttons plus an empty-text results-list toggle. This bounded source
check proves the owning controls are named without claiming native Qt/UIA,
rendering, keyboard, or screen-reader acceptance.
"""
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'Telegram/SourceFiles/history/view/controls/history_view_compose_search.cpp'


REQUIRED_NAMES = (
    ('_cancel', 'tr::lng_sr_cancel_search(tr::now)'),
    ('_previous', 'tr::luxury_SearchPreviousResult(tr::now)'),
    ('_next', 'tr::luxury_SearchNextResult(tr::now)'),
    ('_jumpToDate', 'tr::lng_calendar(tr::now)'),
    ('_chooseFromUser', 'tr::lng_search_messages_from(tr::now)'),
    # Empty-text FlatButton spanning the bar; toggles the results list.
    ('_showList', 'tr::luxury_SearchResultsList(tr::now)'),
)
NEW_KEYS = (
    'luxury_SearchPreviousResult',
    'luxury_SearchNextResult',
    'luxury_SearchResultsList',
)


def check(root=None, path=SOURCE):
    """root is the shared temp dir passed by ui_client_regressions; unused."""
    source = Path(path).read_text(encoding='utf-8')
    for field, localized_name in REQUIRED_NAMES:
        marker = f'{field}->setAccessibleName('
        if source.count(marker) != 1:
            raise AssertionError(
                f'RED: compose-search action {field} has no unique accessible name')
        if localized_name not in source:
            raise AssertionError(
                f'RED: compose-search action {field} is not named with localized text')

    strings = (ROOT / 'Telegram/Resources/langs/lang.strings').read_text(encoding='utf-8')
    overlay = (ROOT / 'Telegram/SourceFiles/luxury/luxury_settings.cpp').read_text(encoding='utf-8')
    for key in NEW_KEYS:
        if f'"{key}" = ' not in strings or f'{{ "{key}", ' not in overlay:
            raise AssertionError(f'RED: {key} missing from lang.strings or RU overlay')

    # Keep the owning action streams and state guards intact.
    for marker in (
        'return _cancel->clicks() | rpl::to_empty;',
        'return _jumpToDate->clicks() | rpl::to_empty;',
        'return _chooseFromUser->clicks() | rpl::to_empty;',
        'return _showList->clicks() | rpl::to_empty;',
        'return _current.changes() | rpl::map(rpl::mappers::_1 - 1);',
    ):
        if marker not in source:
            raise AssertionError(f'GREEN guard missing existing compose-search stream: {marker}')


if __name__ == '__main__':
    try:
        check(None, *sys.argv[1:2])
    except AssertionError as error:
        print(error)
        raise SystemExit(1)
    print(
        'Compose-search cancel/calendar/from/previous/next/results-list accessible-name contract PASS; '
        'source-only, not native Qt/UIA acceptance')
