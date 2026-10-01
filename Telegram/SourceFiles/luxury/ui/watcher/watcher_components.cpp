// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "luxury/ui/watcher/watcher_components.h"

#include "ui/effects/ripple_animation.h"
#include "ui/painter.h"

#include "styles/style_basic.h"
#include "styles/style_luxury_styles.h"
#include "styles/style_widgets.h"

#include <algorithm>
#include <cmath>

#include <QtGui/QPolygonF>

namespace LuxuryUi {
namespace {

constexpr auto kPi = 3.14159265358979323846;

// Timing (not geometry, so plain constants are fine here).
constexpr auto kPillDuration = crl::time(150);
constexpr auto kSpinDuration = crl::time(600);

constexpr auto kSegmentPillAlpha = 0.14;
constexpr auto kTabPillAlpha = 0.16;
constexpr auto kTabHoverAlpha = 0.04;
constexpr auto kDisabledLabelAlpha = 0.4;
constexpr auto kTabCountAlpha = 0.55;

} // namespace

void PaintRefreshGlyph(QPainter &p, float64 base, const QColor &color) {
	// A ~300 degree arc with an arrowhead at its end: the standard
	// circular-arrow "refresh" mark, drawn instead of an icon asset.
	const auto radius = base;
	const auto endDegrees = 90. + 300.;
	const auto radians = endDegrees * kPi / 180.;
	const auto end = QPointF(
		radius * std::cos(radians),
		-radius * std::sin(radians));
	// Unit tangent along the counterclockwise sweep at the arc end.
	const auto tangent = QPointF(-std::sin(radians), -std::cos(radians));
	const auto normal = QPointF(-tangent.y(), tangent.x());
	const auto head = 0.42 * base;
	const auto half = 0.28 * base;
	p.drawArc(
		QRectF(-radius, -radius, 2 * radius, 2 * radius),
		90 * 16,
		300 * 16);
	const auto arrow = QPolygonF({
		end + tangent * head,
		end + normal * half,
		end - normal * half,
	});
	p.setPen(Qt::NoPen);
	p.setBrush(color);
	p.drawConvexPolygon(arrow);
}

void PaintGearGlyph(
		QPainter &p,
		float64 base,
		const QColor &color,
		float64 penWidth) {
	// Ring, eight teeth and a center hole: a painted gear.
	const auto ring = 0.78 * base;
	const auto teethInner = 0.88 * base;
	const auto teethOuter = 1.18 * base;
	const auto hole = 0.30 * base;
	p.drawEllipse(QPointF(0, 0), ring, ring);
	for (auto i = 0; i != 8; ++i) {
		const auto radians = (i * 45.) * kPi / 180.;
		const auto c = std::cos(radians);
		const auto s = std::sin(radians);
		p.drawLine(
			QPointF(teethInner * c, teethInner * s),
			QPointF(teethOuter * c, teethOuter * s));
	}
	p.setPen(QPen(
		color,
		std::max(1., penWidth / 2.),
		Qt::SolidLine,
		Qt::RoundCap,
		Qt::RoundJoin));
	p.drawEllipse(QPointF(0, 0), hole, hole);
}

void PaintClockGlyph(
		QPainter &p,
		const QPointF &topLeft,
		float64 size,
		const QColor &color) {
	const auto pen = QPen(
		color,
		std::max(1., size / 7.),
		Qt::SolidLine,
		Qt::RoundCap,
		Qt::RoundJoin);
	p.setPen(pen);
	p.setBrush(Qt::NoBrush);
	const auto rect = QRectF(topLeft, QSizeF(size, size));
	p.drawEllipse(rect);
	const auto center = rect.center();
	p.drawLine(center, QPointF(center.x(), rect.top() + size * 0.22));
	p.drawLine(center, QPointF(center.x() + size * 0.29, center.y()));
}

ToolButton::ToolButton(QWidget *parent, ToolGlyph glyph)
: RpWidget(parent)
, _glyph(glyph) {
	setFixedSize(st::luxuryWatcherIconSize, st::luxuryWatcherIconSize);
	setCursor(style::cur_pointer);
}

void ToolButton::spin() {
	_spin.start(
		[=](float64) { update(); },
		0.,
		1.,
		kSpinDuration,
		anim::easeOutCubic);
}

void ToolButton::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	auto hq = PainterHighQualityEnabler(p);
	if (_ripple) {
		_ripple->paint(p, 0, 0, width());
		if (_ripple->empty()) {
			_ripple.reset();
		}
	}
	paintIcon(p, (_hovered ? st::windowFg : st::windowSubTextFg)->c);
}

