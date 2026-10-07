// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "luxury/ui/watcher/watcher_components.h"

#include "ui/widgets/buttons.h"
#include "ui/painter.h"
#include "ui/qt_object_factory.h"

#include <algorithm>
#include <cmath>
#include <QtGui/QPolygonF>
#include <QtGui/QtEvents>

#include "styles/style_basic.h"
#include "styles/style_luxury_styles.h"
#include "styles/style_widgets.h"

namespace LuxuryUi {
namespace {

constexpr auto kPi = 3.14159265358979323846;
constexpr auto kSpinDuration = crl::time(450);

} // namespace

class ChoiceButton final : public Ui::LinkButton {
public:
	ChoiceButton(QWidget *parent, QString text);
	void setSelected(bool selected);
	QAccessible::Role accessibilityRole() override {
		return AbstractButton::accessibilityRole();
	}
	Ui::AccessibilityState accessibilityState() const override {
		return {
			.checkable = true,
			.checked = _selected,
			.pressed = isDown(),
			.selectable = true,
			.selected = _selected,
		};
	}

protected:
	void paintEvent(QPaintEvent *e) override;
	void focusInEvent(QFocusEvent *e) override {
		LinkButton::focusInEvent(e);
		update();
	}
	void focusOutEvent(QFocusEvent *e) override {
		LinkButton::focusOutEvent(e);
		update();
	}

private:
	bool _selected = false;

};

ChoiceButton::ChoiceButton(QWidget *parent, QString text)
: LinkButton(parent, text, st::luxuryWatcherControl) {
	setFocusPolicy(Qt::StrongFocus);
	// The paint pass elides to whatever width the segment gets; the
	// tooltip keeps the whole label reachable.
	setToolTip(text);
}

void ChoiceButton::setSelected(bool selected) {
	if (_selected != selected) {
		_selected = selected;
		accessibilityStateChanged({ .checked = true, .selected = true });
		update();
	}
}

void ChoiceButton::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(Qt::NoPen);
	p.setBrush(_selected
		? WithAlpha(AccentColor(), 0.10)
		: (isOver() && isEnabled())
		? st::windowBgOver->c
		: QColor(Qt::transparent));
	const auto inset = st::lineWidth / 2.;
	const auto plate = QRectF(inset, inset, width() - 2 * inset,
		height() - 2 * inset);
	p.drawRoundedRect(plate, st::luxuryWatcherDetailRadius,
		st::luxuryWatcherDetailRadius);
	if (hasFocus()) {
		p.setPen(QPen(AccentColor(), st::lineWidth));
		p.setBrush(Qt::NoBrush);
		p.drawRoundedRect(plate, st::luxuryWatcherDetailRadius,
			st::luxuryWatcherDetailRadius);
	}
	const auto disabled = !isEnabled();
	const auto &font = st::luxuryWatcherChipFont;
	p.setFont(font);
	p.setPen(_selected
		? AccentColor()
		: disabled
		? WithAlpha(st::windowFg->c, 0.5)
		: st::windowFg->c);
	const auto text = font->elided(accessibilityName(),
		std::max(0, width() - 2 * st::luxuryWatcherControlInset));
	p.drawText(rect(), Qt::AlignCenter, text);
}

QColor AccentColor() {
	return st::windowActiveTextFg->c;
}

QColor WithAlpha(const QColor &color, float64 alpha) {
	auto result = color;
	result.setAlphaF(std::clamp(alpha, 0., 1.));
	return result;
}

void PaintRefreshGlyph(QPainter &p, float64 base, const QColor &color) {
	const auto radians = 390. * kPi / 180.;
	const auto end = QPointF(base * std::cos(radians), -base * std::sin(radians));
	const auto tangent = QPointF(-std::sin(radians), -std::cos(radians));
	const auto normal = QPointF(-tangent.y(), tangent.x());
	p.drawArc(QRectF(-base, -base, 2 * base, 2 * base), 90 * 16, 300 * 16);
	p.setPen(Qt::NoPen);
	p.setBrush(color);
	p.drawConvexPolygon(QPolygonF({
		end + tangent * (0.42 * base),
		end + normal * (0.28 * base),
		end - normal * (0.28 * base),
	}));
}

