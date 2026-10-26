"""Clear Watcher Data confirms in a standard confirmation box.

The chat menu built this box by hand: the question was a bare row with
st::boxRowPadding (no top inset) in a box without a title, so its first line
sat st::boxTopMargin (8 px) under the box edge, where Ui::ConfirmBox, used by
the Clear Deleted Messages item next to it, leaves 22 px; the "Also delete
saved media files" checkbox was glued to the last line of the question.
In Russian the menu title used as the confirm label pushed Cancel past the
box edge and the one-line checkbox label was cut.

Bounded source check; not a native acceptance.
"""
from pathlib import Path
import re
import tempfile
from library_regressions import function, source


MENU = 'Telegram/SourceFiles/luxury/ui/context_menu/context_menu.cpp'
LANG = 'Telegram/Resources/langs/lang.strings'
RU = 'Telegram/SourceFiles/luxury/luxury_settings.cpp'


def ordered(text, needles, message):
    position = -1
    for needle in needles:
        found = text.find(needle, position + 1)
        assert found > position, f'{message}: {needle!r} is missing or out of order'
        position = found


def clear_box(menu):
    at = menu.index('tr::luxury_ClearOnlineHistoryText(')
    start = menu.rindex('Box([=](not_null<Ui::GenericBox*> box)', 0, at)
    return function(menu[start:], 'Box([=](not_null<Ui::GenericBox*> box)')


def check(root):
    menu = source(MENU)
    box = clear_box(menu)
    if 'object_ptr<Ui::FlatLabel>' in box:
        raise AssertionError('RED: the Clear Watcher Data question is a bare row '
            'in an untitled box, 8 px under its top edge instead of 22 px')
    assert 'Ui::ConfirmBox(box, {' in box, 'the box must be a Ui::ConfirmBox'
    assert '.confirmStyle = &st::attentionBoxButton' in box, (
        'clearing is destructive: the confirm button must keep the attention style')
    assert '.confirmText = tr::luxury_WatcherClearAction(' in box, (
        'the confirm button must be the short verb: the menu title pushed '
        'Cancel past the left edge of the 320 px box in Russian')
    for path, entry in (
            (LANG, '"luxury_WatcherClearAction" = "Clear";'),
            (RU, '{ "luxury_WatcherClearAction", "Очистить" },')):
        assert entry in source(path), f'{path} must define {entry}'
    assert '(*alsoFiles)->setAllowTextLines();' in box, (
        'the checkbox label must wrap: on one line the Russian label was cut '
        'to "Также удалить сохранённые фай..."')
    assert 'const auto skip = st::defaultBoxCheckbox.margin.top();' in box, (
        'the checkbox row needs the vertical skip upstream gives such checkboxes')
    assert re.search(
        r'object_ptr<Ui::Checkbox>\(\s*box,\s*tr::luxury_WatcherClearKeptFilesAlso\(tr::now\),'
        r'\s*true,\s*st::defaultBoxCheckbox\),\s*'
        r'st::boxRowPadding \+ QMargins\(0, skip, 0, skip\)\)', box), (
        'the also-delete-files checkbox must stay checked by default and be padded')
    assert re.search(
        r'if \(\*alsoFiles && \(\*alsoFiles\)->checked\(\)\) \{\s*'
        r'LuxuryFeatures::Watch::clearKeptMediaForDialog\([^;]*\);\s*\}', box), (
        'saved media files must be deleted only while the checkbox is checked')
    ordered(box, (
        'LuxuryFeatures::Watch::clearKeptMediaForDialog(',
        'LuxuryOnline::clearHistory(',
        'close();',
    ), 'files, then history, then the box closes')

    action = menu[menu.index('.text = tr::luxury_ClearOnlineHistoryMenuText(tr::now)'):]
    action = action[:action.index('});')]
    assert '.handler = ClearWatcherDataHandler(' in action and '.isAttention = true' in action, (
        'the menu item must open the shared handler and stay an attention item')
    print('Watcher clear box PASS: Ui::ConfirmBox with the attention confirm, '
        'padded checked-by-default files checkbox, files cleared only when checked')


if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='luxury-watcher-clear-box-') as directory:
        check(Path(directory))