void ToolButton::paintIcon(QPainter &p, const QColor &color) {
	const auto base = st::luxuryWatcherIconSize / 4.;
	const auto penWidth = std::max(1., base / 4.);
	p.save();
	p.translate(QPointF(width() / 2., height() / 2.));
	p.rotate(_spin.value(1.) * 360.);
	p.setPen(QPen(
		color,
		penWidth,
		Qt::SolidLine,
		Qt::RoundCap,
		Qt::RoundJoin));
	p.setBrush(Qt::NoBrush);
	if (_glyph == ToolGlyph::Refresh) {
		PaintRefreshGlyph(p, base, color);
	} else {
		PaintGearGlyph(p, base, color, penWidth);
	}
	p.restore();
}

void ToolButton::mousePressEvent(QMouseEvent *e) {
	if (e->button() == Qt::LeftButton) {
		_pressed = true;
		if (!_ripple) {
			_ripple = std::make_unique<Ui::RippleAnimation>(
				st::defaultRippleAnimation,
				Ui::RippleAnimation::RoundRectMask(
					size(),
					st::luxuryWatcherIconSize / 2),
				[=] { update(); });
		}
		_ripple->add(e->pos());
	}
}

void ToolButton::mouseReleaseEvent(QMouseEvent *e) {
	if (base::take(_pressed)) {
		if (_ripple) {
			_ripple->lastStop();
		}
		if (rect().contains(e->pos())) {
			_clicks.fire({});
		}
	}
}

void ToolButton::enterEventHook(QEnterEvent *e) {
	_hovered = true;
	update();
	RpWidget::enterEventHook(e);
}

void ToolButton::leaveEventHook(QEvent *e) {
	_hovered = false;
	update();
	RpWidget::leaveEventHook(e);
}

SegmentControl::SegmentControl(
	QWidget *parent,
	std::array<QString, 3> labels)
: RpWidget(parent)
, _labels(std::move(labels)) {
	resize(contentWidth(), st::luxuryWatcherSortHeight);
	setCursor(style::cur_pointer);
}

int SegmentControl::contentWidth() const {
	auto result = 0;
	for (auto i = 0; i != int(_labels.size()); ++i) {
		result += segmentWidth(i) + st::luxuryWatcherSortSkip;
	}
	return result - st::luxuryWatcherSortSkip;
}

int SegmentControl::segmentWidth(int index) const {
	return st::luxuryWatcherChipFont->width(_labels[index])
		+ st::luxuryWatcherPillPadding.left()
		+ st::luxuryWatcherPillPadding.right();
}

int SegmentControl::segmentLeft(int index) const {
	auto result = 0;
	for (auto i = 0; i != index; ++i) {
		result += segmentWidth(i) + st::luxuryWatcherSortSkip;
	}
	return result;
}

void SegmentControl::setIndex(int index) {
	if (index == _selected) {
		return;
	}
	_selected = index;
	// A programmatic move is a correction, not a gesture: snap the pill.
	_pillLeft.stop();
	_pillWidth.stop();
	update();
}

void SegmentControl::setLastEnabled(bool enabled) {
	if (_lastEnabled != enabled) {
		_lastEnabled = enabled;
		update();
	}
}

void SegmentControl::select(int index) {
	if (index == _selected) {
		return;
	}
	const auto fromLeft = segmentLeft(_selected);
	const auto fromWidth = segmentWidth(_selected);
	_selected = index;
	const auto toLeft = segmentLeft(_selected);
	const auto toWidth = segmentWidth(_selected);
	const auto updater = [=] { update(); };
	// easeOutCubic: the pill glides out fast and settles smoothly, the
	// same curve the section sliders use.
	_pillLeft.start(
		updater,
		fromLeft,
		toLeft,
		kPillDuration,
		anim::easeOutCubic);
	_pillWidth.start(
		updater,
		fromWidth,
		toWidth,
		kPillDuration,
		anim::easeOutCubic);
	_changes.fire_copy(_selected);
}

