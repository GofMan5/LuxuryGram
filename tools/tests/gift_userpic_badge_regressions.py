"""Place the pin and TON badges of a gift card beside its sender userpic.

The userpic moves inwards as the avatar corner setting gets squarer; the
badges have to follow it at every corner value and interface scale.
"""
from pathlib import Path
import re
import tempfile
from library_regressions import cpp, function, source

GIFTS = 'Telegram/SourceFiles/info/peer_gifts/info_peer_gifts_common.cpp'


def between(text, first, last):
    start = text.index(first) + len(first)
    return text[start:text.index(last, start)]


def pieces(text):
    paint = function(text, 'void GiftButton::paint(QPainter &p, float64 craftProgress)')
    helper = ''
    if 'int UserpicCornerInset()' in text:
        helper = function(text, '[[nodiscard]] int UserpicCornerInset()')
    inset = re.search(r'const auto userpicInset = [^;]*;', paint)
    avatar = between(
        paint,
        'const auto image = _userpic->image(st::giftBoxUserpicSize);',
        'p.drawImage(extend.left() + skip, extend.top() + skip, image);')
    shift = re.search(r'const auto statusShift = [^;]*;', paint).group(0)
    pin = between(paint, 'const auto &icon = st::giftBoxPinIcon;', 'p.drawEllipse(rect);')
    pin_top = re.search(
        r'extend\.left\(\) \+ skip \+ statusShift,\s*(.*?)\),\s*QSize',
        pin,
        re.S).group(1)
    ton = between(paint, '_tonIcon = st::tonIconEmojiLarge', 'percentSkip +=')
    ton_tops = re.findall(r'(extend\.top\(\) \+ skip[^,)]*)', ton)
    return helper, inset.group(0) if inset else '', avatar, shift, pin_top, ton_tops


def check(root):
    helper, inset, avatar, shift, pin_top, ton_tops = pieces(source(GIFTS))
    assert ton_tops == [pin_top, pin_top + ' + add'], \
        f'the TON badge does not follow the userpic like the pin: {ton_tops}'
    cpp(root, 'gift_userpic_badge', r'''
#include <cassert>
#include <cmath>
#include <iostream>

int g_scale = 100;
int g_corners = 23;

namespace style {
double ConvertScale(double value) { return value * g_scale / 100.; }
} // namespace style

namespace LuxuryUiSettings {
constexpr int kMaxAvatarCorners = 23;
int getAvatarCorners() { return g_corners; }
} // namespace LuxuryUiSettings

namespace st {
int giftBoxUserpicSize = 24;
int giftBoxUserpicSkip = 2;
} // namespace st

struct Extend {
	int left() const { return 0; }
	int top() const { return 0; }
};

''' + helper + r'''

void Check(int scale, int corners) {
	g_scale = scale;
	g_corners = corners;
	st::giftBoxUserpicSize = 24 * scale / 100;
	st::giftBoxUserpicSkip = 2 * scale / 100;
	const auto extend = Extend();
	const auto _userpic = true;
	''' + inset + r'''
	auto avatarLeft = 0;
	auto avatarTop = 0;
	{
		''' + avatar + r'''
		avatarLeft = extend.left() + skip;
		avatarTop = extend.top() + skip;
	}
	''' + shift + r'''
	const auto skip = st::giftBoxUserpicSkip;
	const auto pinLeft = extend.left() + skip + statusShift;
	const auto pinTop = ''' + pin_top + r''';
	const auto avatarRight = avatarLeft + st::giftBoxUserpicSize;
	if (pinLeft < avatarRight + st::giftBoxUserpicSkip || pinTop != avatarTop) {
		std::cerr
			<< "scale " << scale << " corners " << corners
			<< ": userpic " << avatarLeft << "," << avatarTop
			<< " right " << avatarRight
			<< ", pin " << pinLeft << "," << pinTop << "\n";
		assert(!"the pin badge overlaps or misaligns with the userpic");
	}
}

int main() {
	for (const auto scale : { 100, 150, 200 }) {
		for (auto corners = 0; corners <= LuxuryUiSettings::kMaxAvatarCorners; ++corners) {
			Check(scale, corners);
		}
	}
	std::cout << "LuxuryGram gift userpic badge PASS: pin and TON badges sit beside the sender userpic at every corner value and scale\n";
}
''')


if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='gift-userpic-badge-') as directory:
        check(Path(directory))
