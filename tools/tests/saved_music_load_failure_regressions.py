"""SavedMusic failure stays retryable instead of becoming a false empty success."""
from pathlib import Path
from library_regressions import source


def check(root):
    header = source('Telegram/SourceFiles/data/data_saved_music.h')
    impl = source('Telegram/SourceFiles/data/data_saved_music.cpp')
    widget = source('Telegram/SourceFiles/info/saved/info_saved_music_widget.cpp')

    assert 'bool loadFailed(PeerId peerId) const;' in header
    assert 'bool loadFailed = false;' in header
    failure = impl[impl.index('}).fail([=](const MTP::Error &error) {'):]
    assert 'entry.loadFailed = true;' in failure
    assert 'const auto reloaded = base::take(entry.reloading);' in failure
    assert 'entry.loaded = reloaded;' in failure
    assert 'entry.loaded = true;' not in failure
    assert 'entry.total = int(entry.list.size());' not in failure
    success = impl[impl.index(')).done([=](const MTPusers_SavedMusic &result) {'):impl.index('}).fail([=](const MTP::Error &error) {')]
    assert 'entry.loadFailed = false;' in impl
    assert 'savedMusic->loadFailed(_peer->id)' in widget
    # UI-097: the fork-only error keys live in the luxury_ namespace so the RU overlay covers them.
    assert 'tr::luxury_SavedMusicRetry()' in widget and 'tr::luxury_SavedMusicLoadError(' in widget
    assert 'lng_media_saved_music_retry' not in widget and 'lng_media_saved_music_load_error' not in widget
    assert 'savedMusic->loadMore(_peer->id)' in widget
    assert 'savedMusic->loadMore(peerId)' in impl
    assert 'hasBefore < limit && !savedMusic->loadFailed(peerId)' in impl


if __name__ == '__main__':
    check(Path.cwd())
    print('SavedMusic failure/retry GREEN contract PASS')
