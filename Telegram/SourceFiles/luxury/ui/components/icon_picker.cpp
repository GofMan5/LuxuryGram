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

#include <crl/crl_on_main.h>
#include <algorithm>
#include <QStyle>
#include <QStyleOption>

namespace {

// Hover previews the selection frame at half strength.
constexpr auto kHoverOpacity = 0.5f;

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
	setMouseTracking(true);
	setAccessibleName(tr::luxury_AppIconPickerAccessible(tr::now));
	widthValue() | rpl::on_next([=](int w) {
		const auto cell = w / kColumns;
		const auto iconSize = st::iconPickerIconSize;
		const auto contentSize = iconSize + st::iconPickerImagePadding * 2;
		const auto h = rows * cell - (cell - contentSize);
		resize(w, h);
	}, lifetime());
}

QAccessible::Role IconPicker::accessibilityRole() {
	return QAccessible::Role::List;
}

Qt::FocusPolicy IconPicker::accessibilityFocusPolicy() {
	return Qt::TabFocus;
}

bool IconPicker::accessibilitySelectionList() const {
	return true;
}

int IconPicker::accessibilityChildCount() const {
	return int(icons.size());
}

QString IconPicker::accessibilityChildName(int index) const {
	return (index >= 0 && index < int(icons.size()))
		? (tr::luxury_AppIconHeader(tr::now)
			+ QStringLiteral(": ")
			+ icons[index])
		: QString();
}

QAccessible::State IconPicker::accessibilityChildState(int index) const {
	auto state = QAccessible::State();
	if (index < 0 || index >= int(icons.size())) {
		state.invisible = true;
		return state;
	}
	state.disabled = !isEnabled();
	state.focusable = isEnabled();
	state.selectable = isEnabled();
	state.selected = (LuxurySettings::getInstance().appIcon() == icons[index]);
	state.focused = hasFocus() && (index == _focusedIndex);
	state.invisible = !isVisible();
	state.offscreen = accessibilityChildRect(index).isEmpty();
	return state;
}

QAccessible::Role IconPicker::accessibilityChildRole() const {
	return QAccessible::Role::ListItem;
}

QRect IconPicker::accessibilityChildRect(int index) const {
	if (index < 0 || index >= int(icons.size())) {
		return QRect();
	}
	const auto padding = st::iconPickerSelectedPadding;
	return iconRect(index).marginsAdded(
		QMargins(padding, padding, padding, padding)).intersected(rect());
}

bool IconPicker::accessibilityChildSupportsActions(int index) const {
	return accessibilityChildIdentity(index) != 0;
}

quintptr IconPicker::accessibilityChildIdentity(int index) const {
	return (index >= 0 && index < int(icons.size()))
		? quintptr(index + 1)
		: 0;
}

int IconPicker::accessibilityChildIndexByIdentity(quintptr identity) const {
	return (identity >= 1 && identity <= quintptr(icons.size()))
		? int(identity - 1)
		: -1;
}

void IconPicker::accessibilityChildSetFocus(quintptr identity) {
	crl::on_main(this, [=] {
		const auto index = accessibilityChildIndexByIdentity(identity);
		if (index < 0 || !isEnabled() || !isVisible()) {
			return;
		}
		_focusedIndex = index;
		if (!hasFocus()) {
			setFocus(Qt::OtherFocusReason);
		}
		update();
	});
}

void IconPicker::accessibilityChildActivate(quintptr identity) {
	crl::on_main(this, [=] {
		const auto index = accessibilityChildIndexByIdentity(identity);
		if (index < 0 || !isEnabled() || !isVisible()) {
			return;
		}
		_focusedIndex = index;
		if (!hasFocus()) {
			setFocus(Qt::OtherFocusReason);
		}
		activateIcon(index);
	});
}

void IconPicker::drawIcon(
		QPainter &p,
		const QImage &icon,
		QRect rect,
		float strokeOpacity) {
	{
		PainterHighQualityEnabler hq(p);
		p.save();
		p.setPen(QPen(st::boxDividerBg, 0));
		p.setBrush(QBrush(st::boxDividerBg));
		p.setOpacity(strokeOpacity);
		const auto padding = st::iconPickerSelectedPadding;
		p.drawRoundedRect(
			rect.marginsAdded(QMargins(padding, padding, padding, padding)),
			st::iconPickerSelectedRounding,
			st::iconPickerSelectedRounding);
		p.restore();
	}

	p.drawImage(rect, icon);
}

QRect IconPicker::iconRect(int index) const {
	const auto cell = cellWidth();
	const auto size = st::iconPickerIconSize;
	return myrtlrect(QRect(
		(index % kColumns) * cell + (cell - size) / 2,
		(index / kColumns) * cell + st::iconPickerImagePadding,
		size,
		size));
}

int IconPicker::cellWidth() const {
	return width() / kColumns;
}

void IconPicker::paintEvent(QPaintEvent *e) {
	Painter p(this);

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

			const auto rect = iconRect(idx);
			if (idx == _hoveredIndex) {
				opacity = std::max(opacity, kHoverOpacity);
			}
			drawIcon(p, icon, rect, opacity);

			if (hasFocus() && idx == _focusedIndex) {
				auto option = QStyleOptionFocusRect();
				const auto padding = st::iconPickerSelectedPadding;
				option.rect = rect.marginsAdded(
					QMargins(padding, padding, padding, padding));
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

void IconPicker::activateIcon(int index) {
	if (index < 0 || index >= int(icons.size())) {
		return;
	}
	const auto selected = std::find(
		icons.cbegin(),
		icons.cend(),
		LuxurySettings::getInstance().appIcon());
	const auto previous = (selected != icons.cend())
		? int(selected - icons.cbegin())
		: -1;
	if (applyIcon(index)) {
		if (previous >= 0 && previous != index) {
			accessibilityChildSelectionChanged(previous);
		}
		accessibilityChildSelectionChanged(index);
		LuxuryAssets::applyAppIcon();
		repaint();
	} else {
		update();
	}
}

int IconPicker::indexAt(QPoint point) const {
	for (auto i = 0, count = int(icons.size()); i != count; ++i) {
		if (!icons[i].isEmpty() && iconRect(i).contains(point)) {
			return i;
		}
	}
	return -1;
}

void IconPicker::setHovered(int index) {
	if (_hoveredIndex == index) {
		return;
	}
	_hoveredIndex = index;
	setCursor((index >= 0) ? style::cur_pointer : style::cur_default);
	update();
}

void IconPicker::mousePressEvent(QMouseEvent *e) {
	if (const auto index = indexAt(e->pos()); index >= 0) {
		_focusedIndex = index;
		activateIcon(index);
	}
}

void IconPicker::mouseMoveEvent(QMouseEvent *e) {
	setHovered(indexAt(e->pos()));
}

void IconPicker::leaveEventHook(QEvent *e) {
	setHovered(-1);
	RpWidget::leaveEventHook(e);
}

void IconPicker::keyPressEvent(QKeyEvent *e) {
	const auto count = static_cast<int>(icons.size());
	auto index = _focusedIndex;
	switch (e->key()) {
	case Qt::Key_Left:
		index = (index + count + (style::RightToLeft() ? 1 : -1)) % count;
		break;
	case Qt::Key_Right:
		index = (index + count + (style::RightToLeft() ? -1 : 1)) % count;
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
		e->accept();
		activateIcon(_focusedIndex);
		return;
	default:
		RpWidget::keyPressEvent(e);
		return;
	}
	e->accept();
	_focusedIndex = index;
	update();
	if (hasFocus()) {
		accessibilityChildFocused(index);
	}
}
