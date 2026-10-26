/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "chat_helpers/tabbed_selector.h"

#include "chat_helpers/emoji_list_widget.h"
#include "chat_helpers/stickers_list_widget.h"
#include "chat_helpers/gifs_list_widget.h"
#include "base/weak_qptr.h"
#include "menu/menu_send.h"
#include "ui/controls/swipe_handler.h"
#include "ui/controls/tabbed_search.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/shadow.h"
#include "ui/widgets/discrete_sliders.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/scroll_area.h"
#include "ui/layers/box_content.h"
#include "ui/image/image_prepare.h"
#include "ui/cached_round_corners.h"
#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "window/window_session_controller.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "storage/localstorage.h"
#include "data/data_channel.h"
#include "data/data_emoji_statuses.h"
#include "data/data_session.h"
#include "data/data_changes.h"
#include "data/stickers/data_stickers.h"
#include "data/stickers/data_custom_emoji.h" // AllowEmojiWithoutPremium.
#include "boxes/premium_preview_box.h"
#include "lang/lang_keys.h"
#include "mainwindow.h"
#include "apiwrap.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_basic.h"

#include <crl/crl_on_main.h>
#include <QApplication>
#include <QKeyEvent>

namespace ChatHelpers {

class TabbedSelector::TabsSlider final : public Ui::SettingsSlider {
public:
	TabsSlider(TabbedSelector *parent, const style::SettingsSlider &st);
	void setTabs(
		const std::vector<SelectorTab> &types,
		const std::vector<QString> &labels);
	void notifySelectionChanged(int previous);

	QAccessible::Role accessibilityRole() override;
	Qt::FocusPolicy accessibilityFocusPolicy() override;
	std::optional<Qt::Orientation> accessibilityOrientation() const override;
	bool accessibilitySelectionList() const override;
	int accessibilityChildCount() const override;
	QAccessible::Role accessibilityChildRole() const override;
	QString accessibilityChildName(int index) const override;
	QAccessible::State accessibilityChildState(int index) const override;
	QRect accessibilityChildRect(int index) const override;
	bool accessibilityChildSupportsActions(int index) const override;
	quintptr accessibilityChildIdentity(int index) const override;
	int accessibilityChildIndexByIdentity(quintptr identity) const override;
	void accessibilityChildSetFocus(quintptr identity) override;
	void accessibilityChildActivate(quintptr identity) override;

protected:
	void keyPressEvent(QKeyEvent *e) override;
	void keyReleaseEvent(QKeyEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void focusInEvent(QFocusEvent *e) override;
	void focusOutEvent(QFocusEvent *e) override;
	void paintEvent(QPaintEvent *e) override;

private:
	int committedIndex() const;
	void setFocused(int index);
	bool focusTab(quintptr identity);
	void activate(int index);

	const not_null<TabbedSelector*> _owner;
	std::vector<SelectorTab> _types;
	std::vector<QString> _labels;
	std::vector<quintptr> _identities;
	quintptr _identityCounter = 0;
	int _focused = -1;
	bool _keyboardFocus = false;

};

TabbedSelector::TabsSlider::TabsSlider(
		TabbedSelector *parent,
		const style::SettingsSlider &st)
: SettingsSlider(parent, st)
, _owner(parent) {
	setFocusPolicy(Qt::TabFocus);
}

void TabbedSelector::TabsSlider::setTabs(
		const std::vector<SelectorTab> &types,
		const std::vector<QString> &labels) {
	Expects(types.size() == labels.size());
	const auto focused = accessibilityChildIdentity(_focused);
	auto identities = std::vector<quintptr>();
	identities.reserve(types.size());
	for (const auto type : types) {
		const auto i = ranges::find(_types, type);
		const auto identity = (i != end(_types))
			? _identities[i - begin(_types)]
			: ++_identityCounter;
		Assert(identity != 0);
		identities.push_back(identity);
	}
	_types = types;
	_labels = labels;
	_identities = std::move(identities);
	_focused = accessibilityChildIndexByIdentity(focused);
	if (_focused < 0 && !_types.empty()) {
		_focused = std::max(committedIndex(), 0);
	}
	const auto weak = base::make_weak(this);
	setSections(labels);
	if (!weak) {
		return;
	}
	setFocusPolicy(_types.empty() ? Qt::NoFocus : Qt::TabFocus);
	if (!weak) {
		return;
	}
	crl::on_main(this, [=] {
		const auto weak = base::make_weak(this);
		QAccessibleEvent event(this, QAccessible::ObjectReorder);
		QAccessible::updateAccessibility(&event);
		if (weak && hasFocus() && _focused >= 0) {
			accessibilityChildFocused(_focused);
		}
	});
}

int TabbedSelector::TabsSlider::committedIndex() const {
	const auto i = ranges::find(_types, _owner->_currentTabType);
	return (i != end(_types)) ? int(i - begin(_types)) : -1;
}

void TabbedSelector::TabsSlider::notifySelectionChanged(int previous) {
	const auto was = accessibilityChildIdentity(previous);
	const auto now = accessibilityChildIdentity(committedIndex());
	crl::on_main(this, [=] {
		const auto weak = base::make_weak(this);
		const auto previous = accessibilityChildIndexByIdentity(was);
		if (previous >= 0) {
			accessibilityChildSelectionChanged(previous);
		}
		if (weak) {
			const auto index = accessibilityChildIndexByIdentity(now);
			if (index >= 0 && index == committedIndex()) {
				accessibilityChildSelectionChanged(index);
			}
		}
	});
}

void TabbedSelector::TabsSlider::setFocused(int index) {
	if (index < 0 || index >= int(_types.size())) {
		return;
	}
	const auto changed = (_focused != index);
	_focused = index;
	update();
	if (changed && hasFocus()) {
		accessibilityChildFocused(index);
	}
}

bool TabbedSelector::TabsSlider::focusTab(quintptr identity) {
	const auto index = accessibilityChildIndexByIdentity(identity);
	if (index < 0 || !isEnabled() || !isVisible()) {
		return false;
	}
	const auto focus = QPointer<QWidget>(QApplication::focusWidget());
	const auto generation = _owner->_slideGeneration;
	const auto weak = base::make_weak(this);
	_keyboardFocus = true;
	setFocused(index);
	if (!weak
		|| _owner->_slideGeneration != generation
		|| QApplication::focusWidget() != focus.data()
		|| accessibilityChildIdentity(_focused) != identity
		|| !isVisible()
		|| !isEnabled()) {
		return false;
	}
	if (!hasFocus()) {
		setFocus(Qt::OtherFocusReason);
	}
	return weak
		&& _owner->_slideGeneration == generation
		&& hasFocus()
		&& isVisible()
		&& isEnabled()
		&& accessibilityChildIdentity(_focused) == identity;
}

void TabbedSelector::TabsSlider::activate(int index) {
	const auto identity = accessibilityChildIdentity(index);
	if (focusTab(identity)) {
		setActiveSection(accessibilityChildIndexByIdentity(identity));
	}
}

void TabbedSelector::TabsSlider::keyPressEvent(QKeyEvent *e) {
	if (!isEnabled() || !isVisible() || _types.empty()
		|| (e->modifiers() & (Qt::ControlModifier
			| Qt::AltModifier
			| Qt::MetaModifier))) {
		SettingsSlider::keyPressEvent(e);
		return;
	}
	const auto current = (_focused >= 0) ? _focused : 0;
	const auto last = int(_types.size()) - 1;
	auto next = current;
	switch (e->key()) {
	case Qt::Key_Left:
		next = std::clamp(current + (style::RightToLeft() ? 1 : -1), 0, last);
		break;
	case Qt::Key_Right:
		next = std::clamp(current + (style::RightToLeft() ? -1 : 1), 0, last);
		break;
	case Qt::Key_Home: next = 0; break;
	case Qt::Key_End: next = last; break;
	case Qt::Key_Enter:
	case Qt::Key_Return:
	case Qt::Key_Space:
		e->accept();
		if (!e->isAutoRepeat()) {
			activate(current);
		}
		return;
	default:
		SettingsSlider::keyPressEvent(e);
		return;
	}
	e->accept();
	_keyboardFocus = true;
	setFocused(next);
}

void TabbedSelector::TabsSlider::keyReleaseEvent(QKeyEvent *e) {
	if (isEnabled() && isVisible() && !_types.empty()
		&& !(e->modifiers() & (Qt::ControlModifier
			| Qt::AltModifier
			| Qt::MetaModifier))
		&& (e->key() == Qt::Key_Enter
			|| e->key() == Qt::Key_Return
			|| e->key() == Qt::Key_Space)) {
		e->accept();
	} else {
		SettingsSlider::keyReleaseEvent(e);
	}
}

void TabbedSelector::TabsSlider::mouseReleaseEvent(QMouseEvent *e) {
	_keyboardFocus = false;
	const auto weak = base::make_weak(this);
	SettingsSlider::mouseReleaseEvent(e);
	if (weak) {
		_keyboardFocus = false;
		setFocused(activeSection());
	}
}

void TabbedSelector::TabsSlider::focusInEvent(QFocusEvent *e) {
	_keyboardFocus = (e->reason() != Qt::MouseFocusReason);
	if (_focused < 0
		|| e->reason() == Qt::TabFocusReason
		|| e->reason() == Qt::BacktabFocusReason) {
		_focused = _types.empty() ? -1 : std::max(committedIndex(), 0);
	}
	const auto weak = base::make_weak(this);
	SettingsSlider::focusInEvent(e);
	if (weak) {
		update();
	}
}

void TabbedSelector::TabsSlider::focusOutEvent(QFocusEvent *e) {
	_keyboardFocus = false;
	const auto weak = base::make_weak(this);
	SettingsSlider::focusOutEvent(e);
	if (weak) {
		update();
	}
}

void TabbedSelector::TabsSlider::paintEvent(QPaintEvent *e) {
	SettingsSlider::paintEvent(e);
	if (!hasFocus() || !_keyboardFocus || !isEnabled() || _focused < 0) {
		return;
	}
	auto p = QPainter(this);
	p.setPen(QPen(st::windowFg->c, st::lineWidth));
	p.setBrush(Qt::NoBrush);
	p.drawRect(accessibilityChildRect(_focused).marginsRemoved(QMargins(
		st::buttonFocusInset,
		st::buttonFocusInset,
		st::buttonFocusInset,
		st::buttonFocusInset)));
}

QAccessible::Role TabbedSelector::TabsSlider::accessibilityRole() {
	return QAccessible::PageTabList;
}

Qt::FocusPolicy TabbedSelector::TabsSlider::accessibilityFocusPolicy() {
	return Qt::NoFocus;
}

std::optional<Qt::Orientation> TabbedSelector::TabsSlider::accessibilityOrientation() const {
	return Qt::Horizontal;
}

bool TabbedSelector::TabsSlider::accessibilitySelectionList() const {
	return true;
}

int TabbedSelector::TabsSlider::accessibilityChildCount() const {
	return int(_types.size());
}

QAccessible::Role TabbedSelector::TabsSlider::accessibilityChildRole() const {
	return QAccessible::PageTab;
}

QString TabbedSelector::TabsSlider::accessibilityChildName(int index) const {
	return (index >= 0 && index < int(_labels.size()))
		? _labels[index]
		: QString();
}

QAccessible::State TabbedSelector::TabsSlider::accessibilityChildState(int index) const {
	auto state = QAccessible::State();
	if (index < 0 || index >= int(_types.size())) {
		state.invisible = true;
		return state;
	}
	state.disabled = !isEnabled();
	state.focusable = isEnabled();
	state.selectable = isEnabled();
	state.selected = (index == committedIndex());
	state.focused = hasFocus() && (index == _focused);
	state.invisible = !isVisible();
	state.offscreen = accessibilityChildRect(index).isEmpty();
	return state;
}

QRect TabbedSelector::TabsSlider::accessibilityChildRect(int index) const {
	auto result = QRect();
	auto i = 0;
	enumerateSections([&](const Section &section) {
		if (i++ == index) {
			result = myrtlrect(QRect(section.left, 0, section.width, height()));
			return false;
		}
		return true;
	});
	return result.intersected(rect());
}

bool TabbedSelector::TabsSlider::accessibilityChildSupportsActions(int index) const {
	return accessibilityChildIdentity(index) != 0;
}

quintptr TabbedSelector::TabsSlider::accessibilityChildIdentity(int index) const {
	return (index >= 0 && index < int(_identities.size()))
		? _identities[index]
		: 0;
}

int TabbedSelector::TabsSlider::accessibilityChildIndexByIdentity(quintptr identity) const {
	if (identity) {
		const auto i = ranges::find(_identities, identity);
		if (i != end(_identities)) {
			return int(i - begin(_identities));
		}
	}
	return -1;
}

void TabbedSelector::TabsSlider::accessibilityChildSetFocus(quintptr identity) {
	const auto generation = _owner->_slideGeneration;
	crl::on_main(this, [=] {
		if (_owner->_slideGeneration == generation) {
			focusTab(identity);
		}
	});
}

void TabbedSelector::TabsSlider::accessibilityChildActivate(quintptr identity) {
	const auto generation = _owner->_slideGeneration;
	crl::on_main(this, [=] {
		if (_owner->_slideGeneration == generation) {
			activate(accessibilityChildIndexByIdentity(identity));
		}
	});
}

class TabbedSelector::SlideAnimation : public Ui::RoundShadowAnimation {
public:
	enum class Direction {
		LeftToRight,
		RightToLeft,
	};
	void setFinalImages(Direction direction, QImage &&left, QImage &&right, QRect inner, bool wasSectionIcons);

