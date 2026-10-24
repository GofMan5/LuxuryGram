"""Filter toasts appear in the window the filter was edited from.

A parentless Ui::Toast::Show lands in the primary window. Bounded source
check; not a native multi-window acceptance.
"""
from library_regressions import function, source


TOASTS = 'Telegram/SourceFiles/luxury/ui/toasts.cpp'
EDIT = 'Telegram/SourceFiles/luxury/ui/settings/filters/edit_filter.cpp'
LIST = 'Telegram/SourceFiles/luxury/ui/settings/filters/settings_filters_list.cpp'
SECTION = 'Telegram/SourceFiles/luxury/ui/settings/settings_filters.cpp'


def check():
    toasts = source(TOASTS)
    if '::Ui::Toast::Show(' in toasts:
        raise AssertionError('RED: filter toasts go to the primary window')
    assert 'show->showToast(std::move(config));' in toasts
    assert 'show->showToast(tr::luxury_DatabaseError(tr::now));' in toasts

    edit = source(EDIT)
    if 'Ui::Toast::Show(' in edit or 'ShowDatabaseError();' in edit:
        raise AssertionError('RED: "filter added" toast ignores the box window')
    close = edit.index('box->closeBox();')
    assert 'const auto show = box->uiShow();' in edit[close - 80:close]
    assert edit.count('ShowDatabaseError(show);') == 2

    listing = source(LIST)
    if 'ShowDatabaseError();' in listing:
        raise AssertionError('RED: filters list errors ignore the section window')
    assert listing.count('ShowDatabaseError(show);') == 4
    assert 'const auto show = _controller->uiShow();' in listing

    menu = function(source(SECTION), 'void LuxuryFilters::fillTopBarMenu(')
    if 'ShowDatabaseError();' in menu:
        raise AssertionError('RED: "clear filters" error ignores the section window')
    assert 'const auto show = controller()->uiShow();' in menu
    assert 'ShowDatabaseError(show);' in menu


if __name__ == '__main__':
    try:
        check()
    except AssertionError as error:
        print(error)
        raise SystemExit(1)
    print('Filter toast window contract PASS; source-only, not native multi-window acceptance')
