"""Fork chat-menu sections open in the window the menu was opened from.

Session::tryResolveWindow() without a peer returns the primary window, so
"History", "User Messages" and "View Deleted" from a separate chat window
landed in the main window. Bounded source check; not a native multi-window
acceptance.
"""
from library_regressions import function, source


MENU = 'Telegram/SourceFiles/luxury/ui/context_menu/context_menu.cpp'
HEADER = 'Telegram/SourceFiles/luxury/ui/context_menu/context_menu.h'
INNER = 'Telegram/SourceFiles/history/history_inner_widget.cpp'
LIST = 'Telegram/SourceFiles/history/view/history_view_context_menu.cpp'


def check():
    menu = source(MENU)
    deleted = function(menu, 'void AddLuxuryGramActions(')
    if 'tryResolveWindow()' in deleted:
        raise AssertionError('RED: "View Deleted" opens in the primary window')
    assert 'sessionController->showSection(' in deleted

    for name in ('AddHistoryAction', 'AddUserMessagesAction'):
        body = function(menu, f'void {name}(')
        if 'tryResolveWindow()' in body:
            raise AssertionError(f'RED: {name} opens in the primary window')
        assert 'crl::guard(controller,' in body, name

    assert 'controller->showSection(' in function(menu, 'void AddHistoryAction(')
    assert 'controller->searchInChat(' in function(
        menu,
        'void AddUserMessagesAction(')

    header = source(HEADER)
    for name in ('AddHistoryAction', 'AddUserMessagesAction'):
        assert (f'void {name}(\n\tnot_null<Ui::PopupMenu*> menu,\n'
            '\tnot_null<Window::SessionController*> controller,\n'
            '\tHistoryItem *item);') in header, name

    inner = source(INNER)
    assert 'LuxuryUi::AddHistoryAction(_menu, controller, item);' in inner
    assert 'LuxuryUi::AddUserMessagesAction(_menu, controller, item);' in inner

    listing = source(LIST)
    assert ('LuxuryUi::AddHistoryAction(menu, list->controller(), '
        'request.item);') in listing
    assert ('LuxuryUi::AddUserMessagesAction(menu, list->controller(), '
        'request.item);') in listing


if __name__ == '__main__':
    try:
        check()
    except AssertionError as error:
        print(error)
        raise SystemExit(1)
    print('Fork menu window contract PASS; source-only, not native multi-window acceptance')
