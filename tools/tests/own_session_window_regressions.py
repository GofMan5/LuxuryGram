"""Fork actions talk to the window of their own account.

The translator settings rows dereferenced Core::App().activeWindow(), which is
null while the app is in the background, and unarchiving with "hide All
chats" switched the filter of whatever account's window was active.
"""
from library_regressions import function, source

GENERAL = 'Telegram/SourceFiles/luxury/ui/settings/settings_general.cpp'
API = 'Telegram/SourceFiles/apiwrap.cpp'


def check(root=None, read=source):
    translator = function(read(GENERAL), 'void BuildTranslator(')
    assert 'activeWindow()' not in translator
    assert translator.count('const auto controller = builder.controller();') == 1
    assert translator.count('if (controller) {') == 2
    archive = function(read(API), 'void ApiWrap::toggleHistoryArchived(')
    assert 'if (controller && (&controller->session() == _session)) {' in archive
    assert 'filters->lookupId(_session->premium() ? 0 : 1)' in archive
    print('Own session window PASS: translator rows and the unarchive hook use their own account window')


if __name__ == '__main__':
    check()