void SegmentControl::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	auto hq = PainterHighQualityEnabler(p);

	const auto pillLeft = _pillLeft.value(segmentLeft(_selected));
	const auto pillWidth = _pillWidth.value(segmentWidth(_selected));
	auto pillColor = QColor(st::windowActiveTextFg->c);
	pillColor.setAlphaF(kSegmentPillAlpha);
	p.setPen(Qt::NoPen);
	p.setBrush(pillColor);
	p.drawRoundedRect(
		QRectF(pillLeft, 0, pillWidth, height()),
		height() / 2.,
		height() / 2.);

	const auto &font = st::luxuryWatcherChipFont;
	p.setFont(font);
	const auto textTop = (height() - font->height) / 2;
	for (auto i = 0; i != int(_labels.size()); ++i) {
		const auto disabled = (i == 2 && !_lastEnabled);
		if (i == _selected) {
			p.setPen(QPen(st::windowActiveTextFg->c));
		} else {
			auto inactive = QColor(st::windowSubTextFg->c);
			if (disabled) {
				inactive.setAlphaF(kDisabledLabelAlpha);
			}
			p.setPen(QPen(inactive));
		}
		const auto left = segmentLeft(i);
		const auto width = segmentWidth(i);
		const auto textWidth = font->width(_labels[i]);
		p.drawText(
			QPointF(left + (width - textWidth) / 2, textTop + font->ascent),
			_labels[i]);
	}
}

void SegmentControl::mousePressEvent(QMouseEvent *e) {
	if (e->button() != Qt::LeftButton) {
		return;
	}
	const auto x = e->pos().x();
	for (auto i = 0; i != int(_labels.size()); ++i) {
		const auto left = segmentLeft(i);
		// The skip between segments belongs to the segment before it, so
		// the trailing gap after the last one is not a dead zone that
		// still selects it.
		const auto reach = left
			+ segmentWidth(i)
			+ (i == int(_labels.size()) - 1
				? 0
				: st::luxuryWatcherSortSkip);
		if (x < reach) {
			// The last segment can be meaningless for the current tab and
			// paints disabled there; the press bounces off instead of
			// selecting it.
			if (!(i == 2 && !_lastEnabled)) {
				select(i);
			}
			return;
		}
	}
}

void SegmentControl::enterEventHook(QEnterEvent *e) {
	update();
	RpWidget::enterEventHook(e);
}

void SegmentControl::leaveEventHook(QEvent *e) {
	update();
	RpWidget::leaveEventHook(e);
}

TabsBar::TabsBar(QWidget *parent, std::array<QString, 3> labels)
: RpWidget(parent)
, _labels(std::move(labels)) {
	setFixedHeight(st::luxuryWatcherTabHeight);
	setCursor(style::cur_pointer);
	setMouseTracking(true);
}

void TabsBar::setIndex(int index) {
	if (index == _selected) {
		return;
	}
	_selected = index;
	// A programmatic move is a correction, not a gesture: snap the pill.
	_pillLeft.stop();
	update();
}

void TabsBar::setCounts(std::array<int, 3> counts) {
	for (auto i = 0; i != int(counts.size()); ++i) {
		_counts[i] = counts[i] > 0 ? QString::number(counts[i]) : QString();
	}
	update();
}

int TabsBar::segmentWidth(int index) const {
	return index < 2
		? width() / 3
		: width() - 2 * (width() / 3);
}

int TabsBar::segmentLeft(int index) const {
	return index * (width() / 3);
}

void TabsBar::select(int index) {
	if (index == _selected) {
		return;
	}
	const auto fromLeft = segmentLeft(_selected);
	_selected = index;
	const auto toLeft = segmentLeft(_selected);
	_pillLeft.start(
		[=] { update(); },
		fromLeft,
		toLeft,
		kPillDuration,
		anim::easeOutCubic);
	_changes.fire_copy(_selected);
}