	void start();
	void paintFrame(QPainter &p, const style::EmojiPan &st, float64 dt, float64 opacity);

private:
	Direction _direction = Direction::LeftToRight;
	QPixmap _leftImage, _rightImage;
	int _width = 0;
	int _height = 0;
	int _innerLeft = 0;
	int _innerTop = 0;
	int _innerRight = 0;
	int _innerBottom = 0;
	int _innerWidth = 0;
	int _innerHeight = 0;

	int _painterInnerLeft = 0;
	int _painterInnerTop = 0;
	int _painterInnerWidth = 0;
	int _painterInnerBottom = 0;
	int _painterCategoriesTop = 0;
	int _painterInnerHeight = 0;
	int _painterInnerRight = 0;

	int _frameIntsPerLineAdd = 0;
	bool _wasSectionIcons = false;

};

void TabbedSelector::SlideAnimation::setFinalImages(Direction direction, QImage &&left, QImage &&right, QRect inner, bool wasSectionIcons) {
	Expects(!started());
	_direction = direction;
	_leftImage = QPixmap::fromImage(std::move(left).convertToFormat(QImage::Format_ARGB32_Premultiplied), Qt::ColorOnly);
	_rightImage = QPixmap::fromImage(std::move(right).convertToFormat(QImage::Format_ARGB32_Premultiplied), Qt::ColorOnly);

	Assert(!_leftImage.isNull());
	Assert(!_rightImage.isNull());
	_width = _leftImage.width();
	_height = _rightImage.height();
	Assert(!(_width % style::DevicePixelRatio()));
	Assert(!(_height % style::DevicePixelRatio()));
	Assert(_leftImage.devicePixelRatio() == _rightImage.devicePixelRatio());
	Assert(_rightImage.width() == _width);
	Assert(_rightImage.height() == _height);
	Assert(QRect(0, 0, _width, _height).contains(inner));
	_innerLeft = inner.x();
	_innerTop = inner.y();
	_innerWidth = inner.width();
	_innerHeight = inner.height();
	Assert(!(_innerLeft % style::DevicePixelRatio()));
	Assert(!(_innerTop % style::DevicePixelRatio()));
	Assert(!(_innerWidth % style::DevicePixelRatio()));
	Assert(!(_innerHeight % style::DevicePixelRatio()));
	_innerRight = _innerLeft + _innerWidth;
	_innerBottom = _innerTop + _innerHeight;

	_painterInnerLeft = _innerLeft / style::DevicePixelRatio();
	_painterInnerTop = _innerTop / style::DevicePixelRatio();
	_painterInnerRight = _innerRight / style::DevicePixelRatio();
	_painterInnerBottom = _innerBottom / style::DevicePixelRatio();
	_painterInnerWidth = _innerWidth / style::DevicePixelRatio();
	_painterInnerHeight = _innerHeight / style::DevicePixelRatio();
	_painterCategoriesTop = _painterInnerBottom - st::defaultEmojiPan.footer;

	_wasSectionIcons = wasSectionIcons;
}

void TabbedSelector::SlideAnimation::start() {
	Assert(!_leftImage.isNull());
	Assert(!_rightImage.isNull());
	RoundShadowAnimation::start(_width, _height, _leftImage.devicePixelRatio());
	auto checkCorner = [this](const Corner &corner) {
		if (!corner.valid()) return;
		Assert(corner.width <= _innerWidth);
		Assert(corner.height <= _innerHeight);
	};
	checkCorner(_topLeft);
	checkCorner(_topRight);
	checkCorner(_bottomLeft);
	checkCorner(_bottomRight);
	_frameIntsPerLineAdd = (_width - _innerWidth) + _frameIntsPerLineAdded;
}

void TabbedSelector::SlideAnimation::paintFrame(
		QPainter &p,
		const style::EmojiPan &st,
		float64 dt,
		float64 opacity) {
	Expects(started());
	Expects(dt >= 0.);

	_frameAlpha = anim::interpolate(1, 256, opacity);

	auto leftToRight = (_direction == Direction::LeftToRight);

	auto easeOut = anim::easeOutCirc(1., dt);
	auto easeIn = anim::easeInCirc(1., dt);

	auto arrivingCoord = anim::interpolate(_innerWidth, 0, easeOut);
	auto departingCoord = anim::interpolate(0, _innerWidth, easeIn);
	if (auto decrease = (arrivingCoord % style::DevicePixelRatio())) {
		arrivingCoord -= decrease;
	}
	if (auto decrease = (departingCoord % style::DevicePixelRatio())) {
		departingCoord -= decrease;
	}
	auto arrivingAlpha = easeIn;
	auto departingAlpha = 1. - easeOut;
	auto leftCoord = (leftToRight ? arrivingCoord : departingCoord) * -1;
	auto leftAlpha = (leftToRight ? arrivingAlpha : departingAlpha);
	auto rightCoord = (leftToRight ? departingCoord : arrivingCoord);
	auto rightAlpha = (leftToRight ? departingAlpha : arrivingAlpha);

	// _innerLeft ..(left).. leftTo ..(both).. bothTo ..(none).. noneTo ..(right).. _innerRight
	auto leftTo = _innerLeft
		+ std::clamp(_innerWidth + leftCoord, 0, _innerWidth);
	auto rightFrom = _innerLeft + std::clamp(rightCoord, 0, _innerWidth);
	auto painterRightFrom = rightFrom / style::DevicePixelRatio();
	if (opacity < 1.) {
		_frame.fill(Qt::transparent);
	}
	{
		auto p = QPainter(&_frame);
		p.setOpacity(opacity);
		p.fillRect(_painterInnerLeft, _painterInnerTop, _painterInnerWidth, _painterCategoriesTop - _painterInnerTop, st.bg);
		p.fillRect(_painterInnerLeft, _painterCategoriesTop, _painterInnerWidth, _painterInnerBottom - _painterCategoriesTop, _wasSectionIcons ? st.categoriesBg : st.bg);
		p.setCompositionMode(QPainter::CompositionMode_SourceOver);
		if (leftTo > _innerLeft) {
			p.setOpacity(opacity * leftAlpha);
			p.drawPixmap(_painterInnerLeft, _painterInnerTop, _leftImage, _innerLeft - leftCoord, _innerTop, leftTo - _innerLeft, _innerHeight);
		}
		if (rightFrom < _innerRight) {
			p.setOpacity(opacity * rightAlpha);
			p.drawPixmap(painterRightFrom, _painterInnerTop, _rightImage, _innerLeft, _innerTop, _innerRight - rightFrom, _innerHeight);
		}
	}

	// Draw corners
	//paintCorner(_topLeft, _innerLeft, _innerTop);
	//paintCorner(_topRight, _innerRight - _topRight.width, _innerTop);
	paintCorner(_bottomLeft, _innerLeft, _innerBottom - _bottomLeft.height);
	paintCorner(_bottomRight, _innerRight - _bottomRight.width, _innerBottom - _bottomRight.height);

	// Draw shadow upon the transparent
	auto outerLeft = _innerLeft;
	auto outerTop = _innerTop;
	auto outerRight = _innerRight;
	auto outerBottom = _innerBottom;
	if (_shadow.valid()) {
		outerLeft -= _shadow.extend.left();
		outerTop -= _shadow.extend.top();
		outerRight += _shadow.extend.right();
		outerBottom += _shadow.extend.bottom();
	}
	if (style::DevicePixelRatio() > 1) {
		if (auto skipLeft = (outerLeft % style::DevicePixelRatio())) {
			outerLeft -= skipLeft;
		}
		if (auto skipTop = (outerTop % style::DevicePixelRatio())) {
			outerTop -= skipTop;
		}
		if (auto skipRight = (outerRight % style::DevicePixelRatio())) {
			outerRight += (style::DevicePixelRatio() - skipRight);
		}
		if (auto skipBottom = (outerBottom % style::DevicePixelRatio())) {
			outerBottom += (style::DevicePixelRatio() - skipBottom);
		}
	}

	if (opacity == 1.) {
		// Fill above the frame top with transparent.
		auto fillTopInts = (_frameInts + outerTop * _frameIntsPerLine + outerLeft);
		auto fillWidth = (outerRight - outerLeft) * sizeof(uint32);
		for (auto fillTop = _innerTop - outerTop; fillTop != 0; --fillTop) {
			memset(fillTopInts, 0, fillWidth);
			fillTopInts += _frameIntsPerLine;
		}

		// Fill to the left and to the right of the frame with transparent.
		auto fillLeft = (_innerLeft - outerLeft) * sizeof(uint32);
		auto fillRight = (outerRight - _innerRight) * sizeof(uint32);
		if (fillLeft || fillRight) {
			auto fillInts = _frameInts + _innerTop * _frameIntsPerLine;
			for (auto y = _innerTop; y != _innerBottom; ++y) {
				memset(fillInts + outerLeft, 0, fillLeft);
				memset(fillInts + _innerRight, 0, fillRight);
				fillInts += _frameIntsPerLine;
			}
		}

		// Fill below the frame bottom with transparent.
		auto fillBottomInts = (_frameInts + _innerBottom * _frameIntsPerLine + outerLeft);
		for (auto fillBottom = outerBottom - _innerBottom; fillBottom != 0; --fillBottom) {
			memset(fillBottomInts, 0, fillWidth);
			fillBottomInts += _frameIntsPerLine;
		}
	}
	if (_shadow.valid()) {
		paintShadow(outerLeft, outerTop, outerRight, outerBottom);
	}

	// Debug
	//auto frameInts = _frameInts;
	//auto pattern = anim::shifted((static_cast<uint32>(0xFF) << 24) | (static_cast<uint32>(0xFF) << 16) | (static_cast<uint32>(0xFF) << 8) | static_cast<uint32>(0xFF));
	//for (auto y = 0; y != _finalHeight; ++y) {
	//	for (auto x = 0; x != _finalWidth; ++x) {
	//		auto source = *frameInts;
	//		auto sourceAlpha = (source >> 24);
	//		*frameInts = anim::unshifted(anim::shifted(source) * 256 + pattern * (256 - sourceAlpha));
	//		++frameInts;
	//	}
	//	frameInts += _frameIntsPerLineAdded;
	//}

	p.drawImage(
		outerLeft / style::DevicePixelRatio(),
		outerTop / style::DevicePixelRatio(),
		_frame,
		outerLeft,
		outerTop,
		outerRight - outerLeft,
		outerBottom - outerTop);
}

TabbedSelector::Tab::Tab(
	SelectorTab type,
	int index,
	object_ptr<Inner> widget)
: _type(type)
, _index(index)
, _widget(std::move(widget))
, _weak(_widget)
, _footer(_widget ? _widget->createFooter() : nullptr) {
	if (_footer) {
		_footer->setParent(_widget->parentWidget());
	}
}

object_ptr<TabbedSelector::Inner> TabbedSelector::Tab::takeWidget() {
	return std::move(_widget);
}

void TabbedSelector::Tab::returnWidget(object_ptr<Inner> widget) {
	Expects(widget == _weak);

	_widget = std::move(widget);
}

void TabbedSelector::Tab::saveScrollTop() {
	Expects(widget() != nullptr);

	_scrollTop = widget()->getVisibleTop();
}

[[nodiscard]] rpl::producer<std::vector<Ui::EmojiGroup>> GreetingGroupFirst(
		not_null<Data::Session*> owner) {
	return owner->emojiStatuses().stickerGroupsValue(
	) | rpl::map([](std::vector<Ui::EmojiGroup> &&groups) {
		const auto i = ranges::find(
			groups,
			Ui::EmojiGroupType::Greeting,
			&Ui::EmojiGroup::type);
		if (i != begin(groups) && i != end(groups)) {
			ranges::rotate(begin(groups), i, i + 1);
		}
		return std::move(groups);
	});
}

std::unique_ptr<Ui::TabbedSearch> MakeSearch(
		not_null<TabbedSelector::Inner*> parent,
		const style::EmojiPan &st,
		Fn<void(std::vector<QString>&&)> callback,
		not_null<Main::Session*> session,
		TabbedSearchType type) {
	using Descriptor = Ui::SearchDescriptor;
	const auto owner = &session->data();
	auto result = std::make_unique<Ui::TabbedSearch>(parent, st, Descriptor{
		.st = st.search,
		.groups = ((type == TabbedSearchType::ProfilePhoto)
			? owner->emojiStatuses().profilePhotoGroupsValue()
			: (type == TabbedSearchType::Status)
			? owner->emojiStatuses().statusGroupsValue()
			: (type == TabbedSearchType::Stickers)
			? owner->emojiStatuses().stickerGroupsValue()
			: (type == TabbedSearchType::Greeting)
			? GreetingGroupFirst(owner)
			: owner->emojiStatuses().emojiGroupsValue()),
		.customEmojiFactory = owner->customEmojiManager().factory(
			Data::CustomEmojiManager::SizeTag::SetIcon,
			Ui::SearchWithGroups::IconSizeOverride()),
		.allowAutoFocus = [weak = base::make_weak(parent)] {
			return weak && weak->allowSearchAutoFocus();
		},
	});

	result->queryValue(
	) | rpl::skip(1) | rpl::on_next(
		std::move(callback),
		parent->lifetime());

	return result;
}

TabbedSelector::TabbedSelector(
	QWidget *parent,
	std::shared_ptr<Show> show,
	PauseReason level,
	Mode mode)
: TabbedSelector(parent, {
	.show = std::move(show),
	.st = ((mode == Mode::EmojiStatus
		|| mode == Mode::ChannelStatus
		|| mode == Mode::BackgroundEmoji
		|| mode == Mode::FullReactions)
		? st::statusEmojiPan
		: (mode == Mode::RecentReactions)
		? st::backgroundEmojiPan
		: st::defaultEmojiPan),
	.level = level,
	.mode = mode,
}) {
}

TabbedSelector::TabbedSelector(
	QWidget *parent,
	TabbedSelectorDescriptor &&descriptor)
: RpWidget(parent)
, _st(descriptor.st)
, _features(descriptor.features)
, _show(std::move(descriptor.show))
, _level(descriptor.level)
, _customTextColor(std::move(descriptor.customTextColor))
, _excludeStickerSetId(descriptor.excludeStickerSetId)
, _mode(descriptor.mode)
, _panelRounding(Ui::PrepareCornerPixmaps(st::emojiPanRadius, _st.bg))
, _categoriesRounding(
	Ui::PrepareCornerPixmaps(st::emojiPanRadius, _st.categoriesBg))
, _topShadow(full() ? object_ptr<Ui::PlainShadow>(this) : nullptr)
, _bottomShadow(this)
, _scroll(this, _st.scroll)
, _tabs([&] {
	std::vector<Tab> tabs;
	if (full()) {
		tabs.reserve(3);
		tabs.push_back(createTab(SelectorTab::Emoji, 0));
		tabs.push_back(createTab(SelectorTab::Stickers, 1));
		tabs.push_back(createTab(SelectorTab::Gifs, 2));
	} else if (mediaEditor()) {
		tabs.reserve(2);
		tabs.push_back(createTab(SelectorTab::Stickers, 0));
		tabs.push_back(createTab(SelectorTab::Masks, 1));
	} else if (_mode == Mode::StickersOnly || _mode == Mode::ChatIntro) {
		tabs.reserve(1);
		tabs.push_back(createTab(SelectorTab::Stickers, 0));
	} else if (_mode == Mode::StickersAndGifs) {
		tabs.reserve(2);
		tabs.push_back(createTab(SelectorTab::Stickers, 0));
		tabs.push_back(createTab(SelectorTab::Gifs, 1));
	} else if (_mode == Mode::CustomEmojiAndGifs) {
		tabs.reserve(2);
		tabs.push_back(createTab(SelectorTab::Emoji, 0));
		tabs.push_back(createTab(SelectorTab::Gifs, 1));
	} else {
		tabs.reserve(1);
		tabs.push_back(createTab(SelectorTab::Emoji, 0));
	}
	return tabs;
}())
, _currentTabType(full()
	? session().settings().selectorTab()
	: (mediaEditor()
		|| _mode == Mode::StickersOnly
		|| _mode == Mode::StickersAndGifs
		|| _mode == Mode::ChatIntro)
	? SelectorTab::Stickers
	: SelectorTab::Emoji)
, _hasEmojiTab(ranges::contains(_tabs, SelectorTab::Emoji, &Tab::type))
, _hasStickersTab(ranges::contains(_tabs, SelectorTab::Stickers, &Tab::type))
, _hasGifsTab(ranges::contains(_tabs, SelectorTab::Gifs, &Tab::type))
, _hasMasksTab(ranges::contains(_tabs, SelectorTab::Masks, &Tab::type))
, _tabbed(_tabs.size() > 1) {
	resize(st::emojiPanWidth, st::emojiPanMaxHeight);

	for (auto &tab : _tabs) {
		if (tab.hasFooter()) {
			tab.footer()->hide();
		} else {
			_noFooter = true;
		}
		tab.widget()->hide();
	}
	if (tabbed()) {
		createTabsSlider();
	}
	setWidgetToScrollArea();

	for (auto &tab : _tabs) {
		const auto widget = tab.widget();

		widget->scrollToRequests(
		) | rpl::on_next([=, tab = &tab](int y) {
			if (tab == currentTab()) {
				scrollToY(y);
			} else {
				tab->saveScrollTop(y);
			}
		}, widget->lifetime());

		widget->disableScrollRequests(
		) | rpl::on_next([=, tab = &tab](bool disabled) {
			if (tab == currentTab()) {
				_scroll->disableScroll(disabled);
			}
		}, widget->lifetime());
	}

	rpl::merge(
		(hasStickersTab()
			? stickers()->scrollUpdated() | rpl::map_to(0)
			: rpl::never<int>() | rpl::type_erased),
		_scroll->scrollTopChanges()
	) | rpl::on_next([=] {
		handleScroll();
	}, lifetime());

	if (_topShadow) {
		_topShadow->raise();
	}
	_bottomShadow->raise();
	if (_tabsSlider) {
		_tabsSlider->raise();
	}

	if (hasStickersTab()
		|| hasGifsTab()
		|| (hasEmojiTab() && _mode == Mode::Full)) {
		session().changes().peerUpdates(
			Data::PeerUpdate::Flag::Rights
		) | rpl::filter([=](const Data::PeerUpdate &update) {
			return (update.peer.get() == _currentPeer);
		}) | rpl::on_next([=] {
			checkRestrictedPeer();
		}, lifetime());
	}

	if (hasStickersTab()) {
		session().data().stickers().stickerSetInstalled(
		) | rpl::on_next([=](uint64 setId) {
			if (_tabsSlider) {
				_tabsSlider->setActiveSection(
					indexByType(SelectorTab::Stickers));
			}
			stickers()->showStickerSet(setId);
			if (_currentPeer
				&& Data::CanSend(
					_currentPeer,
					ChatRestriction::SendStickers)) {
				_showRequests.fire({});
			}
		}, lifetime());

		rpl::merge(
			session().premiumPossibleValue() | rpl::to_empty,
			session().data().stickers().updated(hasMasksTab()
				? Data::StickersType::Masks
				: Data::StickersType::Stickers)
		) | rpl::on_next([=] {
			refreshStickers();
		}, lifetime());
	}

	style::PaletteChanged(
	) | rpl::on_next([=] {
		refreshRounding();
	}, lifetime());

	if (hasEmojiTab() && _mode == Mode::Full) {
		session().data().stickers().emojiSetInstalled(
		) | rpl::on_next([=](uint64 setId) {
			_tabsSlider->setActiveSection(indexByType(SelectorTab::Emoji));
			emoji()->showSet(setId);
			if (_currentPeer && Data::CanSendTexts(_currentPeer)) {
				_showRequests.fire({});
			}
		}, lifetime());
	}
	if (hasEmojiTab()) {
		emoji()->refreshEmoji();
		setSearchRightReserved(descriptor.searchRightReserved);
	}
	setAttribute(Qt::WA_OpaquePaintEvent, false);
	showAll();
	hide();
}

TabbedSelector::~TabbedSelector() = default;

void TabbedSelector::reinstallSwipe(not_null<Inner*> widget) {
	_swipeLifetime.destroy();

	auto update = [=](Ui::Controls::SwipeContextData data) {
		if (data.translation != 0) {
			if (!_swipeBackData.callback) {
				_swipeBackData = Ui::Controls::SetupSwipeBack(
					this,
					[=]() -> std::pair<QColor, QColor> {
						return {
							st::historyForwardChooseBg->c,
							st::historyForwardChooseFg->c,
						};
					},
					data.translation < 0);
			}
			_swipeBackData.callback(data);
			return;
		} else if (_swipeBackData.lifetime) {
			_swipeBackData = {};
		}
	};

	auto init = [=](Ui::Controls::SwipeHandlerInitData data) {
		if (!_tabsSlider) {
			return Ui::Controls::SwipeHandlerFinishData();
		}
		const auto horizontalDelta = (data.direction == Qt::LeftToRight)
			? 1
			: -1;
		if (widget->canConsumeHorizontalScroll(
				data.cursorPosition,
				horizontalDelta)) {
			return Ui::Controls::SwipeHandlerFinishData();
		}
		const auto activeSection = _tabsSlider->activeSection();
		const auto isToLeft = data.direction == Qt::RightToLeft;
		if ((isToLeft && activeSection > 0)
			|| (!isToLeft && activeSection < _tabsSlider->sectionsCount() - 1)) {
			return Ui::Controls::DefaultSwipeBackHandlerFinishData([=] {
				if (_tabsSlider
					&& _tabsSlider->activeSection() == activeSection) {
					_swipeBackData = {};
					const auto next = activeSection + (isToLeft ? -1 : 1);
					if (next < 0 || next >= _tabsSlider->sectionsCount()) {
						return;
					}
					_tabsSlider->setActiveSection(next);
				}
			});
		}
		return Ui::Controls::SwipeHandlerFinishData();
	};

	Ui::Controls::SetupSwipeHandler({
		.widget = widget,
		.scroll = _scroll.data(),
		.update = std::move(update),
		.init = std::move(init),
		.dontStart = nullptr,
		.skipWheelEvent = [=](not_null<QWheelEvent*> event) {
			const auto delta = Ui::ScrollDelta(event);
			if (std::abs(delta.x()) <= std::abs(delta.y())) {
				return false;
			}
			return widget->canConsumeHorizontalScroll(
				widget->mapFromGlobal(event->globalPosition().toPoint()),
				delta.x());
		},
		.onLifetime = &_swipeLifetime,
	});
}

const style::EmojiPan &TabbedSelector::st() const {
	return _st;
}

Main::Session &TabbedSelector::session() const {
	return _show->session();
}

PauseReason TabbedSelector::level() const {
	return _level;
}

TabbedSelector::Tab TabbedSelector::createTab(SelectorTab type, int index) {
	auto createWidget = [&]() -> object_ptr<Inner> {
		const auto paused = [show = _show, level = _level] {
			return show->paused(level);
		};
		switch (type) {
		case SelectorTab::Emoji: {
			using EmojiMode = EmojiListWidget::Mode;
			using Descriptor = EmojiListDescriptor;
			return object_ptr<EmojiListWidget>(this, Descriptor{
				.show = _show,
				.mode = (_mode == Mode::EmojiStatus
					? EmojiMode::EmojiStatus
					: _mode == Mode::ChannelStatus
					? EmojiMode::ChannelStatus
					: _mode == Mode::BackgroundEmoji
					? EmojiMode::BackgroundEmoji
					: _mode == Mode::FullReactions
					? EmojiMode::FullReactions
					: _mode == Mode::RecentReactions
					? EmojiMode::RecentReactions
					: (_mode == Mode::CustomEmojiOnly
						|| _mode == Mode::CustomEmojiAndGifs)
					? EmojiMode::CustomOnly
					: _mode == Mode::PeerTitle
					? EmojiMode::PeerTitle
					: EmojiMode::Full),
				.customTextColor = _customTextColor,
				.paused = paused,
				.st = &_st,
				.features = _features,
			});
		}
		case SelectorTab::Stickers: {
			using StickersMode = StickersListWidget::Mode;
			using Descriptor = StickersListDescriptor;
			return object_ptr<StickersListWidget>(this, Descriptor{
				.show = _show,
				.mode = (_mode == Mode::ChatIntro
					? StickersMode::ChatIntro
					: StickersMode::Full),
				// A pick only sends in the full panel: elsewhere it adds a sticker
				// to a set or sets the chat intro, and must not ask about sending.
				.requireConfirmation = (_mode == Mode::Full),
				.paused = paused,
				.st = &_st,
				.features = _features,
				.excludeSetId = _excludeStickerSetId,
			});
		}
		case SelectorTab::Gifs: {
			using Descriptor = GifsListDescriptor;
			return object_ptr<GifsListWidget>(this, Descriptor{
				.show = _show,
				.requireConfirmation = (_mode == Mode::Full),
				.paused = paused,
				.st = &_st,
			});
		}
		case SelectorTab::Masks: {
			using StickersMode = StickersListWidget::Mode;
			using Descriptor = StickersListDescriptor;
			return object_ptr<StickersListWidget>(this, Descriptor{
				.show = _show,
				.mode = StickersMode::Masks,
				.paused = paused,
				.st = &_st,
				.features = _features,
			});
		}
		}
		Unexpected("Type in TabbedSelector::createTab.");
	};
	return Tab{ type, index, createWidget() };
}

bool TabbedSelector::full() const {
	return (_mode == Mode::Full);
}

bool TabbedSelector::mediaEditor() const {
	return (_mode == Mode::MediaEditor);
}

bool TabbedSelector::tabbed() const {
	return _tabbed;
}

bool TabbedSelector::hasEmojiTab() const {
	return _hasEmojiTab;
}

bool TabbedSelector::hasStickersTab() const {
	return _hasStickersTab;
}

bool TabbedSelector::hasGifsTab() const {
	return _hasGifsTab;
}

bool TabbedSelector::hasMasksTab() const {
	return _hasMasksTab;
}

rpl::producer<EmojiChosen> TabbedSelector::emojiChosen() const {
	return emoji()->chosen();
}

rpl::producer<FileChosen> TabbedSelector::customEmojiChosen() const {
	return emoji()->customChosen();
}

rpl::producer<FileChosen> TabbedSelector::fileChosen() const {
	auto never = rpl::never<FileChosen>(
	) | rpl::type_erased;
	return rpl::merge(
		hasStickersTab() ? stickers()->chosen() : never,
		hasGifsTab() ? gifs()->fileChosen() : never,
		hasMasksTab() ? masks()->chosen() : never);
}

rpl::producer<PhotoChosen> TabbedSelector::photoChosen() const {
	return hasGifsTab() ? gifs()->photoChosen() : nullptr;
}

auto TabbedSelector::inlineResultChosen() const
-> rpl::producer<InlineChosen> {
	return hasGifsTab() ? gifs()->inlineResultChosen() : nullptr;
}

rpl::producer<> TabbedSelector::photoRequests() const {
	return hasStickersTab() ? stickers()->photoRequests() : rpl::never<>();
}

rpl::producer<> TabbedSelector::audioRequests() const {
	return hasStickersTab() ? stickers()->audioRequests() : rpl::never<>();
}

rpl::producer<> TabbedSelector::linkRequests() const {
	return hasStickersTab() ? stickers()->linkRequests() : rpl::never<>();
}

auto TabbedSelector::choosingStickerUpdated() const
-> rpl::producer<TabbedSelector::Action>{
	return hasStickersTab()
		? stickers()->choosingUpdated()
		: rpl::never<Action>();
}

rpl::producer<> TabbedSelector::cancelled() const {
	return hasGifsTab() ? gifs()->cancelRequests() : nullptr;
}

rpl::producer<> TabbedSelector::checkForHide() const {
	auto never = rpl::never<>();
	return rpl::merge(
		hasStickersTab() ? stickers()->checkForHide() : never,
		hasMasksTab() ? masks()->checkForHide() : never,
		hasEmojiTab() ? emoji()->checkForHide() : never);
}

rpl::producer<> TabbedSelector::slideFinished() const {
	return _slideFinished.events();
}

void TabbedSelector::updateTabsSliderGeometry() {
	if (!_tabsSlider) {
		return;
	}
	const auto w = (mediaEditor() && hasMasksTab() && masks()->mySetsEmpty())
		? width() / 2
		: width();
	_tabsSlider->resizeToWidth(w);
	_tabsSlider->moveToLeft(0, 0);
}

void TabbedSelector::resizeEvent(QResizeEvent *e) {
	updateTabsSliderGeometry();
	if (_topShadow && _tabsSlider) {
		_topShadow->setGeometry(
			_tabsSlider->x(),
			_tabsSlider->bottomNoMargins() - st::lineWidth,
			_tabsSlider->width(),
			st::lineWidth);
	}
	updateScrollGeometry(e->oldSize());
	updateRestrictedLabelGeometry();
	updateFooterGeometry();
	update();
}

void TabbedSelector::contextMenuEvent(QContextMenuEvent *e) {
	e->accept();
}

void TabbedSelector::updateScrollGeometry(QSize oldSize) {
	auto scrollWidth = width() - st::emojiPanRadius;
	auto scrollHeight = height() - scrollTop() - scrollBottom();
	auto inner = currentTab()->widget();
	auto innerWidth = scrollWidth - _st.scroll.width;
	auto setScrollGeometry = [&] {
		_scroll->setGeometryToLeft(
			st::emojiPanRadius,
			scrollTop(),
			scrollWidth,
			scrollHeight);
	};
	auto setInnerGeometry = [&] {
		auto scrollTop = _scroll->scrollTop();
		auto scrollBottom = scrollTop + scrollHeight;
		inner->setMinimalHeight(innerWidth, scrollHeight);
		inner->setVisibleTopBottom(scrollTop, scrollBottom);
	};
	if (oldSize.height() > height()) {
		setScrollGeometry();
		setInnerGeometry();
	} else {
		setInnerGeometry();
		setScrollGeometry();
	}
	_bottomShadow->setGeometry(
		0,
		_scroll->y() + (_dropDown ? 0 : (_scroll->height() - st::lineWidth)),
		width(),
		st::lineWidth);
}

void TabbedSelector::updateFooterGeometry() {
	_footerTop = _dropDown
		? tabsSliderHeight()
		: _noFooter
		? (height() - _roundRadius)
		: (height() - _st.footer);
	for (auto &tab : _tabs) {
		if (tab.hasFooter()) {
			tab.footer()->resizeToWidth(width());
			tab.footer()->moveToLeft(0, _footerTop);
		}
	}
}

void TabbedSelector::updateRestrictedLabelGeometry() {
	if (!_restrictedLabel) {
		return;
	}

	auto labelWidth = width() - st::stickerPanPadding * 2;
	_restrictedLabel->resizeToWidth(labelWidth);
	_restrictedLabel->moveToLeft(
		(width() - _restrictedLabel->width()) / 2,
		(height() / 3 - _restrictedLabel->height() / 2));
}

bool TabbedSelector::eventHook(QEvent *e) {
	if (e->type() == QEvent::Hide || e->type() == QEvent::ParentChange) {
		++_slideGeneration;
		_slideFromTabs = false;
	}
	return RpWidget::eventHook(e);
}

void TabbedSelector::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);

