// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "luxury/ui/components/image_view.h"

#include "base/call_delayed.h"
#include "luxury/features/message_shot/message_shot.h"
#include "ui/painter.h"

#include "styles/style_chat.h"
#include "styles/style_luxury_styles.h"

namespace {

// ponytail: large previews use the native crossfade; raise only after profiling.
constexpr auto kMaxDiffPixels = 4 * 1024 * 1024;

} // namespace

ImageView::ImageView(QWidget *parent)
	: RpWidget(parent) {
}

void ImageView::setImage(const QImage &image) {
	const auto generation = ++_imageGeneration;
	if (this->_image == image) {
		return;
	}

	const auto set = [=] {
		if (generation != _imageGeneration) {
			return;
		}
		this->_prevImage = this->_image;
		this->_image = image;

		if (!this->_prevImage.isNull()
			&& !image.isNull()
			&& this->_prevImage.size() == image.size()
			&& (qint64(image.width()) * image.height()) <= kMaxDiffPixels) {
			computeDiffImages(this->_prevImage, image);
		} else {
			this->_baseImage = QImage();
			this->_prevDiffImage = QImage();
			this->_newDiffImage = QImage();
		}

		const auto size = image.size() / style::DevicePixelRatio();
		setMinimumSize(size.grownBy(st::imageViewInnerPadding));

		if (this->_animation.animating()) {
			this->_animation.stop();
		}

		if (this->_prevImage.isNull()) {
			update();
			return;
		}

		this->_animation.start(
			[=]
			{
				update();
			},
			0.0,
			1.0,
			300,
			anim::easeInCubic);
	};

	if (this->_image.isNull()) {
		set();
		return;
	}

	base::call_delayed(100, this, set);
}

void ImageView::computeDiffImages(const QImage &prev, const QImage &curr) {
	const auto prevConverted = prev.convertToFormat(QImage::Format_ARGB32_Premultiplied);
	const auto currConverted = curr.convertToFormat(QImage::Format_ARGB32_Premultiplied);
	const auto w = prevConverted.width();
	const auto h = prevConverted.height();

	auto base = currConverted.copy();
	auto prevDiff = prevConverted.copy();
	auto newDiff = currConverted.copy();

	for (auto y = 0; y < h; ++y) {
		const auto *prevLine = reinterpret_cast<const QRgb *>(prevConverted.constScanLine(y));
		const auto *currLine = reinterpret_cast<const QRgb *>(currConverted.constScanLine(y));
		auto *baseLine = reinterpret_cast<QRgb *>(base.scanLine(y));
		auto *prevDiffLine = reinterpret_cast<QRgb *>(prevDiff.scanLine(y));
		auto *newDiffLine = reinterpret_cast<QRgb *>(newDiff.scanLine(y));

		for (auto x = 0; x < w; ++x) {
			if (prevLine[x] == currLine[x]) {
				prevDiffLine[x] = 0;
				newDiffLine[x] = 0;
			} else {
				baseLine[x] = 0;
			}
		}
	}

	this->_baseImage = base;
	this->_prevDiffImage = prevDiff;
	this->_newDiffImage = newDiff;
}

QImage ImageView::getImage() const {
	return _image;
}

void ImageView::paintEvent(QPaintEvent *e) {
	Painter p(this);

	const auto brush = QBrush(LuxuryFeatures::MessageShot::makeDefaultBackgroundColor());

	QPainterPath path;
	path.addRoundedRect(rect(), st::roundRadiusLarge, st::roundRadiusLarge);

	p.fillPath(path, brush);

	if (!_baseImage.isNull()) {
		const auto realRect = rect().marginsRemoved(st::imageViewInnerPadding);

		const auto resizedRect = QRect(
			(realRect.width() - _image.width() / style::DevicePixelRatio()) / 2 + st::imageViewInnerPadding.left(),
			(realRect.height() - _image.height() / style::DevicePixelRatio()) / 2 + st::imageViewInnerPadding.top(),
			_image.width() / style::DevicePixelRatio(),
			_image.height() / style::DevicePixelRatio());

		p.drawImage(resizedRect, _baseImage);

		const auto t = _animation.value(1.0);

		if (t < 1.0) {
			p.setOpacity(1.0 - t);
			p.drawImage(resizedRect, _prevDiffImage);
			p.setOpacity(1.0);
		}

		if (t > 0.0) {
			p.setOpacity(t);
			p.drawImage(resizedRect, _newDiffImage);
			p.setOpacity(1.0);
		}
	} else {
		if (!_prevImage.isNull()) {
			const auto realRect = rect().marginsRemoved(st::imageViewInnerPadding);

			const auto resizedRect = QRect(
				(realRect.width() - _prevImage.width() / style::DevicePixelRatio()) / 2 + st::imageViewInnerPadding.left(),
				(realRect.height() - _prevImage.height() / style::DevicePixelRatio()) / 2 + st::imageViewInnerPadding.top(),
				_prevImage.width() / style::DevicePixelRatio(),
				_prevImage.height() / style::DevicePixelRatio());

			const auto opacity = 1.0 - _animation.value(1.0);
			p.setOpacity(opacity);
			p.drawImage(resizedRect, _prevImage);
			p.setOpacity(1.0);
		}

		if (!_image.isNull()) {
			const auto realRect = rect().marginsRemoved(st::imageViewInnerPadding);

			const auto resizedRect = QRect(
				(realRect.width() - _image.width() / style::DevicePixelRatio()) / 2 + st::imageViewInnerPadding.left(),
				(realRect.height() - _image.height() / style::DevicePixelRatio()) / 2 + st::imageViewInnerPadding.top(),
				_image.width() / style::DevicePixelRatio(),
				_image.height() / style::DevicePixelRatio());

			const auto opacity = _animation.value(1.0);
			p.setOpacity(opacity);
			p.drawImage(resizedRect, _image);
			p.setOpacity(1.0);
		}
	}
}
