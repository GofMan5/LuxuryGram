"""Fork copy actions confirm the copy with a toast in the window they ran in.

Bounded source check of the fork-added copy actions; not a native acceptance.
"""
from library_regressions import function, source


STICKER_SET = 'Telegram/SourceFiles/boxes/sticker_set_box.cpp'
SETTINGS_MAIN = 'Telegram/SourceFiles/settings/sections/settings_main.cpp'
SUBTEXT = 'Telegram/SourceFiles/luxury/ui/context_menu/menu_item_subtext.cpp'
DETAILS = 'Telegram/SourceFiles/luxury/ui/context_menu/context_menu.cpp'


def after(text, needle, size):
    start = text.index(needle)
    return text[start:start + size]


def check():
    sticker = function(
        source(STICKER_SET),
        'void StickerSetBox::Inner::contextMenuEvent(')
    copy_id = after(sticker, 'QString::number(_pack[index]->id)', 200)
    if 'showToast' not in copy_id:
        raise AssertionError('RED: emoji "Copy ID" in a sticker set copies silently')
    assert '_show->showToast(tr::luxury_IDCopiedToast(tr::now));' in copy_id

    cover = after(source(SETTINGS_MAIN), 'auto id = IDString(_user);', 200)
    if 'showToast' not in cover:
        raise AssertionError('RED: settings "Copy ID" copies silently')
    assert '_controller->showToast(tr::luxury_IDCopiedToast(tr::now));' in cover

    subtext = source(SUBTEXT)
    default = after(subtext, 'setText(subtext);', 200)
    if 'showToast' not in default:
        raise AssertionError('RED: message details rows copy silently')
    assert 'window->showToast(tr::lng_text_copied(tr::now));' in default
    author = after(subtext, 'setText(QString::number(authorId));', 300)
    assert 'Core::App().activeWindow()' in author
    assert 'tryResolveWindow' not in author

    details = function(source(DETAILS), 'void AddMessageDetailsAction(')
    name = after(details, 'setText(mediaName);', 200)
    if 'showToast' not in name:
        raise AssertionError('RED: message details file name copies silently')
    assert 'window->showToast(tr::lng_text_copied(tr::now));' in name


if __name__ == '__main__':
    try:
        check()
    except AssertionError as error:
        print(error)
        raise SystemExit(1)
    print('Fork copy feedback contract PASS; source-only, not native acceptance')