	auto switching = (_slideAnimation != nullptr);
	if (switching) {
		paintSlideFrame(p);
		if (!_a_slide.animating()) {
			_slideAnimation.reset();
			const auto generation = _slideGeneration;
			InvokeQueued(this, [=] {
				if (_slideGeneration != generation
					|| _slideAnimation
					|| !isVisible()) {
					return;
				}
				const auto weak = base::make_weak(this);
				afterShown();
				if (weak
					&& _slideGeneration == generation
					&& !_slideAnimation
					&& isVisible()) {
					_slideFinished.fire({});
				}
			});
		}
	} else {
		paintContent(p);
	}
}

void TabbedSelector::refreshRounding() {
	const auto radius = _roundRadius ? _roundRadius : st::emojiPanRadius;
	_panelRounding = Ui::PrepareCornerPixmaps(radius, _st.bg);
	_categoriesRounding = Ui::PrepareCornerPixmaps(radius, _st.categoriesBg);
}

void TabbedSelector::paintSlideFrame(QPainter &p) {
	if (_roundRadius > 0) {
		paintBgRoundedPart(p);
	} else if (_tabsSlider) {
		p.fillRect(0, 0, width(), _tabsSlider->height(), _st.bg);
	}
	auto slideDt = _a_slide.value(1.);
	_slideAnimation->paintFrame(p, _st, slideDt, 1.);
}

