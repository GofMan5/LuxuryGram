"""The expanded player saves the full-size cover, not the Telegram thumbnail.

Issue #3. Save Cover and click-to-open wrote the image the player shows, and
for a song with a cover that is the small Telegram thumbnail. The audio file
itself keeps the artwork the uploader embedded, so the player now reads that
attached picture through FFmpeg and only falls back to the thumbnail, or to
the largest iTunes artwork of a song that has no Telegram cover.

Runs the real artwork-size choice over the extracted production function; the
FFmpeg extraction and both call sites are source contracts. Not native QA.
"""
from pathlib import Path
import re
import tempfile
from library_regressions import cpp, function, source

BOX = 'Telegram/SourceFiles/luxury/ui/boxes/expanded_player_box.cpp'
ITUNES = 'Telegram/SourceFiles/luxury/ui/utils/itunes_search.cpp'


def check(root, read=source):
    box = read(BOX)
    itunes = read(ITUNES)

    # Asking for 3000 must reach iTunes' 3000px artwork instead of being
    # clamped to the 600px preview the player displays.
    size = function(itunes, 'int ArtworkSize(')
    cpp(root, 'cover_artwork_size', r'''
#include <cassert>
#include <iostream>
''' + size + r'''
int main() {
    assert(ArtworkSize(3000) == 3000);
    assert(ArtworkSize(5000) == 3000);
    assert(ArtworkSize(600) == 600);
    assert(ArtworkSize(400) == 300);
    assert(ArtworkSize(300) == 300);
    assert(ArtworkSize(1) == 100);
    std::cout << "cover artwork size PASS: 3000 stays 3000\n";
}
''')

    saved = function(box, 'void ExpandedPlayerBox::saveCover()')
    opened = function(box, 'void ExpandedPlayerBox::openCover()')
    for name, body in (('saveCover', saved), ('openCover', opened)):
        assert 'BestCoverImage(' in body, f'{name} does not resolve the best cover'
        assert body.index('BestCoverImage(') < body.index('.save('), (
            f'{name} writes the shown image instead of the resolved one')

    extract = function(box, 'QImage ExtractEmbeddedCover(')
    assert 'ReadFileWrap' in extract
    assert 'AV_DISPOSITION_ATTACHED_PIC' in extract
    assert 'stream->attached_pic' in extract
    assert 'kCoverSourceLimit' in extract
    assert 'QImage::fromData' in extract

    best = function(box, 'QImage BestCoverImage(')
    assert 'ExtractEmbeddedCover(' in best
    assert 'FetchCover(' in best
    assert best.index('ExtractEmbeddedCover(') < best.index('FetchCover('), (
        'iTunes is queried before the file itself')
    assert 'allowItunes' in best

    # 3000x3000 is requested, so the pixel cap must admit it, and an entry
    # larger than the whole cover cache must not be inserted and wipe it.
    cap = int(re.search(r'kMaxArtworkPixels = (\d+) \* 1024 \* 1024', itunes).group(1))
    assert cap * 1024 * 1024 >= 3000 * 3000, cap
    assert 'cost <= kArtworkCacheKiB' in itunes


if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='luxury-cover-full-') as directory:
        check(Path(directory))
