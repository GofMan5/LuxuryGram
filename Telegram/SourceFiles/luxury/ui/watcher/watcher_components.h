// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

#include "ui/effects/animations.h"
#include "ui/text/text.h"
#include "ui/widgets/buttons.h"
#include "ui/rp_widget.h"

#include <array>

class QPainter;

namespace LuxuryUi {

[[nodiscard]] QColor AccentColor();
[[nodiscard]] QColor WithAlpha(const QColor &color, float64 alpha);

enum class ToolGlyph {
	Refresh,
	Gear,
};

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

class ToolButton final : public Ui::AbstractButton {
public:
	ToolButton(QWidget *parent, ToolGlyph glyph);

	[[nodiscard]] rpl::producer<> clicks() const {
		return _clicks.events();
	}
	void spin();

protected:
	void paintEvent(QPaintEvent *e) override;
	void onStateChanged(State was, StateChangeSource source) override;
	void focusInEvent(QFocusEvent *e) override;
	void focusOutEvent(QFocusEvent *e) override;

private:
	void paintIcon(QPainter &p, const QColor &color);

	ToolGlyph _glyph = ToolGlyph::Refresh;
	Ui::Animations::Simple _spin;
	rpl::event_stream<> _clicks;

};

class ActionButton final : public Ui::LinkButton {
public:
	ActionButton(QWidget *parent, const QString &text);
	QAccessible::Role accessibilityRole() override {
		return AbstractButton::accessibilityRole();
	}

protected:
	void paintEvent(QPaintEvent *e) override;
	void focusInEvent(QFocusEvent *e) override;
	void focusOutEvent(QFocusEvent *e) override;

};

class ChoiceButton;

// Each segment is a toolkit button: release/cancel, keyboard and
// accessibility behavior stay with lib_ui instead of a second input stack.
class SegmentControl final : public Ui::RpWidget {
public:
	SegmentControl(QWidget *parent, std::array<QString, 3> labels);

	[[nodiscard]] rpl::producer<int> changes() const {
		return _changes.events();
	}
	[[nodiscard]] int contentWidth() const;
	void setIndex(int index);
	void setLastEnabled(bool enabled);

protected:
	void resizeEvent(QResizeEvent *e) override;

private:
	void layoutButtons();

	std::array<ChoiceButton*, 3> _buttons = {};
	bool _lastEnabled = true;
	rpl::event_stream<int> _changes;

};

class TabsBar final : public Ui::RpWidget {
public:
	TabsBar(QWidget *parent, std::array<QString, 3> labels);

	[[nodiscard]] rpl::producer<int> tabChanges() const {
		return _changes.events();
	}
	void setIndex(int index);
	void setCounts(std::array<int, 3> counts);

protected:
	void resizeEvent(QResizeEvent *e) override;

private:
	void layoutButtons();

	std::array<QString, 3> _labels;
	std::array<ChoiceButton*, 3> _buttons = {};
	rpl::event_stream<int> _changes;

};

class EmptyBlock final : public Ui::RpWidget {
public:
	EmptyBlock(QWidget *parent, const QString &text);
	void setText(const QString &text);

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;

private:
	Ui::Text::String _text = { 1 };

};

} // namespace LuxuryUi
