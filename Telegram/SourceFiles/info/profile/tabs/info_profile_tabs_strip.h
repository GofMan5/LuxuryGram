/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"
#include "ui/effects/animations.h"
#include "ui/text/text.h"
#include "ui/widgets/shadow.h"

namespace style {
struct ProfileTabsStrip;
} // namespace style

namespace Ui {
class RippleAnimation;
} // namespace Ui

namespace Info::Profile {

struct StripTab {
	QString id;
	TextWithEntities text;
};

class TabsStrip final : public Ui::RpWidget {
public:
	TabsStrip(QWidget *parent, const style::ProfileTabsStrip &st);

	void setTextContext(Ui::Text::MarkedContext context);
	void setTabs(std::vector<StripTab> tabs);
	void setActiveTab(const QString &id);
	void trackVerticalScroll(rpl::producer<> scrolls);

	[[nodiscard]] rpl::producer<QString> activated() const;
	[[nodiscard]] rpl::producer<QString> contextMenuRequests() const;

	QAccessible::Role accessibilityRole() override;
	std::optional<Qt::Orientation> accessibilityOrientation() const override;
	bool accessibilitySelectionList() const override;
	int accessibilityChildCount() const override;
	QAccessible::Role accessibilityChildRole() const override;
	QString accessibilityChildName(int index) const override;
	QAccessible::State accessibilityChildState(int index) const override;
	QRect accessibilityChildRect(int index) const override;
	bool accessibilityChildSupportsActions(int index) const override;
	quintptr accessibilityChildIdentity(int index) const override;
	int accessibilityChildIndexByIdentity(quintptr identity) const override;
	void accessibilityChildSetFocus(quintptr identity) override;
	void accessibilityChildActivate(quintptr identity) override;

private:
	struct Button {
		StripTab tab;
		quintptr identity = 0;
		QRect geometry;
		Ui::Text::String text;
		std::unique_ptr<Ui::RippleAnimation> ripple;
	};

	int resizeGetHeight(int newWidth) override;
	void wheelEvent(QWheelEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void contextMenuEvent(QContextMenuEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;
	void focusInEvent(QFocusEvent *e) override;
	void focusOutEvent(QFocusEvent *e) override;
	bool eventHook(QEvent *e) override;

	void validateContent(QRect island);
	void invalidate();

	void setSelected(int index);
	void setActive(int index);
	void setFocused(int index);
	void activateTab(int index);
	void notifyActiveChanged(int previous);
	void scrollToTab(int index);
	void scrollTo(float64 value);
	void addRipple(int index, QPoint position);
	void stopPressedRipple();
	void markVerticalScroll();
	void updatePointerAimed();
	void wheelScrollBy(float64 delta);
	[[nodiscard]] bool wheelScrollsTabs(Qt::ScrollPhase phase) const;
	[[nodiscard]] int indexAt(QPoint position) const;
	[[nodiscard]] QPoint contentOrigin() const;
	[[nodiscard]] QRect islandRect() const;
	[[nodiscard]] int islandInteriorWidth() const;
	[[nodiscard]] QRect highlightRect(int index) const;
	[[nodiscard]] QRectF currentHighlightRect() const;
	[[nodiscard]] int scrollValue() const;

	const style::ProfileTabsStrip &_st;
	Ui::Text::MarkedContext _context;
	Ui::BoxShadow _shadow;
	QImage _content;
	std::vector<Button> _buttons;
	rpl::event_stream<QString> _activated;
	rpl::event_stream<QString> _contextMenuRequests;
	std::optional<Qt::Orientation> _locked;
	int _dragx = 0;
	int _pressx = 0;
	float64 _dragscroll = 0.;
	float64 _scroll = 0.;
	float64 _scrollTo = 0.;
	crl::time _verticalScrollAt = 0;
	QPoint _lastCursorPosition;
	Ui::Animations::Simple _scrollAnimation;
	Ui::Animations::Simple _activeAnimation;
	QRect _activeFrom;
	QRect _activeTo;
	int _scrollMax = 0;
	int _fullWidth = 0;
	int _selected = -1;
	int _pressed = -1;
	int _active = -1;
	int _wasActive = -1;
	int _focused = -1;
	quintptr _identityCounter = 0;
	bool _keyboardFocus = false;
	bool _contentValid = false;
	bool _pointerAimed = false;

};

} // namespace Info::Profile