void TabbedSelector::paintBgRoundedPart(QPainter &p) {
	const auto fill = _dropDown
		? QRect(0, height() - _roundRadius, width(), _roundRadius)
		: _tabsSlider
		? QRect(0, 0, width(), _tabsSlider->height())
		: QRect(0, 0, width(), _roundRadius);
	Ui::FillRoundRect(p, fill, _st.bg, {
		.p = {
			_dropDown ? QPixmap() : _panelRounding.p[0],
			_dropDown ? QPixmap() : _panelRounding.p[1],
			_dropDown ? _panelRounding.p[2] : QPixmap(),
			_dropDown ? _panelRounding.p[3] : QPixmap(),
		},
	});
	if (_dropDown && _tabsSlider) {
		const auto tabs = QRect(0, 0, width(), _tabsSlider->height());
		Ui::FillRoundRect(p, tabs, _st.bg, {
			.p = { _panelRounding.p[0], _panelRounding.p[1], {}, {} },
		});
	}
}

void TabbedSelector::paintContent(QPainter &p) {
	const auto &footerBg = hasSectionIcons() ? _st.categoriesBg : _st.bg;
	if (_roundRadius > 0) {
		paintBgRoundedPart(p);

		const auto &pixmaps = hasSectionIcons()
			? _categoriesRounding
			: _panelRounding;
		const auto footerPart = QRect(
			0,
			_footerTop,
			width(),
			_noFooter ? _roundRadius : _st.footer);
		const auto roundedTop = _dropDown && !_tabsSlider;
		Ui::FillRoundRect(p, footerPart, footerBg, {
			.p = {
				roundedTop ? pixmaps.p[0] : QPixmap(),
				roundedTop ? pixmaps.p[1] : QPixmap(),
				_dropDown ? QPixmap() : pixmaps.p[2],
				_dropDown ? QPixmap() : pixmaps.p[3],
			},
		});
	} else {
		if (_tabsSlider) {
			p.fillRect(0, 0, width(), _tabsSlider->height(), _st.bg);
		}
		p.fillRect(0, _footerTop, width(), _st.footer, footerBg);
	}

	auto sidesTop = marginTop();
	auto sidesHeight = height() - sidesTop - marginBottom();
	if (_restrictedLabel) {
		p.fillRect(0, sidesTop, width(), sidesHeight, st::emojiPanBg);
	} else {
		p.fillRect(
			myrtlrect(
				width() - _st.scroll.width,
				sidesTop,
				_st.scroll.width,
				sidesHeight),
			_st.bg);
		p.fillRect(
			myrtlrect(0, sidesTop, st::emojiPanRadius, sidesHeight),
			_st.bg);
	}
}

