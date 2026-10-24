"""Self-forwards folder toast exposes a localized name for its icon-only menu button.

Bounded source check of the single owner SelfForwardsTagger::createRightButton;
not native Qt/UIA, toast rendering, menu or folder-mutation acceptance.
"""
import sys
from library_regressions import function, source


OWNER = 'Telegram/SourceFiles/history/view/history_view_self_forwards_tagger.cpp'


def check(root=None, path=OWNER):
    """root is the shared temp dir passed by ui_client_regressions; unused."""
    text = source(path)
    body = function(text, 'SelfForwardsTagger::createRightButton(')
    if body.count('button->setAccessibleName(tr::lng_filters_menu_add(tr::now));') != 1:
        raise AssertionError('RED: self-forwards folder toast button has no localized accessible name')
    assert 'st::joinChatAddToFilterToastButton' in body
    # The owner remains the sole construction path and still opens the folder menu.
    assert text.count('createRightButton(widget)') == 1
    assert 'FillChooseFilterMenu(_controller, menu, history);' in text


if __name__ == '__main__':
    try:
        check(None, *sys.argv[1:2])
    except AssertionError as error:
        print(error)
        raise SystemExit(1)
    print('Self-forwards folder toast button accessible-name contract PASS; source-only, not native Qt/UIA acceptance')
