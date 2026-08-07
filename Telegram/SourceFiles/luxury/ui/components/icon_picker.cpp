// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "luxury/ui/components/icon_picker.h"

#include "tray.h"
#include "luxury/luxury_settings.h"
#include "luxury/ui/luxury_logo.h"
#include "core/application.h"
#include "main/main_domain.h"
#include "styles/style_luxury_styles.h"
#include "ui/painter.h"
#include "window/main_window.h"

#ifdef Q_OS_WIN
#include "luxury/utils/windows_utils.h"
#endif

namespace {

const QVector<QString> icons{
	LuxuryAssets::DEFAULT_ICON,
	LuxuryAssets::ALT_ICON,
	LuxuryAssets::DISCORD_ICON,
	LuxuryAssets::SPOTIFY_ICON,
	LuxuryAssets::EXTERA_ICON,
	LuxuryAssets::NOTHING_ICON,
	LuxuryAssets::BARD_ICON,
	LuxuryAssets::YAPLUS_ICON,
	LuxuryAssets::WIN95_ICON,
	LuxuryAssets::CHIBI_ICON,
	LuxuryAssets::CHIBI2_ICON,
	LuxuryAssets::EXTERA2_ICON,
};

const auto rows = static_cast<int>(icons.size()) / IconPicker::kColumns
	+ std::min(1, static_cast<int>(icons.size()) % IconPicker::kColumns);

void applyIcon() {
#ifdef Q_OS_WIN
	LuxuryAssets::loadAppIco();
	reloadAppIconFromTaskBar();
#endif

	Window::OverrideApplicationIcon(LuxuryAssets::currentAppLogo());
	Core::App().refreshApplicationIcon();
	Core::App().tray().updateIconCounters();
	Core::App().domain().notifyUnreadBadgeChanged();
}

} // namespace

IconPicker::IconPicker(QWidget *parent)
	: RpWidget(parent) {
	widthValue() | rpl::on_next([=](int w) {
		const auto cell = w / kColumns;
		const auto iconSize = st::iconPickerIconSize;
		const auto contentSize = iconSize + st::iconPickerImagePadding * 2;
		const auto h = rows * cell - (cell - contentSize);
		resize(w, h);
	}, lifetime());
}

void IconPicker::drawIcon(QPainter &p, const QImage &icon, int x, int y, float strokeOpacity) {
	{
		PainterHighQualityEnabler hq(p);
		p.save();
		p.setPen(QPen(st::boxDividerBg, 0));
		p.setBrush(QBrush(st::boxDividerBg));
		p.setOpacity(strokeOpacity);
		p.drawRoundedRect(
			x + st::iconPickerSelectedPadding,
			y + st::iconPickerSelectedPadding,
			st::iconPickerIconSize + st::iconPickerSelectedPadding * 2,
			st::iconPickerIconSize + st::iconPickerSelectedPadding * 2,
			st::iconPickerSelectedRounding,
			st::iconPickerSelectedRounding
		);
		p.restore();
	}

	const auto rect = QRect(
		x + st::iconPickerImagePadding,
		y + st::iconPickerImagePadding,
		st::iconPickerIconSize,
		st::iconPickerIconSize
	);
	p.drawImage(rect, icon);
}

int IconPicker::cellWidth() const {
	return width() / kColumns;
}

void IconPicker::paintEvent(QPaintEvent *e) {
	Painter p(this);

	const auto cell = cellWidth();
	const auto iconSize = st::iconPickerIconSize;

	for (int row = 0; row < rows; row++) {
		const auto columns = std::min(kColumns, static_cast<int>(icons.size()) - row * kColumns);
		for (int i = 0; i < columns; i++) {
			auto const idx = i + row * kColumns;

			const auto &iconName = icons[idx];
			if (iconName.isEmpty()) {
				continue;
			}
			QImage icon;
			if (const auto cached = _cachedIcons.find(iconName); cached != _cachedIcons.end()) {
				icon = cached->second;
			} else {
				icon = _cachedIcons[iconName] = LuxuryAssets::loadPreview(iconName);
			}
			auto opacity = 0.0f;
			if (iconName == _wasSelected) {
				opacity = 1.0f - _animation.value(1.0f);
			} else if (iconName == LuxuryAssets::currentAppLogoName()) {
				opacity = _wasSelected.isEmpty() ? 1.0f : _animation.value(1.0f);
			}

			const auto x = i * cell + (cell - iconSize) / 2;
			const auto y = row * cell;

			drawIcon(p, icon, x, y, opacity);
		}
	}
}

void IconPicker::mousePressEvent(QMouseEvent *e) {
	const auto &settings = LuxurySettings::getInstance();
	auto changed = false;

	const auto cell = cellWidth();
	const auto iconSize = st::iconPickerIconSize;

	for (int row = 0; row < rows; row++) {
		const auto columns = std::min(kColumns, static_cast<int>(icons.size()) - row * kColumns);
		for (int i = 0; i < columns; i++) {
			auto const idx = i + row * kColumns;

			const auto x = i * cell + (cell - iconSize) / 2;
			const auto y = row * cell;

			if (e->pos().x() >= x && e->pos().x() <= x + iconSize
				&& e->pos().y() >= y && e->pos().y() <= y + iconSize) {
				const auto &iconName = icons[idx];
				if (iconName.isEmpty()) {
					break;
				}

				if (settings.appIcon() != iconName) {
					_wasSelected = settings.appIcon();
					_animation.start(
						[=]
						{
							update();
						},
						0.0,
						1.0,
						200,
						anim::easeOutCubic
					);

					LuxurySettings::getInstance().setAppIcon(iconName);
					changed = true;
					break;
				}
			}
		}
	}

	if (changed) {
		applyIcon();

		repaint();
	}
}
