"""Watcher event texts in Russian must not assume the watched user is male."""
from pathlib import Path
import re
import tempfile
from library_regressions import source


GENDERED = {
    'luxury_WatchGiftReceived': 'отправил(а)',
    'luxury_WatchNameChanged': 'Изменил(а)',
    'luxury_WatchUsernameChanged': 'Изменил(а)',
    'luxury_WatchPhotoUpdated': 'Обновил(а)',
}


def check(root):
    en = source('Telegram/Resources/langs/lang.strings')
    ru = source('Telegram/SourceFiles/luxury/luxury_settings.cpp')

    def english(key):
        return re.search(r'"' + key + r'" = "([^\n]*)";', en).group(1)

    def russian(key):
        return re.search(r'\{ "' + key + r'", "([^\n]*)" \}', ru).group(1)

    for key, verb in GENDERED.items():
        text = russian(key)
        assert verb in text, (key, text)
        assert sorted(re.findall(r'\{\w+\}', text)) == sorted(
            re.findall(r'\{\w+\}', english(key))), (key, text)
    anonymous = russian('luxury_WatchGiftReceivedAnonymous')
    assert anonymous.startswith('Неизвестный пользователь отправил '), anonymous
    print('Watcher RU event texts PASS: '
        + str(len(GENDERED))
        + ' third-person verbs use the (а) form, placeholders match EN')


if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='luxury-watcher-ru-') as directory:
        check(Path(directory))
