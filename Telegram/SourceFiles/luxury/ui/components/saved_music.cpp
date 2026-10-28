// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "luxury/ui/components/saved_music.h"

#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_origin.h"
#include "data/data_session.h"
#include "info/profile/info_profile_music_button.h"
#include "luxury/ui/utils/color_utils.h"
#include "luxury/ui/utils/itunes_search.h"
#include "luxury/ui/utils/palette.h"
#include "luxury/luxury_settings.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "ui/image/image.h"
#include "ui/widgets/labels.h"
#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "window/themes/window_theme.h"

#include "styles/palette.h"
#include "styles/style_info.h"

#include <QSvgRenderer>
#include <QtCore/QCoreApplication>

#include <thread>

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
	bool noCover = true;
};

QPixmap MakeNoCoverImage(const QSize &size) {
	struct Cache {
		QPixmap pixmap;
		QSize size;
		QColor fill;
	};
	static auto cache = Cache();
	[[maybe_unused]] static const auto cleanup = [] {
		// A QPixmap that outlives QGuiApplication is freed against a platform
		// backend that is already gone, which is a crash on exit for something
		// nothing needs by then.
		if (const auto app = QCoreApplication::instance()) {
			QObject::connect(app, &QCoreApplication::aboutToQuit, [] {
				cache.pixmap = QPixmap();
			});
		}
		return true;
	}();

	const auto fill = Window::Theme::IsNightMode()
		? st::windowBoldFg->c.darker()
		: st::windowBoldFg->c.lighter();
	const auto physical = size * style::DevicePixelRatio();
	if (!cache.pixmap.isNull()
		&& cache.size == physical
		&& cache.fill == fill) {
		return cache.pixmap;
	}

	auto image = QImage(physical, QImage::Format_ARGB32);
	if (image.isNull()) {
		return QPixmap();
	}
	image.fill(fill);
	{
		auto p = Painter(&image);
		auto hq = PainterHighQualityEnabler(p);

		auto svgIcon = QSvgRenderer(u":/gui/icons/luxury/nocover.svg"_q);
		p.setPen(st::windowBoldFg->p);
		svgIcon.render(&p, QRect(QPoint(), physical));
	}
	const auto img = Image(std::move(image));
	cache.pixmap = img.pix(
		size,
		Images::PrepareArgs{ .options = Images::Option::RoundSmall });
	cache.size = physical;
	cache.fill = fill;
	return cache.pixmap;
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

	// White-on-cover contrast above which the cover is brightened to soften glare.
	constexpr auto kTooBright = 15.0f;
	// White-on-cover contrast below which the cover is darkened to lift the text.
	constexpr auto kTooDark = 10.0f;
	// Minimum white-on-cover contrast kept before forcing a darker shade.
	constexpr auto kMinTextContrast = 3.0f;

	auto adjustedColor = extractedColor;
	if (contrast > kTooBright) {
		adjustedColor = AdjustHsl(extractedColor, 2.0f);
	} else if (contrast < kTooDark) {
		adjustedColor = AdjustHsl(extractedColor, 0.5f);
	}

	if (Luxury::Ui::ColorUtils::calculateContrast(whiteColor, adjustedColor) < kMinTextContrast) {
		adjustedColor = Ui::BlendColors(
			QColor::fromRgba(adjustedColor),
			Qt::black,
			0.3).rgba();
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

	style::PaletteChanged(
	) | rpl::on_next([=] {
		refreshTextColors();
		update();
	}, lifetime());

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
	auto job = [
		weak,
		generation,
		performerText = _performerText,
		titleText = _titleText,
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
			adaptiveColor,
			size
		]() mutable {
			const auto strong = weak.get();
			if (!strong || strong->_coverGeneration != generation) {
				return;
			}

			auto pixmap = QPixmap();
			if (fetched) {
				const auto image = Image(std::move(background));
				pixmap = image.pix(
					QSize(size, size),
					Images::PrepareArgs{
						.options = Images::Option::RoundSmall,
						.outer = QSize(size, size),
					});
			} else if (!noCover) {
				pixmap = QPixmap::fromImage(existingDraw);
			}
			const auto effectiveNoCover = noCover
				|| (adaptiveColor && !extractedColor);
			strong->_currentCover = {
				.pix = std::move(pixmap),
				.bg = extractedColor
					? std::make_optional(QColor::fromRgb(*extractedColor))
					: std::nullopt,
				.noCover = effectiveNoCover,
				.placeholder = noCover,
			};
			strong->refreshTextColors();

			strong->repaint();
			strong->_title->repaint();
			strong->_performer->repaint();
			strong->_onReady.fire({});
		});
	};
	// Only the iTunes lookup blocks (a nested event loop for up to two
	// timeouts), and that must not hold a pool worker the rest of the app
	// waits on; a cover that is already here is plain CPU work.
	if (cover.noCover) {
		std::thread(std::move(job)).detach();
	} else {
		crl::async(std::move(job));
	}
}

void LuxuryMusicButton::refreshTextColors() {
	if (!_currentCover) {
		return;
	}
	const auto &cover = *_currentCover;
	if (!cover.noCover
		&& LuxurySettings::getInstance().adaptiveCoverColor()
		&& !cover.pix.isNull()) {
		_title->setTextColorOverride(Qt::white);
		_performer->setTextColorOverride(performerColor);
	} else {
		const auto color = _overrideBg
			? st::groupCallMembersFg->c
			: st::windowBoldFg->c;
		_title->setTextColorOverride(color);
		_performer->setTextColorOverride(color);
	}
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
	const auto &cover = *_currentCover;
	const auto bg = cover.bg.value_or(GetNoCoverBgColor(_overrideBg));
	if (cover.noCover || !settings.adaptiveCoverColor()) {
		p.fillRect(e->rect(), bg);
		paintRipple(p, QPoint());
	} else {
		QRadialGradient gradient(rect().topRight(), rect().width() * 2.0);
		gradient.setColorAt(0, bg);
		gradient.setColorAt(1, QColor::fromRgb(AdjustHsl(bg.rgb(), 1.5f)));
		p.fillRect(rect(), gradient);
		paintRipple(p, QPoint());
	}

	const auto pix = cover.placeholder
		? MakeNoCoverImage(QSize(size, size))
		: cover.pix;
	if (!pix.isNull()) {
		auto hq = PainterHighQualityEnabler(p);
		const auto coverRect = QRect(st::infoMusicButtonPadding.left(), st::infoMusicButtonPadding.top(), size, size);
		p.drawPixmap(coverRect, pix);
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
	_title->moveToLeft(padding.left() + coverSize, top);
	_performer->resizeToNaturalWidth(available);
	_performer->moveToLeft(padding.left() + coverSize, top + font->height + skip);

	return padding.top() + font->height + skip + font->height + padding.bottom();
}

} // namespace Info::Profile
