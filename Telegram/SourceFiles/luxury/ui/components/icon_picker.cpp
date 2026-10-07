// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "luxury/ui/components/icon_picker.h"

#include "lang_auto.h"
#include "luxury/ui/luxury_logo.h"
#include "luxury/luxury_settings.h"
#include "ui/painter.h"

#include "styles/style_luxury_styles.h"

#include <QStyle>
#include <QStyleOption>

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

} // namespace

IconPicker::IconPicker(QWidget *parent)
	: RpWidget(parent) {
	setFocusPolicy(Qt::StrongFocus);
	setAccessibleName(tr::luxury_AppIconPickerAccessible(tr::now));
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

			if (hasFocus() && idx == _focusedIndex) {
				auto option = QStyleOptionFocusRect();
				option.rect = QRect(
					x + st::iconPickerSelectedPadding,
					y + st::iconPickerSelectedPadding,
					st::iconPickerIconSize + st::iconPickerSelectedPadding * 2,
					st::iconPickerIconSize + st::iconPickerSelectedPadding * 2
				);
				option.state |= QStyle::State_KeyboardFocusChange;
				style()->drawPrimitive(QStyle::PE_FrameFocusRect, &option, &p, this);
			}
		}
	}
}

bool IconPicker::applyIcon(int index) {
	const auto &iconName = icons[index];
	if (iconName.isEmpty()) {
		return false;
	}

	const auto &settings = LuxurySettings::getInstance();
	if (settings.appIcon() == iconName) {
		return false;
	}

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
	return true;
}

void IconPicker::mousePressEvent(QMouseEvent *e) {
	const auto cell = cellWidth();
	const auto iconSize = st::iconPickerIconSize;

	for (int row = 0; row < rows; row++) {
		const auto columns = std::min(kColumns, static_cast<int>(icons.size()) - row * kColumns);
		for (int i = 0; i < columns; i++) {
			auto const idx = i + row * kColumns;

			const auto x = i * cell + (cell - iconSize) / 2 + st::iconPickerImagePadding;
			const auto y = row * cell + st::iconPickerImagePadding;

			if (e->pos().x() >= x && e->pos().x() <= x + iconSize
				&& e->pos().y() >= y && e->pos().y() <= y + iconSize) {
				_focusedIndex = idx;
				if (applyIcon(idx)) {
					LuxuryAssets::applyAppIcon();

					repaint();
				}
				return;
			}
		}
	}
}

void IconPicker::keyPressEvent(QKeyEvent *e) {
	const auto count = static_cast<int>(icons.size());
	auto index = _focusedIndex;
	switch (e->key()) {
	case Qt::Key_Left:
		index = (index + count - 1) % count;
		break;
	case Qt::Key_Right:
		index = (index + 1) % count;
		break;
	case Qt::Key_Up:
		index = (index + count - kColumns) % count;
		break;
	case Qt::Key_Down:
		index = (index + kColumns) % count;
		break;
	case Qt::Key_Enter:
	case Qt::Key_Return:
	case Qt::Key_Space:
		if (applyIcon(_focusedIndex)) {
			LuxuryAssets::applyAppIcon();

			repaint();
		}
		return;
	default:
		RpWidget::keyPressEvent(e);
		return;
	}
	_focusedIndex = index;
	update();
}
