"""Publishing the filter backup announces a plain link, not a sticker pack link.

Filters -> Export -> "To link" uploads the backup to dpaste and copies its URL.
The success toast reused lng_stickers_copied, whose English text is neutral
but whose server translations name the sticker pack ("Ссылка на набор
стикеров скопирована."), so Russian users were told a sticker link was copied.

Bounded source check; not a native acceptance.
"""
from pathlib import Path
import re
import tempfile
from library_regressions import function, source


FILTERS = 'Telegram/SourceFiles/luxury/features/filters/filters_utils.cpp'
SETTINGS = 'Telegram/SourceFiles/luxury/ui/settings/settings_luxury_utils.cpp'
STICKER_KEYS = ('lng_stickers_copied', 'lng_stickers_copied_emoji')


def check(root):
    publish = function(source(FILTERS), 'void FilterUtils::publishFilters(')
    success = publish[publish.index('QGuiApplication::clipboard()->setText(url);'):]
    success = success[:success.index('} else {')]
    keys = re.findall(r'tr::(lng_[a-z0-9_]+)\(', success)
    if any(key in STICKER_KEYS for key in keys):
        raise AssertionError(
            'RED: the filters publish toast says a sticker pack link was copied')
    assert keys == ['lng_channel_public_link_copied'], (
        'the filters publish toast must use the link key of the other fork link copies')
    assert 'tr::lng_channel_public_link_copied(' in source(SETTINGS), (
        'the fork link-copy toast moved; re-check which key it uses')
    print('Filters publish toast PASS: the copied dpaste link is announced '
        'as a link, not as a sticker pack link')


if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='luxury-filters-publish-toast-') as directory:
        check(Path(directory))