int TabbedSelector::tabsSliderHeight() const {
	return _tabsSlider ? (_tabsSlider->height() - st::lineWidth) : 0;
}

int TabbedSelector::marginTop() const {
	return (_dropDown && !_noFooter)
		? (tabsSliderHeight() + _st.footer)
		: _tabsSlider
		? tabsSliderHeight()
		: _roundRadius;
}

int TabbedSelector::scrollTop() const {
	return tabbed()
		? marginTop()
		: (_dropDown && !_noFooter)
		? _st.footer
		: 0;
}

int TabbedSelector::marginBottom() const {
	return (_dropDown || _noFooter) ? _roundRadius : _st.footer;
}

int TabbedSelector::scrollBottom() const {
	return (_dropDown || _noFooter) ? 0 : marginBottom();
}

void TabbedSelector::refreshStickers() {
	if (hasStickersTab()) {
		stickers()->refreshStickers();
		if (isHidden() || _currentTabType != SelectorTab::Stickers) {
			stickers()->preloadImages();
		}
	}
	if (hasMasksTab()) {
		const auto masksList = masks();
		masksList->refreshStickers();
		if (isHidden() || _currentTabType != SelectorTab::Masks) {
			masksList->preloadImages();
		}

		fillTabsSliderSections();
		updateTabsSliderGeometry();
		if (hasStickersTab() && masksList->mySetsEmpty()) {
			_tabsSlider->setActiveSection(indexByType(SelectorTab::Stickers));
		}
	}
}

