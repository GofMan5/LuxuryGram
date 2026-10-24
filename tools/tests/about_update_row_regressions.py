"""The About box update row says what the updater does and does nothing silently.

Bounded source check of the fork-added row; not a native acceptance.
"""
from pathlib import Path
import tempfile
from library_regressions import function, source


ABOUT = 'Telegram/SourceFiles/boxes/about_box.cpp'
CHECKER = 'Telegram/SourceFiles/core/update_checker.cpp'


def check(root):
    row = function(source(ABOUT), 'void AddUpdateRow(')
    guard = row[:row.index('verticalLayout()')]
    if 'cAutoUpdate()' not in guard:
        raise AssertionError(
            'RED: About shows "Check for updates" while automatic updates '
            'are off, and the updater ignores the click')
    assert 'Core::UpdaterDisabled()' in guard

    text = function(source(CHECKER), 'rpl::producer<QString> UpdateStateText(')
    progress = text[text.index('checker.progress()'):]
    progress = progress[:progress.index('checker.failed()')]
    initial = text[text.index('case UpdateChecker::State::Download:'):]
    initial = initial[:initial.index('case UpdateChecker::State::Ready:')]
    for name, part in (('progress', progress), ('initial', initial)):
        if 'UpdateDownloadText(' in part:
            raise AssertionError(
                'RED: About shows a bare "12 / 34 MB" for the '
                + name + ' download state, without "Downloading update"')
    assert 'tr::lng_settings_downloading_update(' in text
    print('About update row PASS: hidden without automatic updates, '
        'download state worded like settings')


if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='luxury-about-update-') as directory:
        check(Path(directory))
