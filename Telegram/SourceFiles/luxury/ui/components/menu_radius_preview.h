// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

#include "ui/rp_widget.h"

namespace Ui {
class RippleAnimation;
} // namespace Ui

class MenuRadiusPreview final : public Ui::RpWidget {
public:
	MenuRadiusPreview(QWidget *parent);

protected:
	void paintEvent(QPaintEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void leaveEvent(QEvent *e) override;

private:
	std::unique_ptr<Ui::RippleAnimation> _ripple;
	int _rippleRadius = -1;
	int _hoverRow = -1;

};
