"""Message shot and luxurygram:// link boxes open in the window they came from.

The legacy global Ui::show targets the last active primary window and, by
default, closes every box already open there. Bounded source check of the
owners; not a native multi-window acceptance.
"""
import sys
from library_regressions import function, source


SHOT = 'Telegram/SourceFiles/luxury/features/message_shot/message_shot.cpp'
SHOT_BOX = 'Telegram/SourceFiles/luxury/ui/boxes/message_shot_box.cpp'
LINKS = 'Telegram/SourceFiles/luxury/luxury_url_handlers.cpp'
MAIN_MENU = 'Telegram/SourceFiles/window/window_main_menu.cpp'
THEME_BOX = 'Telegram/SourceFiles/luxury/ui/boxes/theme_selector_box.cpp'
PLAYER_BOX = 'Telegram/SourceFiles/luxury/ui/boxes/expanded_player_box.cpp'


def check():
    shot = source(SHOT)
    show = function(shot, 'void ShowMessageShotBox(')
    if 'Ui::show(' in show:
        raise AssertionError('RED: message shot box goes to the primary window and closes its boxes')
    assert show.count('controller->show(std::move(box));') == 1
    wrapper = function(shot, 'void WrapperImpl(')
    if 'Core::App().findWindow(widget)' not in wrapper:
        raise AssertionError('RED: message shot resolves the primary window, not the one with the selection')
    assert 'session->tryResolveWindow()' in wrapper

    box = source(SHOT_BOX)
    if 'Ui::show(' in box:
        raise AssertionError('RED: message shot theme picker opens through the global Ui::show')
    assert box.count('_config.controller->show(std::move(box));') == 1

    links = source(LINKS)
    for name, message in (
            ('bool ResolveUser(', 'luxury_UserNotFoundMessage'),
            ('bool ResolveChat(', 'luxury_ChatNotFoundMessage')):
        body = function(links, name)
        if 'Ui::show(' in body:
            raise AssertionError('RED: ' + name + ' "not found" box closes the boxes open in the window')
        assert body.count(
            'controller->show(Ui::MakeInformBox(tr::' + message + '()));') == 1

    # Ui::Toast::Show without a parent lands in the primary window.
    for path in (SHOT, SHOT_BOX, THEME_BOX, PLAYER_BOX):
        if 'Ui::Toast::Show(' in source(path):
            raise AssertionError('RED: ' + path + ' shows a toast in the primary window')
    assert 'crl::on_main(controller, [=] {' in box
    assert 'crl::on_main(controller, [=] {' in source(PLAYER_BOX)

    menu = source(MAIN_MENU)
    if 'Ui::show(' in menu:
        raise AssertionError('RED: drawer "read all" confirmation opens through the global Ui::show')
    read = menu.index('tr::luxury_ReadConfirmationBoxQuestion()')
    assert 'controller->show(std::move(box));' in menu[read:read + 400]


if __name__ == '__main__':
    try:
        check()
    except AssertionError as error:
        print(error)
        raise SystemExit(1)
    print('Fork box window contract PASS; source-only, not native multi-window acceptance')