bool TabbedSelector::preventAutoHide() const {
	return (hasStickersTab() && stickers()->preventAutoHide())
		|| (hasMasksTab() && masks()->preventAutoHide())
		|| (hasEmojiTab() && emoji()->preventAutoHide())
		|| hasMenu();
}

bool TabbedSelector::hasMenu() const {
	return (_menu && !_menu->empty());
}

QImage TabbedSelector::grabForAnimation() {
	auto slideAnimationData = base::take(_slideAnimation);
	auto slideAnimation = base::take(_a_slide);

	showAll();
	if (_topShadow) {
		_topShadow->hide();
	}
	if (_tabsSlider) {
		_tabsSlider->hide();
	}
	Ui::SendPendingMoveResizeEvents(this);

	auto result = QImage(
		size() * style::DevicePixelRatio(),
		QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(style::DevicePixelRatio());
	result.fill(Qt::transparent);
	render(&result);

	_a_slide = base::take(slideAnimation);
	_slideAnimation = base::take(slideAnimationData);

	return result;
}

bool TabbedSelector::floatPlayerHandleWheelEvent(QEvent *e) {
	return _scroll->viewportEvent(e);
}

QRect TabbedSelector::floatPlayerAvailableRect() const {
	return mapToGlobal(_scroll->geometry());
}

void TabbedSelector::hideFinished() {
	++_slideGeneration;
	_slideFromTabs = false;
	for (auto &tab : _tabs) {
		tab.widget()->panelHideFinished();
	}
	_a_slide.stop();
	_slideAnimation.reset();
}

void TabbedSelector::showStarted() {
	++_slideGeneration;
	_slideFromTabs = false;
	if (hasStickersTab()) {
		session().api().updateStickers();
	}
	if (hasMasksTab()) {
		session().api().updateMasks();
	}
	if (hasEmojiTab()) {
		session().api().updateCustomEmoji();
	}
	if (hasGifsTab()) {
		session().api().updateSavedGifs();
	}
	currentTab()->widget()->refreshRecent();
	currentTab()->widget()->preloadImages();
	_a_slide.stop();
	_slideAnimation.reset();
	showAll();
}

void TabbedSelector::beforeHiding() {
	++_slideGeneration;
	_slideFromTabs = false;
	const auto weak = base::make_weak(this);
	const auto generation = _slideGeneration;
	if (!_scroll->isHidden()) {
		currentTab()->widget()->beforeHiding();
		if (!weak || _slideGeneration != generation) {
			return;
		}
	}
	if (_beforeHidingCallback) {
		_beforeHidingCallback(_currentTabType);
		if (!weak || _slideGeneration != generation) {
			return;
		}
	}
	if (Ui::InFocusChain(this)) {
		window()->setFocus();
	}
}

void TabbedSelector::afterShown() {
	if (!_a_slide.animating()) {
		const auto generation = _slideGeneration;
		const auto weak = base::make_weak(this);
		showAll();
		if (!weak || _slideGeneration != generation) {
			return;
		}
		currentTab()->widget()->afterShownWithSearchFocus(!_slideFromTabs);
		if (weak && _slideGeneration == generation && _afterShownCallback) {
			_afterShownCallback(_currentTabType);
		}
	}
}

void TabbedSelector::setCurrentPeer(PeerData *peer) {
	if (hasGifsTab()) {
		gifs()->setInlineQueryPeer(peer);
	}
	_currentPeer = peer;
	checkRestrictedPeer();
	if (hasEmojiTab()) {
		emoji()->showMegagroupSet(peer ? peer->asMegagroup() : nullptr);
	}
	if (hasStickersTab()) {
		stickers()->showMegagroupSet(peer ? peer->asMegagroup() : nullptr);
	}
	setAllowEmojiWithoutPremium(
		peer && Data::AllowEmojiWithoutPremium(peer));
}

void TabbedSelector::provideRecentEmoji(
		const std::vector<EmojiStatusId> &customRecentList) {
	for (const auto &tab : _tabs) {
		if (tab.type() == SelectorTab::Emoji) {
			const auto emoji = static_cast<EmojiListWidget*>(tab.widget());
			emoji->provideRecent(customRecentList);
		}
	}
}

void TabbedSelector::setMarkedCustomIds(
		const base::flat_set<DocumentId> &ids) {
	for (const auto &tab : _tabs) {
		if (tab.type() == SelectorTab::Emoji) {
			const auto emoji = static_cast<EmojiListWidget*>(tab.widget());
			emoji->setMarkedCustomIds(ids);
		}
	}
}

void TabbedSelector::setMarkedCustomPainter(
		Fn<void(QPainter&, QRect, DocumentId)> painter) {
	for (const auto &tab : _tabs) {
		if (tab.type() == SelectorTab::Emoji) {
			const auto emoji = static_cast<EmojiListWidget*>(tab.widget());
			emoji->setMarkedCustomPainter(painter);
		}
	}
}

void TabbedSelector::checkRestrictedPeer() {
	if (_currentPeer) {
		const auto error = (_currentTabType == SelectorTab::Stickers)
			? Data::RestrictionError(
				_currentPeer,
				ChatRestriction::SendStickers)
			: (_currentTabType == SelectorTab::Gifs)
			? Data::RestrictionError(
				_currentPeer,
				ChatRestriction::SendGifs)
			: (_currentTabType == SelectorTab::Emoji && _mode == Mode::Full)
			? ((true || Data::RestrictionError(
				_currentPeer, // We don't allow input if texts are forbidden.
				ChatRestriction::SendInline))
				? Data::RestrictionError(
					_currentPeer,
					ChatRestriction::SendOther)
				: Data::SendError())
			: Data::SendError();
		const auto changed = (_restrictedLabelKey != error.text)
			|| (_restrictedPeer != _currentPeer)
			|| (_restrictedBoostsToLift != error.boostsToLift)
			|| (bool(_restrictedLabel) != bool(error));
		if (!changed) {
			return;
		}
		_restrictedLabelKey = error.text;
		_restrictedPeer = _currentPeer;
		_restrictedBoostsToLift = error.boostsToLift;
		if (error) {
			const auto show = _show;
			const auto peer = _currentPeer;
			_restrictedLabel.create(
				this,
				rpl::single(error.boostsToLift
					? tr::link(error.text)
					: TextWithEntities{ error.text }),
				st::stickersRestrictedLabel);
			const auto lifting = error.boostsToLift;
			_restrictedLabel->setClickHandlerFilter([=](auto...) {
				const auto window = show->resolveWindow();
				window->resolveBoostState(peer->asChannel(), lifting);
				return false;
			});
			_restrictedLabel->show();
			updateRestrictedLabelGeometry();
			currentTab()->footer()->hide();
			_scroll->hide();
			_bottomShadow->hide();
			update();
			return;
		}
	} else {
		_restrictedLabelKey = QString();
		_restrictedPeer = nullptr;
		_restrictedBoostsToLift = 0;
	}
	if (_restrictedLabel) {
		_restrictedLabel.destroy();
		if (!_a_slide.animating()) {
			currentTab()->footer()->show();
			_scroll->show();
			_bottomShadow->setVisible(_mode == Mode::EmojiStatus);
			update();
		}
	}
}

bool TabbedSelector::isRestrictedView() {
	checkRestrictedPeer();
	return (_restrictedLabel != nullptr);
}

void TabbedSelector::showAll() {
	if (isRestrictedView()) {
		_restrictedLabel->show();
	} else {
		if (currentTab()->hasFooter()) {
			currentTab()->footer()->show();
		}
		_scroll->show();
		_bottomShadow->setVisible(_mode == Mode::EmojiStatus);
	}
	if (_topShadow) {
		_topShadow->show();
	}
	if (_tabsSlider) {
		_tabsSlider->show();
	}
}

void TabbedSelector::hideForSliding() {
	hideChildren();
	if (_topShadow) {
		_topShadow->show();
	}
	if (_tabsSlider) {
		_tabsSlider->show();
	}
	currentTab()->widget()->clearSelection();
}

void TabbedSelector::handleScroll() {
	auto scrollTop = _scroll->scrollTop();
	auto scrollBottom = scrollTop + _scroll->height();
	currentTab()->widget()->setVisibleTopBottom(scrollTop, scrollBottom);
}

void TabbedSelector::setRoundRadius(int radius) {
	_roundRadius = radius;
	refreshRounding();
	if (_tabsSlider) {
		_tabsSlider->setRippleTopRoundRadius(_roundRadius);
	}
}

void TabbedSelector::setAllowEmojiWithoutPremium(bool allow) {
	for (const auto &tab : _tabs) {
		if (tab.type() == SelectorTab::Emoji) {
			const auto emoji = static_cast<EmojiListWidget*>(tab.widget());
			emoji->setAllowWithoutPremium(allow);
		}
	}
}

void TabbedSelector::createTabsSlider() {
	_tabsSlider.create(this, _st.tabs);

	fillTabsSliderSections();

	_tabsSlider->setActiveSectionFast(indexByType(_currentTabType));
	_tabsSlider->sectionActivated(
	) | rpl::on_next([=] {
		switchTab();
	}, lifetime());
}

void TabbedSelector::fillTabsSliderSections() {
	if (!_tabsSlider) {
		return;
	}

	const auto types = ranges::views::all(
		_tabs
	) | ranges::views::filter([&](const Tab &tab) {
		return (tab.type() == SelectorTab::Masks)
			? !masks()->mySetsEmpty()
			: true;
	}) | ranges::views::transform(&Tab::type) | ranges::to_vector;
	const auto sections = ranges::views::all(
		types
	) | ranges::views::transform([&](SelectorTab type) {
		return [&] {
			switch (type) {
			case SelectorTab::Emoji:
				return tr::lng_switch_emoji;
			case SelectorTab::Stickers:
				return tr::lng_switch_stickers;
			case SelectorTab::Gifs:
				return tr::lng_switch_gifs;
			case SelectorTab::Masks:
				return tr::lng_switch_masks;
			}
			Unexpected("SelectorTab value in fillTabsSliderSections.");
		}()(tr::now);
	}) | ranges::to_vector;
	_tabsSlider->setTabs(types, sections);
}

bool TabbedSelector::hasSectionIcons() const {
	return !_restrictedLabel && !_noFooter;
}

void TabbedSelector::switchTab() {
	Expects(tabbed());

	const auto tab = _tabsSlider->activeSection();
	Assert(tab >= 0 && tab < _tabs.size());
	const auto newTabType = typeByIndex(tab);
	if (_currentTabType == newTabType) {
		_scroll->scrollToY(0);
		return;
	}

	const auto weak = base::make_weak(this);
	const auto generation = _slideGeneration + 1;
	const auto fromTabs = _tabsSlider->hasFocus();
	const auto parked = QPointer<QWidget>(window());
	auto restoreTabs = fromTabs;
	auto parking = true;
	const auto focusConnection = fromTabs
		? QObject::connect(qApp, &QApplication::focusChanged, this,
			[&](QWidget *from, QWidget *to) {
				if (!parking || from != _tabsSlider.data() || to != parked.data()) {
					restoreTabs = false;
				}
			})
		: QMetaObject::Connection();
	const auto disconnect = gsl::finally([&] {
		QObject::disconnect(focusConnection);
	});
	if (fromTabs) {
		parked->setFocus();
		parking = false;
		if (!weak || _slideGeneration + 1 != generation) {
			return;
		}
	}

	const auto wasSectionIcons = hasSectionIcons() && !_dropDown;
	const auto wasIndex = indexByType(_currentTabType);
	currentTab()->saveScrollTop();

	beforeHiding();
	if (!weak || _slideGeneration != generation) {
		return;
	}
	_slideFromTabs = fromTabs;

	auto wasCache = grabForAnimation();
	if (!weak || _slideGeneration != generation) {
		return;
	}

	auto widget = _scroll->takeWidget<Inner>();
	widget->setParent(this);
	widget->hide();
	if (currentTab()->hasFooter()) {
		currentTab()->footer()->hide();
	}
	currentTab()->returnWidget(std::move(widget));

	_currentTabType = newTabType;
	_tabsSlider->notifySelectionChanged(wasIndex);
	_restrictedLabel.destroy();
	checkRestrictedPeer();

	currentTab()->widget()->refreshRecent();
	currentTab()->widget()->preloadImages();
	setWidgetToScrollArea();

	auto nowCache = grabForAnimation();
	if (!weak || _slideGeneration != generation) {
		return;
	}

	auto direction = (wasIndex > indexByType(_currentTabType))
		? SlideAnimation::Direction::LeftToRight
		: SlideAnimation::Direction::RightToLeft;
	if (direction == SlideAnimation::Direction::LeftToRight) {
		std::swap(wasCache, nowCache);
	}
	_slideAnimation = std::make_unique<SlideAnimation>();
	const auto slidingTop = _dropDown ? _footerTop : _scroll->y();
	const auto slidingRect = QRect(
		0,
		slidingTop * style::DevicePixelRatio(),
		width() * style::DevicePixelRatio(),
		(height() - slidingTop) * style::DevicePixelRatio());
	_slideAnimation->setFinalImages(
		direction,
		std::move(wasCache),
		std::move(nowCache),
		slidingRect,
		wasSectionIcons);
	_slideAnimation->setCornerMasks(
		Images::CornersMask(st::emojiPanRadius));
	_slideAnimation->start();

	hideForSliding();
	if (!weak || _slideGeneration != generation) {
		return;
	}

	getTab(wasIndex)->widget()->hideFinished();
	if (!weak || _slideGeneration != generation) {
		return;
	}
	QObject::disconnect(focusConnection);
	if (restoreTabs && parked && QApplication::focusWidget() == parked
		&& _tabsSlider->isVisible() && _tabsSlider->isEnabled()) {
		_tabsSlider->setFocus(Qt::OtherFocusReason);
		if (!weak || _slideGeneration != generation) {
			return;
		}
	}

	_a_slide.start(
		[=] { update(); },
		0.,
		1.,
		st::emojiPanSlideDuration,
		anim::linear);
	update();

	if (full()) {
		session().settings().setSelectorTab(_currentTabType);
		session().saveSettingsDelayed();
	}
}

void TabbedSelector::setSearchRightReserved(int value) {
	emoji()->setSearchRightReserved(value);
}

void TabbedSelector::refreshSearchColors() {
	if (hasEmojiTab()) {
		emoji()->refreshSearchColors();
	}
}

not_null<EmojiListWidget*> TabbedSelector::emoji() const {
	Expects(hasEmojiTab());

	return static_cast<EmojiListWidget*>(
		getTab(indexByType(SelectorTab::Emoji))->widget());
}

not_null<StickersListWidget*> TabbedSelector::stickers() const {
	Expects(hasStickersTab());

	return static_cast<StickersListWidget*>(
		getTab(indexByType(SelectorTab::Stickers))->widget());
}

not_null<GifsListWidget*> TabbedSelector::gifs() const {
	Expects(hasGifsTab());

	return static_cast<GifsListWidget*>(
		getTab(indexByType(SelectorTab::Gifs))->widget());
}

not_null<StickersListWidget*> TabbedSelector::masks() const {
	Expects(hasMasksTab());

	return static_cast<StickersListWidget*>(
		getTab(indexByType(SelectorTab::Masks))->widget());
}

void TabbedSelector::setWidgetToScrollArea() {
	auto inner = _scroll->setOwnedWidget(currentTab()->takeWidget());
	auto innerWidth = _scroll->width() - _st.scroll.width;
	auto scrollHeight = _scroll->height();
	inner->setMinimalHeight(innerWidth, scrollHeight);
	inner->moveToLeft(0, 0);
	inner->show();

	if (_tabs.size() > 1) {
		reinstallSwipe(inner);
	}

	_scroll->disableScroll(false);
	scrollToY(currentTab()->getScrollTop());
	handleScroll();
}

void TabbedSelector::scrollToY(int y) {
	_scroll->scrollToY(y);

	// Qt render glitch workaround, shadow sometimes disappears if we just scroll to y.
	if (_topShadow) {
		_topShadow->update();
	}
}

void TabbedSelector::showMenuWithDetails(SendMenu::Details details) {
	_menu = currentTab()->widget()->fillContextMenu(details);
	if (_menu && !_menu->empty()) {
		_menu->popup(QCursor::pos());
	}
}

void TabbedSelector::setDropDown(bool dropDown) {
	if (_dropDown == dropDown) {
		return;
	}
	_dropDown = dropDown;
	updateFooterGeometry();
	updateScrollGeometry(size());
}

rpl::producer<> TabbedSelector::contextMenuRequested() const {
	return events(
	) | rpl::filter([=](not_null<QEvent*> e) {
		return e->type() == QEvent::ContextMenu;
	}) | rpl::to_empty;
}

SelectorTab TabbedSelector::typeByIndex(int index) const {
	for (const auto &tab : _tabs) {
		if (tab.index() == index) {
			return tab.type();
		}
	}
	Unexpected("Type in TabbedSelector::typeByIndex.");
}

int TabbedSelector::indexByType(SelectorTab type) const {
	for (const auto &tab : _tabs) {
		if (tab.type() == type) {
			return tab.index();
		}
	}
	Unexpected("Index in TabbedSelector::indexByType.");
}

not_null<TabbedSelector::Tab*> TabbedSelector::getTab(int index) {
	return &(_tabs[index]);
}

not_null<const TabbedSelector::Tab*> TabbedSelector::getTab(int index) const {
	return &_tabs[index];
}

not_null<TabbedSelector::Tab*> TabbedSelector::currentTab() {
	return &_tabs[indexByType(_currentTabType)];
}

not_null<const TabbedSelector::Tab*> TabbedSelector::currentTab() const {
	return &_tabs[indexByType(_currentTabType)];
}

TabbedSelector::Inner::Inner(
	QWidget *parent,
	std::shared_ptr<Show> show,
	PauseReason level)
: Inner(
	parent,
	st::defaultEmojiPan,
	show,
	[show, level] { return show->paused(level); }) {
}

TabbedSelector::Inner::Inner(
	QWidget *parent,
	const style::EmojiPan &st,
	std::shared_ptr<Show> show,
	Fn<bool()> paused)
: RpWidget(parent)
, _st(st)
, _show(std::move(show))
, _session(&_show->session())
, _paused(paused) {
}

rpl::producer<int> TabbedSelector::Inner::scrollToRequests() const {
	return _scrollToRequests.events();
}

rpl::producer<bool> TabbedSelector::Inner::disableScrollRequests() const {
	return _disableScrollRequests.events();
}

void TabbedSelector::Inner::scrollTo(int y) {
	_scrollToRequests.fire_copy(y);
}

void TabbedSelector::Inner::disableScroll(bool disabled) {
	_disableScrollRequests.fire_copy(disabled);
}

void TabbedSelector::Inner::showBoxPreventHide(
		object_ptr<Ui::BoxContent> box) {
	const auto weak = base::make_weak(box.data());
	_show->showBox(std::move(box));
	preventHideWithBox(weak);
}

void TabbedSelector::Inner::preventHideWithBox(
		base::weak_qptr<Ui::BoxContent> weak) {
	if (const auto strong = weak.get()) {
		_preventHideWithBox = true;
		connect(strong, &QObject::destroyed, this, [=] {
			_preventHideWithBox = false;
			_checkForHide.fire({});
		});
	}
}

void TabbedSelector::Inner::paintEmptySearchResults(
		Painter &p,
		const style::icon &icon,
		const QString &text,
		bool skipIcon) const {
	const auto iconLeft = (width() - icon.width()) / 2;
	const auto iconTop = std::max(
		(height() / 3) - (icon.height() / 2),
		st::normalFont->height);
	if (!skipIcon) {
		icon.paint(p, iconLeft, iconTop, width());
	}

	const auto textWidth = st::normalFont->width(text);
	const auto textTop = std::min(
		iconTop + icon.height() - st::normalFont->height,
		height() - 2 * st::normalFont->height);
	p.setFont(st::normalFont);
	p.setPen(_st.tabs.labelFg);
	p.drawTextLeft(
		(width() - textWidth) / 2,
		textTop,
		width(),
		text,
		textWidth);
}

void TabbedSelector::Inner::visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) {
	_visibleTop = visibleTop;
	_visibleBottom = visibleBottom;
}

