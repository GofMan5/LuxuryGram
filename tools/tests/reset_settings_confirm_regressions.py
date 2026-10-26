"""Reset Settings confirms with a destructive, self-describing button.

The question asks whether to reset all LuxuryGram preferences, which cannot be
undone, yet the confirm button read a generic "Yes" in the default style, so it
looked no more dangerous than Cancel next to it. Upstream confirms destructive
actions with the action's verb in st::attentionBoxButton (Delete, Terminate,
Clear Deleted Messages in the same menu family).

Bounded source check; not a native acceptance.
"""
from pathlib import Path
import tempfile
from library_regressions import source


OTHER = 'Telegram/SourceFiles/luxury/ui/settings/settings_other.cpp'
LANG = 'Telegram/Resources/langs/lang.strings'
RU = 'Telegram/SourceFiles/luxury/luxury_settings.cpp'


def reset_box(text):
    start = text.index('.id = u"luxury/resetSettings"_q,')
    end = text.index('\n\t});', start)
    return text[start:end]


def check(root):
    box = reset_box(source(OTHER))
    assert 'Ui::MakeConfirmBox({' in box, 'Reset Settings must still ask first'
    if '.confirmText = tr::lng_box_yes()' in box:
        raise AssertionError('RED: resetting every preference is confirmed by a '
            'generic "Yes" in the default style')
    assert '.confirmText = tr::luxury_BoxActionReset(),' in box, (
        'the confirm button must name the action')
    assert '.confirmStyle = &st::attentionBoxButton,' in box, (
        'the reset cannot be undone: the confirm button needs the attention style')
    for path, entry in (
            (LANG, '"luxury_BoxActionReset" = "Reset";'),
            (RU, '{ "luxury_BoxActionReset", "Сбросить" },')):
        assert entry in source(path), f'{path} must define {entry}'
    confirmed = box[box.index('.confirmed = '):box.index('.confirmText = ')]
    assert 'LuxurySettings::reset()' in confirmed and 'close();' in confirmed, (
        'the confirm callback must still reset and close the box')
    print('Reset settings confirm PASS: Reset verb in the attention style, '
        'reset and close kept')


if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='luxury-reset-settings-') as directory:
        check(Path(directory))
