// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

#include "ui/effects/animations.h"
#include "ui/rp_widget.h"

class IconPicker : public Ui::RpWidget
{
public:
	IconPicker(QWidget *parent);
	~IconPicker() = default;

	QAccessible::Role accessibilityRole() override;
	Qt::FocusPolicy accessibilityFocusPolicy() override;
	bool accessibilitySelectionList() const override;
	int accessibilityChildCount() const override;
	QString accessibilityChildName(int index) const override;
	QAccessible::State accessibilityChildState(int index) const override;
	QAccessible::Role accessibilityChildRole() const override;
	QRect accessibilityChildRect(int index) const override;
	bool accessibilityChildSupportsActions(int index) const override;
	quintptr accessibilityChildIdentity(int index) const override;
	int accessibilityChildIndexByIdentity(quintptr identity) const override;
	void accessibilityChildSetFocus(quintptr identity) override;
	void accessibilityChildActivate(quintptr identity) override;

	static constexpr int kColumns = 4;

protected:
	void paintEvent(QPaintEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void leaveEventHook(QEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;

private:
	void drawIcon(QPainter &p, const QImage &icon, QRect rect, float strokeOpacity);
	[[nodiscard]] QRect iconRect(int index) const;
	[[nodiscard]] int indexAt(QPoint point) const;
	void setHovered(int index);
	[[nodiscard]] bool applyIcon(int index);
	void activateIcon(int index);
	[[nodiscard]] int cellWidth() const;

	Ui::Animations::Simple _animation;
	QString _wasSelected;
	int _focusedIndex = 0;
	int _hoveredIndex = -1;
	std::unordered_map<QString, QImage> _cachedIcons;

};
