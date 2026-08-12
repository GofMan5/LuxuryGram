// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "luxury/ui/components/avatar_corners_preview.h"

#include "data/data_peer.h"
#include "styles/style_luxury_icons.h"
#include "styles/style_dialogs.h"
#include "styles/style_settings.h"
#include "ui/empty_userpic.h"
#include "ui/painter.h"
#include "ui/effects/ripple_animation.h"

#include <QDesktopServices>

AvatarCornersPreview::AvatarCornersPreview(
	QWidget *parent,
	not_null<Window::SessionController*>)
: RpWidget(parent)
, _emptyUserpic(
	Ui::EmptyUserpic::UserpicColor(
		Data::DecideColorIndex(
			peerFromChannel(ChannelId(2331068091)))),
	u"LuxuryGram"_q) {
	const auto &row = st::defaultDialogRow;
	setFixedHeight(row.height);
	setCursor(Qt::PointingHandCursor);
}

void AvatarCornersPreview::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);

	const auto &row = st::defaultDialogRow;
	const auto photoSize = row.photoSize;
	const auto xShift = st::settingsButtonNoIcon.padding.left()
		- row.padding.left();
	const auto userpicX = row.padding.left() + xShift;
	const auto userpicY = (height() - photoSize) / 2;

	p.fillRect(rect(), st::windowBg);

	if (_ripple) {
		_ripple->paint(p, 0, 0, width());
		if (_ripple->empty()) {
			_ripple.reset();
		}
	}

	_emptyUserpic.paintCircle(p, userpicX, userpicY, width(), photoSize);

	const auto nameText = u"LuxuryGram"_q;
	p.setPen(st::dialogsNameFg);
	p.setFont(st::semiboldFont);
	p.drawText(row.nameLeft + xShift, row.nameTop + st::semiboldFont->ascent, nameText);

	p.setPen(st::dialogsTextFg);
	p.setFont(st::dialogsTextFont);
	p.drawText(row.textLeft + xShift, row.textTop + st::dialogsTextFont->ascent, u"Better late than never"_q);
}

void AvatarCornersPreview::mousePressEvent(QMouseEvent *e) {
	if (e->button() == Qt::LeftButton) {
		if (!_ripple) {
			auto mask = Ui::RippleAnimation::RectMask(size());
			_ripple = std::make_unique<Ui::RippleAnimation>(
				st::defaultRippleAnimation,
				std::move(mask),
				[=] { update(); });
		}
		_ripple->add(e->pos());
	}
}

void AvatarCornersPreview::mouseReleaseEvent(QMouseEvent *e) {
	if (_ripple) {
		_ripple->lastStop();
	}
	if (e->button() == Qt::LeftButton && rect().contains(e->pos())) {
		QDesktopServices::openUrl(
			u"https://github.com/GofMan5/LuxuryGram/releases"_q);
	}
}
