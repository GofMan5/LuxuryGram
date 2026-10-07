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

	static constexpr int kColumns = 4;

protected:
	void paintEvent(QPaintEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;

private:
	void drawIcon(QPainter &p, const QImage &icon, int x, int y, float strokeOpacity);
	[[nodiscard]] bool applyIcon(int index);
	[[nodiscard]] int cellWidth() const;

	Ui::Animations::Simple _animation;
	QString _wasSelected;
	int _focusedIndex = 0;
	std::unordered_map<QString, QImage> _cachedIcons;
};