void PaintGearGlyph(
		QPainter &p,
		float64 base,
		const QColor &color,
		float64 penWidth) {
	p.drawEllipse(QPointF(), 0.78 * base, 0.78 * base);
	for (auto i = 0; i != 8; ++i) {
		const auto radians = i * kPi / 4.;
		const auto point = QPointF(std::cos(radians), std::sin(radians));
		p.drawLine(point * (0.88 * base), point * (1.18 * base));
	}
	p.setPen(QPen(color, std::max(float64(st::lineWidth), penWidth / 2.),
		Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
	p.drawEllipse(QPointF(), 0.30 * base, 0.30 * base);
}

void PaintClockGlyph(QPainter &p, const QPointF &topLeft, float64 size,
		const QColor &color) {
	p.setPen(QPen(color, std::max(float64(st::lineWidth), size / 7.),
		Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
	p.setBrush(Qt::NoBrush);
	const auto rect = QRectF(topLeft, QSizeF(size, size));
	p.drawEllipse(rect);
	const auto center = rect.center();
	p.drawLine(center, QPointF(center.x(), rect.top() + size * 0.22));
	p.drawLine(center, QPointF(center.x() + size * 0.29, center.y()));
}

ToolButton::ToolButton(QWidget *parent, ToolGlyph glyph)
: AbstractButton(parent)
, _glyph(glyph) {
	setFixedSize(st::luxuryWatcherIconSize, st::luxuryWatcherIconSize);
	setFocusPolicy(Qt::StrongFocus);
	setClickedCallback([=] { _clicks.fire({}); });
}

void ToolButton::spin() {
	_spin.start([=](float64) { update(); }, 0., 1., kSpinDuration,
		anim::easeOutCubic);
}

void ToolButton::onStateChanged(State was, StateChangeSource source) {
	update();
}

void ToolButton::focusInEvent(QFocusEvent *e) {
	AbstractButton::focusInEvent(e);
	update();
}

void ToolButton::focusOutEvent(QFocusEvent *e) {
	AbstractButton::focusOutEvent(e);
	update();
}

void ToolButton::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	auto hq = PainterHighQualityEnabler(p);
	const auto inset = st::lineWidth / 2.;
	const auto plate = QRectF(inset, inset, width() - 2 * inset,
		height() - 2 * inset);
	if (isOver() || isDown() || hasFocus()) {
		p.setPen(hasFocus() ? QPen(AccentColor(), st::lineWidth) : QPen(Qt::NoPen));
		p.setBrush(st::windowBgOver);
		p.drawRoundedRect(plate, st::luxuryWatcherDetailRadius,
			st::luxuryWatcherDetailRadius);
	}
	paintIcon(p, isOver() ? AccentColor() : st::windowSubTextFg->c);
}

void ToolButton::paintIcon(QPainter &p, const QColor &color) {
	const auto base = st::luxuryWatcherIconSize / 4.;
	const auto penWidth = std::max(float64(st::lineWidth), base / 4.);
	p.save();
	p.translate(width() / 2., height() / 2.);
	if (_glyph == ToolGlyph::Refresh) {
		p.rotate(_spin.value(1.) * 360.);
	}
	p.setPen(QPen(color, penWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
	p.setBrush(Qt::NoBrush);
	if (_glyph == ToolGlyph::Refresh) {
		PaintRefreshGlyph(p, base, color);
	} else {
		PaintGearGlyph(p, base, color, penWidth);
	}
	p.restore();
}

ActionButton::ActionButton(QWidget *parent, const QString &text)
: LinkButton(parent, text, st::luxuryWatcherAction) {
	setFocusPolicy(Qt::StrongFocus);
}

void ActionButton::focusInEvent(QFocusEvent *e) {
	LinkButton::focusInEvent(e);
	update();
}

void ActionButton::focusOutEvent(QFocusEvent *e) {
	LinkButton::focusOutEvent(e);
	update();
}

void ActionButton::paintEvent(QPaintEvent *e) {
	LinkButton::paintEvent(e);
	if (hasFocus()) {
		auto p = Painter(this);
		p.setPen(QPen(AccentColor(), st::lineWidth));
		p.drawLine(0, height() - st::lineWidth,
			width(), height() - st::lineWidth);
	}
}

SegmentControl::SegmentControl(QWidget *parent, std::array<QString, 3> labels)
: RpWidget(parent) {
	for (auto i = 0; i != 3; ++i) {
		_buttons[i] = Ui::CreateChild<ChoiceButton>(this, labels[i]);
		_buttons[i]->setClickedCallback([=] {
			setIndex(i);
			_changes.fire_copy(i);
		});
	}
	setIndex(0);
}

int SegmentControl::contentWidth() const {
	auto result = 0;
	for (auto i = 0; i != 3; ++i) {
		result += _buttons[i]->naturalWidth() + st::luxuryWatcherSortSkip;
	}
	return result - st::luxuryWatcherSortSkip;
}

void SegmentControl::setIndex(int index) {
	if (index < 0 || index >= 3 || (index == 2 && !_lastEnabled)) {
		return;
	}
	for (auto i = 0; i != 3; ++i) {
		_buttons[i]->setSelected(i == index);
	}
}

void SegmentControl::setLastEnabled(bool enabled) {
	// Disabled in place: the third option stays visible and muted, so
	// the control never changes width and the choice reads as
	// "unavailable here" instead of disappearing.
	_lastEnabled = enabled;
	_buttons[2]->setDisabled(!enabled);
	layoutButtons();
}

void SegmentControl::layoutButtons() {
	const auto gap = st::luxuryWatcherSortSkip;
	const auto available = std::max(0, width() - 2 * gap);
	auto left = 0;
	for (auto i = 0; i != 3; ++i) {
		const auto w = available / 3 + (i == 2 ? available % 3 : 0);
		_buttons[i]->setGeometryToLeft(left, 0, w, height(), width());
		left += w + gap;
	}
}

void SegmentControl::resizeEvent(QResizeEvent *e) {
	RpWidget::resizeEvent(e);
	layoutButtons();
}

TabsBar::TabsBar(QWidget *parent, std::array<QString, 3> labels)
: RpWidget(parent)
, _labels(std::move(labels)) {
	setFixedHeight(st::luxuryWatcherTabHeight);
	for (auto i = 0; i != 3; ++i) {
		_buttons[i] = Ui::CreateChild<ChoiceButton>(this, _labels[i]);
		_buttons[i]->setIsPageTab(true);
		_buttons[i]->setClickedCallback([=] {
			setIndex(i);
			_changes.fire_copy(i);
		});
	}
	setIndex(0);
}

void TabsBar::setIndex(int index) {
	if (index < 0 || index >= 3) {
		return;
	}
	for (auto i = 0; i != 3; ++i) {
		_buttons[i]->setSelected(i == index);
	}
}

void TabsBar::setCounts(std::array<int, 3> counts) {
	for (auto i = 0; i != 3; ++i) {
		_buttons[i]->setText(_labels[i] + (counts[i] > 0
			? u" · "_q + QString::number(counts[i]) : QString()));
	}
	layoutButtons();
}

void TabsBar::resizeEvent(QResizeEvent *e) {
	RpWidget::resizeEvent(e);
	layoutButtons();
}

void TabsBar::layoutButtons() {
	const auto gap = st::luxuryWatcherTabSkip;
	const auto available = std::max(0, width() - 2 * gap);
	auto left = 0;
	for (auto i = 0; i != 3; ++i) {
		const auto w = available / 3 + (i == 2 ? available % 3 : 0);
		_buttons[i]->setGeometryToLeft(left, 0, w, height(), width());
		left += w + gap;
	}
}

EmptyBlock::EmptyBlock(QWidget *parent, const QString &text)
: RpWidget(parent) {
	setText(text);
}

void EmptyBlock::setText(const QString &text) {
	_text.setText(st::boxTextStyle, text, kPlainTextOptions);
	resizeToWidth(width());
	update();
}

int EmptyBlock::resizeGetHeight(int newWidth) {
	return st::luxuryWatcherEmptyGlyphSize
		+ 2 * st::luxuryWatcherDetailSkip
		+ _text.countHeight(std::max(1, newWidth));
}

void EmptyBlock::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	auto hq = PainterHighQualityEnabler(p);
	const auto glyph = st::luxuryWatcherEmptyGlyphSize;
	PaintClockGlyph(p, QPointF((width() - glyph) / 2., 0.), glyph,
		st::windowSubTextFg->c);
	p.setPen(st::windowSubTextFg);
	_text.draw(p, {
		.position = { 0, glyph + st::luxuryWatcherDetailSkip },
		.availableWidth = width(),
		.align = style::al_top,
		.clip = e->rect(),
	});
}

} // namespace LuxuryUi
