"""The open chat refreshes once per burst of block/filter changes.

Loading the block list fires IsBlocked for every peer, and each event rebuilt
every view of the open chat, even with "hide from blocked" off.
"""
from library_regressions import source

WIDGET = 'Telegram/SourceFiles/history/history_widget.cpp'


def check(root=None, read=source):
    text = read(WIDGET)
    start = text.index('const auto filtersRefreshQueued = lifetime().make_state<bool>(false);')
    block = text[start:text.index('largeEmojiChanges(', start)]
    assert 'settings.filtersEnabled() && settings.hideFromBlocked()' in block
    assert block.index('Flag::IsBlocked') < block.index('hideFromBlocked()') < block.index('FiltersCacheController::updates()')
    assert 'return !std::exchange(*filtersRefreshQueued, true);' in block
    assert block.index('crl::on_main(') < block.index('*filtersRefreshQueued = false;') < block.index('if (_history) {')
    print('History filters refresh PASS: block changes only matter with blocked-hiding on, a burst refreshes once')


if __name__ == '__main__':
    check()
