// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "luxury/ui/components/menu_radius_preview.h"

#include "luxury/luxury_ui_settings.h"
#include "ui/effects/ripple_animation.h"
#include "ui/painter.h"
#include "styles/style_luxury_styles.h"
#include "styles/style_settings.h"
#include "styles/style_widgets.h"

#include <algorithm>

namespace {

constexpr auto kRows = 3;

} // namespace

MenuRadiusPreview::MenuRadiusPreview(QWidget *parent)
: RpWidget(parent)
, _shadow(st::defaultPopupMenu.shadow) {
	const auto &padding = st::menuRadiusPreviewPadding;
	setFixedHeight(padding.top()
		+ st::defaultPopupMenu.menu.itemPadding.top()
		+ st::defaultPopupMenu.menu.itemPadding.bottom()
		+ kRows * (st::defaultMenu.itemPadding.top()
			+ st::defaultPopupMenu.menu.itemStyle.font->height
			+ st::defaultMenu.itemPadding.bottom())
		+ padding.bottom());
	setMouseTracking(true);
}

QRect MenuRadiusPreview::plate() const {
	return rect().marginsRemoved(st::menuRadiusPreviewPadding);
}

int MenuRadiusPreview::rowHeight() const {
	return plate().height() / kRows;
}

int MenuRadiusPreview::rowAt(QPoint position) const {
	const auto inner = plate();
	if (!inner.contains(position)) {
		return -1;
	}
	return std::clamp(
		(position.y() - inner.y()) / rowHeight(),
		0,
		kRows - 1);
}

void MenuRadiusPreview::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	auto hq = PainterHighQualityEnabler(p);

	const auto &menu = st::defaultPopupMenu.menu;
	const auto radius = LuxuryUiSettings::effectiveMenuRadius(
		st::defaultPopupMenu.radius);
	const auto inner = plate();
	const auto rowHeight = this->rowHeight();
	const auto barHeight = menu.itemStyle.font->height / 3;

	// A miniature popup menu: one rounded plate, rows with a highlight, icon
	// dots and text bars. A menu has the colour of the settings page, so the
	// plate sits on a divider-coloured stage, with the shadow of a real menu,
	// to keep the corners that follow the live radius value visible in every
	// theme.
	p.setPen(Qt::NoPen);
	p.setBrush(st::boxDividerBg);
	p.drawRoundedRect(rect(), st::roundRadiusLarge, st::roundRadiusLarge);
	_shadow.paint(p, inner, radius);
	p.setBrush(menu.itemBg);
	p.drawRoundedRect(inner, radius, radius);

	// Rows must stay inside the rounded plate: the painter clips to the
	// plate path so square row highlights can never poke past the corners.
	auto platePath = QPainterPath();
	platePath.addRoundedRect(QRectF(inner), radius, radius);
	p.save();
	p.setClipPath(platePath);
	for (auto row = 0; row != kRows; ++row) {
		const auto top = inner.y() + row * rowHeight;
		if (row == _hoverRow) {
			p.fillRect(
				QRect(inner.x(), top, inner.width(), rowHeight),
				menu.itemBgOver);
		}
		if (_ripple && row == _rippleRow) {
			_ripple->paint(p, inner.x(), top, width());
			if (_ripple->empty()) {
				_ripple.reset();
				_rippleRow = -1;
			}
		}

		const auto dot = rowHeight / 6.;
		const auto dotX = inner.x() + st::defaultMenu.itemPadding.left();
		const auto dotY = top + (rowHeight - dot) / 2.;
		p.setBrush(st::menuIconFg);
		p.drawEllipse(QRectF(dotX, dotY, dot, dot));

		const auto barX = dotX + dot + dot;
		const auto barY = top + (rowHeight - barHeight) / 2.;
		const auto barWidth = (row == kRows - 1)
			? inner.width() * 0.28
			: inner.width() * (0.52 - 0.08 * row);
		p.drawRoundedRect(
			QRectF(barX, barY, barWidth, barHeight),
			barHeight / 2.,
			barHeight / 2.);
	}
	p.restore();
}

void MenuRadiusPreview::mouseMoveEvent(QMouseEvent *e) {
	const auto row = rowAt(e->pos());
	if (_hoverRow != row) {
		_hoverRow = row;
		update();
	}
}

void MenuRadiusPreview::mousePressEvent(QMouseEvent *e) {
	const auto row = rowAt(e->pos());
	if (e->button() != Qt::LeftButton || row < 0) {
		return;
	}
	const auto inner = plate();
	if (!_ripple || _rippleRow != row) {
		_ripple = std::make_unique<Ui::RippleAnimation>(
			st::defaultPopupMenu.menu.ripple,
			Ui::RippleAnimation::RectMask(
				QSize(inner.width(), rowHeight())),
			[=] { update(); });
		_rippleRow = row;
	}
	_ripple->add(e->pos() - QPoint(inner.x(), inner.y() + row * rowHeight()));
}

void MenuRadiusPreview::mouseReleaseEvent(QMouseEvent *e) {
	if (_ripple) {
		_ripple->lastStop();
	}
}

void MenuRadiusPreview::leaveEventHook(QEvent *e) {
	_hoverRow = -1;
	update();
	RpWidget::leaveEventHook(e);
}
