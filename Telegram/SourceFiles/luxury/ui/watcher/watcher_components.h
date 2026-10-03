// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

#include "ui/effects/animations.h"
#include "ui/rp_widget.h"

#include <array>

class QPainter;

namespace Ui {
class RippleAnimation;
} // namespace Ui

namespace LuxuryUi {

// The Watcher accent: a fixed neon azure, theme-independent. The dark
// minimal look the box goes for needs exactly one saturated highlight,
// and the theme's active-text color varies too much to be trusted with
// it. Pills, online states, duration markers and hover glints all use
// this one color.
[[nodiscard]] QColor AccentColor();

// A color helper: the accent (or any color) at a fixed alpha.
[[nodiscard]] QColor WithAlpha(const QColor &color, float64 alpha);

// Glyphs painted in code instead of icon assets: the refresh circular
// arrow and the settings gear.
enum class ToolGlyph {
	Refresh,
	Gear,
};

// Shared paint helpers for card-based lists. All of them paint into the
// current painter state except where noted.
void PaintRefreshGlyph(QPainter &p, float64 base, const QColor &color);
void PaintGearGlyph(
	QPainter &p,
	float64 base,
	const QColor &color,
	float64 penWidth);
void PaintClockGlyph(
	QPainter &p,
	const QPointF &topLeft,
	float64 size,
	const QColor &color);

// A square icon button with a code-painted glyph, hover brightening and a
// press ripple. spin() rotates the glyph through one easing turn -- the
// refresh feedback.
class ToolButton : public Ui::RpWidget {
public:
	ToolButton(QWidget *parent, ToolGlyph glyph);

	[[nodiscard]] rpl::producer<> clicks() const {
		return _clicks.events();
	}
	void spin();

protected:
	void paintEvent(QPaintEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void enterEventHook(QEnterEvent *e) override;
	void leaveEventHook(QEvent *e) override;

private:
	void paintIcon(QPainter &p, const QColor &color);

	ToolGlyph _glyph = ToolGlyph::Refresh;
	bool _hovered = false;
	bool _pressed = false;
	Ui::Animations::Simple _spin;
	rpl::event_stream<> _clicks;
	std::unique_ptr<Ui::RippleAnimation> _ripple;

};

// The one-click sort control: pill segments laid out at their natural
// width, an active pill gliding between them (150 ms easeOutCubic), and
// per-segment disabling -- a disabled segment paints dimmed and bounces
// presses off instead of selecting.
class SegmentControl : public Ui::RpWidget {
public:
	SegmentControl(QWidget *parent, std::array<QString, 3> labels);

	[[nodiscard]] rpl::producer<int> changes() const {
		return _changes.events();
	}
	[[nodiscard]] int contentWidth() const;
	void setIndex(int index);
	void setLastEnabled(bool enabled);

protected:
	void paintEvent(QPaintEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void enterEventHook(QEnterEvent *e) override;
	void leaveEventHook(QEvent *e) override;

private:
	[[nodiscard]] int segmentLeft(int index) const;
	[[nodiscard]] int segmentWidth(int index) const;
	void select(int index);

	std::array<QString, 3> _labels;
	int _selected = 0;
	bool _lastEnabled = true;
	Ui::Animations::Simple _pillLeft;
	Ui::Animations::Simple _pillWidth;
	rpl::event_stream<int> _changes;

};

// Equal-width segmented tabs: the active tab is a pill gliding between
// segments, each label carries an optional count, and hovering a segment
// lightens its background. Selection fires on press.
class TabsBar : public Ui::RpWidget {
public:
	TabsBar(QWidget *parent, std::array<QString, 3> labels);

	[[nodiscard]] rpl::producer<int> tabChanges() const {
		return _changes.events();
	}
	void setIndex(int index);
	void setCounts(std::array<int, 3> counts);

protected:
	void paintEvent(QPaintEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void leaveEventHook(QEvent *e) override;

private:
	[[nodiscard]] int segmentLeft(int index) const;
	[[nodiscard]] int segmentWidth(int index) const;
	void select(int index);

	std::array<QString, 3> _labels;
	std::array<QString, 3> _counts = {};
	int _selected = 0;
	int _hovered = -1;
	Ui::Animations::Simple _pillLeft;
	rpl::event_stream<int> _changes;

};

// A centered empty state: one dim code-painted glyph above a muted label.
class EmptyBlock : public Ui::RpWidget {
public:
	EmptyBlock(QWidget *parent, const QString &text);

	void setText(const QString &text);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	QString _text;

};

} // namespace LuxuryUi
