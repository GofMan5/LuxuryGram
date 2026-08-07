// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "luxury/ui/luxury_logo.h"

#include "luxury/luxury_settings.h"
#include "styles/style_luxury_styles.h"
#include "ui/rect.h"

#include <QSvgRenderer>

static QString LAST_LOADED_NAME;
static QImage LAST_LOADED;
static QImage LAST_LOADED_PAD;

namespace LuxuryAssets {

bool isValidAppIcon(const QString &name) {
	return !name.isEmpty()
		&& name.size() <= 64
		&& !name.contains(u'/')
		&& !name.contains(u'\\')
		&& !name.contains(u':')
		&& QFile::exists(qsl(":/gui/art/luxury/%1/app_icon.ico").arg(name));
}

QString safeAppIconName() {
	const auto &name = LuxurySettings::getInstance().appIcon();
	return isValidAppIcon(name) ? name : DEFAULT_ICON;
}

QString appIcoPath() {
	return cWorkingDir()
		+ u"tdata/LuxuryGram-"_q
		+ safeAppIconName()
		+ u".ico"_q;
}

void loadAppIco() {
	const auto iconName = safeAppIconName();
	const auto iconPath = appIcoPath();

	auto f = QFile(iconPath);
	if (f.exists() && !f.remove()) {
		f.setPermissions(f.permissions() | QFile::WriteOwner);
		if (!f.remove()) {
			LOG(("Failed to replace LuxuryGram application icon: %1"
			).arg(f.errorString()));
			return;
		}
	}
	if (!QFile::copy(
			qsl(":/gui/art/luxury/%1/app_icon.ico").arg(iconName),
			iconPath)) {
		LOG(("Failed to write LuxuryGram application icon: %1").arg(iconPath));
	}
}

QImage CreateImage(const QString &name, const QSize resultImageSize, const int padding = 0) {
	const auto iconSize = resultImageSize.shrunkBy(QMargins(padding, padding, padding, padding));

	const auto pngPath = qsl(":/gui/art/luxury/%1/app.png").arg(name);
	if (QFile::exists(pngPath)) {
		const auto loaded = QImage(pngPath).scaled(iconSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
		auto res = QImage(
			resultImageSize * style::DevicePixelRatio(),
			QImage::Format_ARGB32_Premultiplied);
		res.setDevicePixelRatio(style::DevicePixelRatio());
		res.fill(Qt::transparent);
		{
			auto p = QPainter(&res);
			p.drawImage(QRect(padding, padding, iconSize.width(), iconSize.height()), loaded);
		}
		return res;
	}

	const auto svgPath = qsl(":/gui/art/luxury/%1/app.svg").arg(name);
	if (!QFile::exists(svgPath)) {
		return {};
	}

	auto svg = QSvgRenderer(svgPath);
	auto image = QImage(
		resultImageSize * style::DevicePixelRatio(),
		QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(style::DevicePixelRatio());
	image.fill(Qt::transparent);
	{
		auto p = QPainter(&image);

		QPainterPath path;
		path.addRoundedRect(
			QRect(padding, padding, iconSize.width(), iconSize.height()),
			iconSize.width() / 2.0f,
			iconSize.height() / 2.0f
		);

		p.save();

		p.setRenderHint(QPainter::Antialiasing, true);
		p.setClipPath(path);
		p.setRenderHint(QPainter::Antialiasing, false);

		svg.render(&p, QRect(padding, padding, iconSize.width(), iconSize.height()));

		p.restore();
	}
	return image;
}

void loadIcons() {
	const auto iconName = safeAppIconName();
	if (LAST_LOADED_NAME != iconName) {
		LAST_LOADED_NAME = iconName;
		LAST_LOADED = CreateImage(iconName, Size(256));
		LAST_LOADED_PAD = CreateImage(iconName, Size(256), 12);
	}
}

QImage loadPreview(const QString &name) {
	return CreateImage(name, Size(st::iconPickerIconSize), st::iconPickerImagePadding);
}

QString currentAppLogoName() {
	if (LAST_LOADED_NAME.isEmpty()) {
		loadIcons();
	}
	return LAST_LOADED_NAME;
}

QImage currentAppLogo() {
	loadIcons();
	return LAST_LOADED;
}

QImage currentAppLogoPad() {
	loadIcons();
	return LAST_LOADED_PAD;
}

}