void TabsBar::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	auto hq = PainterHighQualityEnabler(p);

	const auto pillLeft = _pillLeft.value(segmentLeft(_selected));
	auto pillColor = QColor(st::windowActiveTextFg->c);
	pillColor.setAlphaF(kTabPillAlpha);
	p.setPen(Qt::NoPen);
	p.setBrush(pillColor);
	const auto pillRect = QRectF(
		pillLeft + st::luxuryWatcherTabSkip,
		st::luxuryWatcherTabSkip,
		segmentWidth(_selected) - 2 * st::luxuryWatcherTabSkip,
		height() - 2 * st::luxuryWatcherTabSkip);
	p.drawRoundedRect(pillRect, pillRect.height() / 2., pillRect.height() / 2.);

	const auto &font = st::luxuryWatcherTabFont;
	const auto &countFont = st::luxuryWatcherTabCountFont;
	const auto textTop = (height() - font->height) / 2;
	for (auto i = 0; i != int(_labels.size()); ++i) {
		if (i == _hovered && i != _selected) {
			auto hoverColor = QColor(st::windowFg->c);
			hoverColor.setAlphaF(kTabHoverAlpha);
			p.setPen(Qt::NoPen);
			p.setBrush(hoverColor);
			p.drawRoundedRect(
				QRectF(
					segmentLeft(i) + st::luxuryWatcherTabSkip,
					st::luxuryWatcherTabSkip,
					segmentWidth(i) - 2 * st::luxuryWatcherTabSkip,
					height() - 2 * st::luxuryWatcherTabSkip),
				(height() - 2 * st::luxuryWatcherTabSkip) / 2.,
				(height() - 2 * st::luxuryWatcherTabSkip) / 2.);
		}
		const auto labelWidth = font->width(_labels[i]);
		const auto countWidth = _counts[i].isEmpty()
			? 0
			: countFont->spacew
				+ countFont->width(u"·"_q)
				+ countFont->spacew
				+ countFont->width(_counts[i]);
		const auto total = labelWidth + countWidth;
		const auto left = segmentLeft(i) + (segmentWidth(i) - total) / 2;
		p.setFont(font);
		p.setPen(i == _selected ? st::windowFg : st::windowSubTextFg);
		p.drawText(QPointF(left, textTop + font->ascent), _labels[i]);
		if (!_counts[i].isEmpty()) {
			auto countColor = QColor(
				(i == _selected
					? st::windowActiveTextFg
					: st::windowSubTextFg)->c);
			countColor.setAlphaF(kTabCountAlpha);
			p.setFont(countFont);
			p.setPen(countColor);
			const auto countTop = (height() - countFont->height) / 2;
			p.drawText(
				QPointF(
					left + labelWidth + countFont->spacew,
					countTop + countFont->ascent),
				u"·"_q);
			p.drawText(
				QPointF(
					left
						+ labelWidth
						+ 2 * countFont->spacew
						+ countFont->width(u"·"_q),
					countTop + countFont->ascent),
				_counts[i]);
		}
	}
}

void TabsBar::mousePressEvent(QMouseEvent *e) {
	if (e->button() != Qt::LeftButton) {
		return;
	}
	const auto x = e->pos().x();
	for (auto i = 0; i != int(_labels.size()); ++i) {
		if (x < segmentLeft(i) + segmentWidth(i)) {
			select(i);
			return;
		}
	}
}

void TabsBar::mouseMoveEvent(QMouseEvent *e) {
	const auto x = e->pos().x();
	auto hovered = -1;
	for (auto i = 0; i != int(_labels.size()); ++i) {
		if (x < segmentLeft(i) + segmentWidth(i)) {
			hovered = i;
			break;
		}
	}
	if (hovered != _hovered) {
		_hovered = hovered;
		update();
	}
}

void TabsBar::leaveEventHook(QEvent *e) {
	_hovered = -1;
	update();
	RpWidget::leaveEventHook(e);
}

EmptyBlock::EmptyBlock(QWidget *parent, const QString &text)
: RpWidget(parent)
, _text(text) {
	// A fixed box: the glyph circle, a skip, and one label line.
	const auto circle = st::luxuryWatcherEmptyGlyphSize
		+ 2 * st::luxuryWatcherCardSkip;
	setFixedHeight(
		circle
		+ st::luxuryWatcherCardSkip
		+ st::luxuryWatcherEmptyFont->height);
}

void EmptyBlock::setText(const QString &text) {
	if (_text != text) {
		_text = text;
		update();
	}
}

void EmptyBlock::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	auto hq = PainterHighQualityEnabler(p);

	// A dim clock in a soft circle above the label: a recognizable
	// "nothing recorded yet" mark without an icon asset.
	const auto glyph = st::luxuryWatcherEmptyGlyphSize;
	const auto circle = glyph + 2 * st::luxuryWatcherCardSkip;
	auto haloColor = QColor(st::windowSubTextFg->c);
	haloColor.setAlphaF(0.09);
	p.setPen(Qt::NoPen);
	p.setBrush(haloColor);
	p.drawEllipse(
		QPointF(width() / 2., circle / 2.),
		circle / 2.,
		circle / 2.);
	auto glyphColor = QColor(st::windowSubTextFg->c);
	glyphColor.setAlphaF(0.5);
	PaintClockGlyph(
		p,
		QPointF(width() / 2. - glyph / 2., circle / 2. - glyph / 2.),
		glyph,
		glyphColor);

	const auto &font = st::luxuryWatcherEmptyFont;
	p.setFont(font);
	auto textColor = QColor(st::windowSubTextFg->c);
	textColor.setAlphaF(0.75);
	p.setPen(textColor);
	p.drawText(
		QRectF(
			0,
			circle + st::luxuryWatcherCardSkip,
			width(),
			font->height),
		_text,
		QTextOption(Qt::AlignHCenter | Qt::AlignTop));
}

} // namespace LuxuryUi
