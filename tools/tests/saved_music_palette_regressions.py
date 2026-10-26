"""Saved music covers follow a day/night switch while they are on screen.

The profile music row (LuxuryMusicButton) baked its colours when the cover
was prepared: the row background, the "no cover" placeholder and the
title/performer text overrides. After switching to night mode with a profile
open, the row stayed light-on-dark with dark #222222 text on the night
background (1.06:1 contrast, unreadable) until the profile was reopened.
The expanded player kept its day placeholder after a switch back to day.

Measured in the round 92 harness: night title contrast 1.06 -> 14.52,
placeholder #474747 -> #828282 in night, #828282 -> #474747 in day.

The row placeholder was also rasterised at its logical size and stretched on
a high-DPI screen (38 px drawn into 76 device pixels at 200%). Round 93
harness, mean difference on the SVG edges against a physical-size render:
5.64 -> 0.00 at 200%, 0.00 at 300%; the expanded player is 0.00 throughout.

Bounded source check; not a native acceptance.
"""
from pathlib import Path
import tempfile
from library_regressions import function, source


MUSIC = 'Telegram/SourceFiles/luxury/ui/components/saved_music.cpp'
MUSIC_H = 'Telegram/SourceFiles/luxury/ui/components/saved_music.h'
PLAYER = 'Telegram/SourceFiles/luxury/ui/boxes/expanded_player_box.cpp'


def check(root, read=source):
    music = read(MUSIC)
    header = read(MUSIC_H)
    constructor = function(music, 'LuxuryMusicButton::LuxuryMusicButton(')
    if 'style::PaletteChanged(' not in constructor:
        raise AssertionError('RED: the saved music row ignores palette changes: '
            'after a night switch its text stays #222222 on the night row (1.06:1)')
    assert 'refreshTextColors();' in constructor and 'update();' in constructor, (
        'a palette change must recolour the labels and repaint the row')

    refresh = function(music, 'void LuxuryMusicButton::refreshTextColors()')
    assert 'st::windowBoldFg->c' in refresh and 'st::groupCallMembersFg->c' in refresh, (
        'label colours must be read from the palette when they are refreshed')

    make = function(music, 'void LuxuryMusicButton::makeCover()')
    assert 'strong->refreshTextColors();' in make, (
        'the prepared cover must take its label colours from the shared refresh')
    for baked in ('MakeNoCoverImage(', 'GetNoCoverBgColor(', 'st::windowBoldFg', 'st::groupCallMembersFg'):
        assert baked not in make, (
            f'makeCover must not bake {baked} into the cover: it goes stale on a theme switch')

    assert 'std::optional<QColor> bg;' in header and 'bool placeholder = true;' in header, (
        'only an extracted cover colour may be stored; the placeholder is drawn at paint time')
    paint = function(music, 'void LuxuryMusicButton::paintEvent(')
    assert 'cover.bg.value_or(GetNoCoverBgColor(_overrideBg))' in paint, (
        'the row background without an extracted colour must come from the palette')
    assert 'MakeNoCoverImage(QSize(size, size))' in paint, (
        'the no-cover placeholder must be drawn for the current palette')

    no_cover = function(music, 'QPixmap MakeNoCoverImage(')
    assert 'cache.fill == fill' in no_cover and 'Background()->id()' not in no_cover, (
        'the placeholder cache must be keyed by its fill colour: the background id '
        'does not change when the default theme switches to night')
    if 'size * style::DevicePixelRatio()' not in no_cover:
        raise AssertionError('RED: the no-cover placeholder is rasterised at its logical '
            'size and stretched on a high-DPI screen')
    for physical in ('QImage(physical, QImage::Format_ARGB32)',
            'QRect(QPoint(), physical)', 'cache.size == physical', 'cache.size = physical;'):
        assert physical in no_cover, (
            f'the placeholder must be drawn and cached at device pixels: missing {physical}')

    cover = function(read(PLAYER), 'CoverWidget(QWidget *parent, int radius)')
    assert 'style::PaletteChanged(' in cover and '_placeholder = QImage();' in cover, (
        'the expanded player must drop its cached placeholder on a palette change')
    print('Saved music palette PASS: row text, background and placeholder plus the '
        'expanded player placeholder follow a day/night switch; the row placeholder '
        'is drawn at device pixels')


if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='luxury-saved-music-palette-') as directory:
        check(Path(directory))
