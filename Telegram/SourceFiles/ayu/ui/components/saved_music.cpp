// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/ui/components/saved_music.h"

#include "ayu/ayu_settings.h"
#include "ayu/ui/utils/color_utils.h"
#include "ayu/ui/utils/itunes_search.h"
#include "ayu/ui/utils/palette.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_origin.h"
#include "data/data_session.h"
#include "info/profile/info_profile_music_button.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "styles/palette.h"
#include "styles/style_info.h"
#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "ui/image/image.h"
#include "ui/widgets/labels.h"
#include "window/themes/window_theme.h"

#include <QSvgRenderer>

namespace Info::Profile {

namespace {

QColor performerColor(255, 255, 255, 153); // white 60%

QRgb AdjustHsl(QRgb color, float luminance, float saturation = -1.0f) {
	auto hsl = Luxury::Ui::ColorUtils::colorToHSL(color);

	if (saturation > 0.0f) {
		hsl[1] = std::min(hsl[1] * saturation, 1.0f);
	}

	hsl[2] = std::min(hsl[2] * luminance, 1.0f);

	return Luxury::Ui::ColorUtils::HSLToRGB(hsl);
}

QRgb BlendARGB(QRgb color1, QRgb color2, float ratio) {
	const auto inverseRatio = 1.0f - ratio;
	const auto r = static_cast<int>(qRed(color1) * inverseRatio + qRed(color2) * ratio);
	const auto g = static_cast<int>(qGreen(color1) * inverseRatio + qGreen(color2) * ratio);
	const auto b = static_cast<int>(qBlue(color1) * inverseRatio + qBlue(color2) * ratio);
	const auto a = static_cast<int>(qAlpha(color1) * inverseRatio + qAlpha(color2) * ratio);
	return qRgba(r, g, b, a);
}

QColor GetNoCoverBgColor(std::optional<QColor> overrideBg) {
	if (overrideBg) {
		return Ui::BlendColors(
			*overrideBg,
			Qt::black,
			st::infoProfileTopBarActionButtonBgOpacity);
	}

	return st::shadowFg->c;
}

struct Cover
{
	QPixmap pixToDraw;
	QPixmap pixToBg;
	bool noCover;
};

QPixmap MakeNoCoverImage(const QSize &size) {
	static QPixmap result;
	static auto resultTheme = Window::Theme::Background()->id();
	if (!result.isNull() && result.size() == size && resultTheme == Window::Theme::Background()->id()) {
		return result;
	}
	resultTheme = Window::Theme::Background()->id();

	auto image = QImage(size, QImage::Format_ARGB32);
	{
		auto p = Painter(&image);
		auto hq = PainterHighQualityEnabler(p);

		const auto bgColor = Window::Theme::IsNightMode()
								 ? st::windowBoldFg->c.darker()
								 : st::windowBoldFg->c.lighter();
		image.fill(bgColor);

		auto svgIcon = QSvgRenderer(u":/gui/icons/ayu/nocover.svg"_q);
		p.setPen(st::windowBoldFg->p);
		svgIcon.render(&p, QRect(0, 0, size.width(), size.height()));
	}
	const auto img = Image(std::move(image));
	result = img.pix(size, Images::PrepareArgs{.options = Images::Option::RoundSmall});
	return result;
}

} // namespace

Cover GetCurrentCover(
	const std::shared_ptr<Data::DocumentMedia> &dataMedia,
	const QSize &size) {
	if (!dataMedia) {
		return {
			.pixToDraw = MakeNoCoverImage(size),
			.pixToBg = MakeNoCoverImage(size),
			.noCover = true
		};
	}

	auto cover = QPixmap();
	const auto scaled = [&](not_null<Image*> image)
	{
		const auto aspectRatio = Qt::KeepAspectRatioByExpanding;
		const auto targetSize = size * style::DevicePixelRatio();
		return image->size().scaled(targetSize, aspectRatio);
	};
	const auto args = Images::PrepareArgs{
		.options = Images::Option::RoundSmall,
		.outer = size,
	};
	if (const auto normal = dataMedia->thumbnail()) {
		return {
			.pixToDraw = normal->pixNoCache(scaled(normal), args),
			.pixToBg = normal->pixNoCache(),
			.noCover = false
		};
	}

	return {
		.pixToDraw = MakeNoCoverImage(size),
		.pixToBg = MakeNoCoverImage(size),
		.noCover = true
	};
}

std::optional<QRgb> ExtractColorFromCover(const QImage &cover) {
	const auto palette = Luxury::Ui::Palette::from(cover).generate();

	const auto *swatch = palette.darkVibrantSwatch();
	if (!swatch) {
		swatch = palette.mutedSwatch();
	}
	if (!swatch) {
		swatch = palette.darkMutedSwatch();
	}
	if (!swatch) {
		swatch = palette.dominantSwatch();
	}

	if (!swatch) {
		return std::nullopt;
	}

	const auto extractedColor = swatch->rgb();

	constexpr auto whiteColor = qRgb(255, 255, 255);
	const auto contrast = Luxury::Ui::ColorUtils::calculateContrast(whiteColor, extractedColor);

	auto adjustedColor = extractedColor;
	if (contrast > 15.0f) {
		adjustedColor = AdjustHsl(extractedColor, 2.0f);
	} else if (contrast < 10.0f) {
		adjustedColor = AdjustHsl(extractedColor, 0.5f);
	}

	if (Luxury::Ui::ColorUtils::calculateContrast(whiteColor, adjustedColor) < 3.0f) {
		adjustedColor = BlendARGB(adjustedColor, qRgb(0, 0, 0), 0.3f);
	}

	return adjustedColor;
}

LuxuryMusicButton::LuxuryMusicButton(
	QWidget *parent,
	MusicButtonData data,
	std::optional<QColor> overrideBg,
	Fn<void()> handler)
	: RippleButton(parent, st::infoMusicButtonRipple)
	  , _performer(std::make_unique<Ui::FlatLabel>(
		  this,
		  data.performer,
		  st::infoMusicButtonPerformer))
	  , _title(std::make_unique<Ui::FlatLabel>(
		  this,
		  data.title,
		  st::infoMusicButtonTitle))
	  , _mediaView(data.mediaView)
	  , _overrideBg(overrideBg) {
	_performerText = data.performer;
	_titleText = data.title;
	rpl::combine(
		_title->naturalWidthValue(),
		_performer->naturalWidthValue()
	) | rpl::on_next([=]
							 {
								 resizeToWidth(widthNoMargins());
							 },
							 lifetime());

	_title->setAttribute(Qt::WA_TransparentForMouseEvents);
	_performer->setAttribute(Qt::WA_TransparentForMouseEvents);

	downloadAndMakeCover(data.msgId);

	setClickedCallback(std::move(handler));
}

LuxuryMusicButton::~LuxuryMusicButton() = default;

void LuxuryMusicButton::updateData(MusicButtonData data) {
	_performer->setText(data.performer);
	_title->setText(data.title);
	_performerText = data.performer;
	_titleText = data.title;
	_mediaView = data.mediaView;
	downloadAndMakeCover(data.msgId);

	resizeToWidth(widthNoMargins());
}

void LuxuryMusicButton::downloadAndMakeCover(FullMsgId msgId) {
	_coverDownloadLifetime.destroy();
	if (_mediaView && _mediaView->owner()->isSongWithCover() && !_mediaView->thumbnail()) {
		const auto settings = &_mediaView->owner()->session().settings().autoDownload();
		// Data::AutoDownload::Type::Music always returns false
		if (settings->shouldDownload(Data::AutoDownload::Source::User,
									 Data::AutoDownload::Type::File,
									 _mediaView->owner()->size)) {
			_mediaView->thumbnailWanted(Data::FileOrigin(msgId));
			_mediaView->owner()->owner().session().downloaderTaskFinished(
			) | rpl::take_while([=]
			{
				if (_mediaView->thumbnail()) {
					makeCover();
				}
				return !_mediaView->thumbnail();
			}) | rpl::start(_coverDownloadLifetime);
			return;
		}
	}

	makeCover();
}

void LuxuryMusicButton::makeCover() {
	const auto weak = base::make_weak(this);
	const auto generation = ++_coverGeneration;
	const auto &font = st::infoMusicButtonTitle.style.font;
	const auto skip = st::normalFont->spacew / 2;
	const auto size = font->height + skip + font->height;
	const auto adaptiveColor = LuxurySettings::getInstance().adaptiveCoverColor();
	auto cover = GetCurrentCover(_mediaView, QSize(size, size));
	const auto existingDraw = cover.pixToDraw.toImage();
	const auto existingBackground = cover.pixToBg.toImage();
	crl::async([
		weak,
		generation,
		performerText = _performerText,
		titleText = _titleText,
		overrideBg = _overrideBg,
		existingDraw,
		existingBackground,
		noCover = cover.noCover,
		adaptiveColor,
		size
	]() mutable {
		auto background = existingBackground;
		auto fetched = false;
		if (noCover) {
			background = Luxury::Ui::Itunes::FetchCover(
				performerText,
				titleText,
				size);
			fetched = !background.isNull();
			noCover = !fetched;
		}
		const auto extractedColor = (!noCover && adaptiveColor)
			? ExtractColorFromCover(background)
			: std::nullopt;

		crl::on_main([
			weak,
			generation,
			existingDraw,
			background = std::move(background),
			noCover,
			fetched,
			extractedColor,
			overrideBg,
			adaptiveColor,
			size
		]() mutable {
			const auto strong = weak.get();
			if (!strong || strong->_coverGeneration != generation) {
				return;
			}

			auto pixmap = QPixmap();
			if (noCover) {
				pixmap = MakeNoCoverImage(QSize(size, size));
			} else if (fetched) {
				const auto image = Image(background);
				pixmap = image.pix(
					QSize(size, size),
					Images::PrepareArgs{
						.options = Images::Option::RoundSmall,
						.outer = QSize(size, size),
					});
			} else {
				pixmap = QPixmap::fromImage(existingDraw);
			}
			const auto color = extractedColor
				? QColor::fromRgb(*extractedColor)
				: GetNoCoverBgColor(overrideBg);
			const auto effectiveNoCover = noCover
				|| (adaptiveColor && !extractedColor);
			strong->_currentCover = {
				.pix = std::move(pixmap),
				.bg = color,
				.noCover = effectiveNoCover,
			};

			const auto &settings = LuxurySettings::getInstance();
			const auto &current = *strong->_currentCover;
			if (!current.noCover
				&& settings.adaptiveCoverColor()
				&& !current.pix.isNull()) {
				strong->_title->setTextColorOverride(Qt::white);
				strong->_performer->setTextColorOverride(performerColor);
			} else {
				const auto color = overrideBg
					? st::groupCallMembersFg->c
					: st::windowBoldFg->c;
				strong->_title->setTextColorOverride(color);
				strong->_performer->setTextColorOverride(color);
			}

			strong->repaint();
			strong->_title->repaint();
			strong->_performer->repaint();
			strong->_onReady.fire({});
		});
	});
}

void LuxuryMusicButton::paintEvent(QPaintEvent *e) {
	if (!_currentCover) {
		return;
	}

	auto p = Painter(this);

	const auto &font = st::infoMusicButtonTitle.style.font;
	const auto skip = st::normalFont->spacew / 2;
	const auto size = font->height + skip + font->height;

	const auto &settings = LuxurySettings::getInstance();
	const auto cover = _currentCover.value();
	if (cover.noCover || !settings.adaptiveCoverColor()) {
		p.fillRect(e->rect(), cover.bg);
		paintRipple(p, QPoint());
	} else {
		QRadialGradient gradient(rect().topRight(), rect().width() * 2.0);
		gradient.setColorAt(0, cover.bg);
		gradient.setColorAt(1, QColor::fromRgb(AdjustHsl(cover.bg.rgb(), 1.5f)));
		p.fillRect(rect(), gradient);
	}

	if (!cover.pix.isNull()) {
		auto hq = PainterHighQualityEnabler(p);
		const auto coverRect = QRect(st::infoMusicButtonPadding.left(), st::infoMusicButtonPadding.top(), size, size);
		p.drawPixmap(coverRect, cover.pix);
	}
}

int LuxuryMusicButton::resizeGetHeight(int newWidth) {
	const auto padding = st::infoMusicButtonPadding;
	const auto &font = st::infoMusicButtonTitle.style.font;

	const auto top = padding.top();
	const auto skip = st::normalFont->spacew / 2;

	const auto coverSize = font->height + skip + font->height;

	const auto available = newWidth - padding.left() - padding.right() - coverSize;
	_title->resizeToNaturalWidth(available);
	_title->moveToLeft(st::infoMusicButtonPadding.left() + padding.left() + coverSize, top);
	_performer->resizeToNaturalWidth(available);
	_performer->moveToLeft(st::infoMusicButtonPadding.left() + padding.left() + coverSize, top + font->height + skip);

	return padding.top() + font->height + skip + font->height + padding.bottom();
}

} // namespace Info::Profile
