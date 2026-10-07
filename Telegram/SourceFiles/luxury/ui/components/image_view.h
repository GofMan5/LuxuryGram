// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

#include "ui/effects/animations.h"
#include "ui/rp_widget.h"

class ImageView : public Ui::RpWidget
{
public:
	ImageView(QWidget *parent);

	void setImage(const QImage &image);
	QImage getImage() const;

protected:
	void paintEvent(QPaintEvent *e) override;

	void computeDiffImages(const QImage &prev, const QImage &curr);

private:
	QImage _image;
	QImage _prevImage;
	QImage _baseImage;
	QImage _prevDiffImage;
	QImage _newDiffImage;

	Ui::Animations::Simple _animation;
	int _imageGeneration = 0;

};
