// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "luxury/ui/components/menu_radius_preview.h"

#include "luxury/luxury_settings.h"
#include "ui/effects/ripple_animation.h"
#include "ui/painter.h"
#include "styles/style_widgets.h"
#include "styles/style_settings.h"

namespace {

constexpr auto kRows = 3;

} // namespace

MenuRadiusPreview::MenuRadiusPreview(QWidget *parent)
: RpWidget(parent) {
	setFixedHeight(st::defaultPopupMenu.menu.itemPadding.top()
		+ st::defaultPopupMenu.menu.itemPadding.bottom()
		+ kRows * (st::defaultMenu.itemPadding.top()
			+ st::defaultPopupMenu.menu.itemStyle.font->height
			+ st::defaultMenu.itemPadding.bottom()));
	setMouseTracking(true);
}

int MenuRadiusPreview::hoveredRow() const {
	return _hoverRow;
}

void MenuRadiusPreview::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	auto hq = PainterHighQualityEnabler(p);

	const auto radius = LuxuryUiSettings::effectiveMenuRadius(
		st::defaultPopupMenu.radius);
	const auto rowHeight = height() / kRows;
	const auto textFont = st::defaultPopupMenu.menu.itemStyle.font;
	const auto barHeight = textFont->height / 3;

	// A miniature popup menu: one rounded plate, rows with a highlight,
	// icon dots and text bars. The corners follow the live radius value,
	// so dragging the slider reshapes this preview in real time.
	p.setBrush(st::menuBg);
	p.setPen(Qt::NoPen);
	p.drawRoundedRect(rect(), radius, radius);

	if (_ripple) {
		_ripple->paint(p, 0, 0, width());
		if (_ripple->empty()) {
			_ripple.reset();
		}
	}

	for (auto row = 0; row != kRows; ++row) {
		const auto top = row * rowHeight;
		if (row == _hoverRow) {
			const auto inner = rect().marginsRemoved(
				QMargins(0, top, 0, height() - top - rowHeight));
			// Keep the row highlight inside the rounded plate: the
			// outermost rows take the menu radius on their outer side.
			p.setBrush(st::menuBgOver);
			p.drawRoundedRect(
				inner,
				(row == 0) ? radius / 2. : 0.,
				(row == 0) ? radius / 2. : 0.);
			p.setBrush(st::menuBg);
		}

		const auto dot = rowHeight / 6.;
		const auto dotX = st::defaultMenu.itemPadding.left();
		const auto dotY = top + (rowHeight - dot) / 2.;
		p.setBrush(st::menuIconFg);
		p.drawEllipse(dotX, dotY, dot, dot);

		const auto barX = dotX + dot + dot;
		const auto barY = top + (rowHeight - barHeight) / 2.;
		const auto barWidth = (row == kRows - 1)
			? width() * 0.28
			: width() * (0.52 - 0.08 * row);
		p.setBrush(st::menuIconFg);
		p.drawRoundedRect(
			QRectF(barX, barY, barWidth, barHeight),
			barHeight / 2.,
			barHeight / 2.);
		p.setBrush(st::menuBg);
	}
}

void MenuRadiusPreview::mouseMoveEvent(QMouseEvent *e) {
	const auto row = std::clamp(
		e->pos().y() / (height() / kRows),
		0,
		kRows - 1);
	if (_hoverRow != row) {
		_hoverRow = row;
		update();
	}
}

void MenuRadiusPreview::mousePressEvent(QMouseEvent *e) {
	if (e->button() == Qt::LeftButton) {
		if (!_ripple) {
			auto mask = Ui::RippleAnimation::RoundRectMask(
				size(),
				LuxuryUiSettings::effectiveMenuRadius(
					st::defaultPopupMenu.radius));
			_ripple = std::make_unique<Ui::RippleAnimation>(
				st::defaultRippleAnimation,
				std::move(mask),
				[=] { update(); });
		}
		_ripple->add(e->pos());
	}
}

void MenuRadiusPreview::mouseReleaseEvent(QMouseEvent *e) {
	if (_ripple) {
		_ripple->lastStop();
	}
}