void TabbedSelector::Inner::setMinimalHeight(
		int newWidth,
		int newMinimalHeight) {
	if (_minimalHeight != newMinimalHeight) {
		_minimalHeight = newMinimalHeight;
		resizeToWidth(newWidth);
	} else if (newWidth != width()) {
		resizeToWidth(newWidth);
	}
}

int TabbedSelector::Inner::resizeGetHeight(int newWidth) {
	auto result = std::max(
		countDesiredHeight(newWidth),
		minimalHeight());
	if (result != height()) {
		update();
	}
	return result;
}

int TabbedSelector::Inner::minimalHeight() const {
	return _minimalHeight.value_or(defaultMinimalHeight());
}

int TabbedSelector::Inner::defaultMinimalHeight() const {
	return st::emojiPanMaxHeight - _st.footer;
}

void TabbedSelector::Inner::afterShownWithSearchFocus(bool allow) {
	const auto weak = base::make_weak(this);
	const auto previous = _allowSearchAutoFocus;
	_allowSearchAutoFocus = previous && allow;
	afterShown();
	if (weak) {
		_allowSearchAutoFocus = previous;
	}
}

void TabbedSelector::Inner::hideFinished() {
	processHideFinished();
	if (auto footer = getFooter()) {
		footer->processHideFinished();
	}
}

void TabbedSelector::Inner::panelHideFinished() {
	hideFinished();
	processPanelHideFinished();
	if (auto footer = getFooter()) {
		footer->processPanelHideFinished();
	}
}

TabbedSelector::InnerFooter::InnerFooter(
	QWidget *parent,
	const style::EmojiPan &st)
: RpWidget(parent)
, _st(st) {
	resize(st::emojiPanWidth, _st.footer);
}

const style::EmojiPan &TabbedSelector::InnerFooter::st() const {
	return _st;
}

} // namespace ChatHelpers
