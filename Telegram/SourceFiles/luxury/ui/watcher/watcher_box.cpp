// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "luxury/ui/watcher/watcher_box.h"

#include "base/unixtime.h"
#include "base/unique_qptr.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "lang_auto.h"
#include "luxury/data/messages_storage.h"
#include "luxury/luxury_settings.h"
#include "luxury/utils/telegram_helpers.h"
#include "main/main_session.h"
#include "ui/effects/animations.h"
#include "ui/effects/ripple_animation.h"
#include "ui/layers/generic_box.h"
#include "ui/style/style_core_color.h"
#include "ui/text/format_values.h"
#include "ui/text/text.h"
#include "ui/widgets/menu/menu.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "ui/widgets/menu/menu_add_action_callback_factory.h"
#include "ui/widgets/discrete_sliders.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/menu/menu.h"
#include "ui/widgets/popup_menu.h"
#include "ui/painter.h"
#include "ui/qt_object_factory.h"
#include "ui/vertical_list.h"
#include "window/window_session_controller.h"

#include "styles/style_basic.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_luxury_styles.h"
#include "styles/style_settings.h"
#include "styles/style_widgets.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <vector>

#include <QtGui/QPolygonF>

namespace LuxuryWatcher {

namespace {

constexpr auto kPi = 3.14159265358979323846;

// Fixed semantic accents. Literal color values are palette-only in this
// style system (a .style module cannot define them), and adding them to
// the shared lib_ui palette would bump the submodule for one box, so they
// live here as owned colors instead: like Telegram's media-viewer accents
// they are meant to stay the same in day and night themes.
const auto kOkFg = style::internal::OwnedColor(QColor(0x31, 0xc4, 0x8d));
const auto kGiftFg = style::internal::OwnedColor(QColor(0x5e, 0xb5, 0xf7));
const auto kNameFg = style::internal::OwnedColor(QColor(0x31, 0xc4, 0x8d));
const auto kUsernameFg = style::internal::OwnedColor(QColor(0xa7, 0x8b, 0xfa));
const auto kPhotoFg = style::internal::OwnedColor(QColor(0xf5, 0x9e, 0x0b));

// Timing (not geometry, so plain constants are fine here).
constexpr auto kCardAppearDuration = crl::time(150);
constexpr auto kCardStagger = crl::time(20);
constexpr auto kCardStaggerMax = crl::time(200);
constexpr auto kPulseDuration = crl::time(1600);
constexpr auto kSortDuration = crl::time(150);
constexpr auto kSpinDuration = crl::time(600);

constexpr auto kCardBgBlend = 0.04;
constexpr auto kCardBorderAlpha = 40;
constexpr auto kSegmentPillAlpha = 0.14;
constexpr auto kDurationPillAlpha = 0.12;
constexpr auto kChipAlpha = 0.18;
constexpr auto kPulseHaloAlpha = 0.5;
constexpr auto kDisabledLabelAlpha = 0.4;

enum class Tab {
	All,
	Sessions,
	Events,
};

enum class Order {
	Newest,
	Oldest,
	Longest,
};

enum class ToolIcon {
	Refresh,
	Gear,
};

constexpr auto kMaxOnlineHistoryRows = 50;
constexpr auto kMaxWatchEventRows = 30;
constexpr auto kHistoryReadLimit = 200;

[[nodiscard]] QColor Blended(
		const style::color &from,
		const style::color &toward,
		float64 alpha) {
	const auto &a = from->c;
	const auto &b = toward->c;
	return QColor(
		int(std::round(a.red() + (b.red() - a.red()) * alpha)),
		int(std::round(a.green() + (b.green() - a.green()) * alpha)),
		int(std::round(a.blue() + (b.blue() - a.blue()) * alpha)));
}

[[nodiscard]] QColor CardBackgroundColor() {
	// A card is a raised plate: in the dark theme the blend goes toward
	// the light text color, in the light theme toward the dark one, so the
	// same alpha reads as elevation in both.
	return Blended(st::windowBg, st::windowFg, kCardBgBlend);
}

void PaintCardShell(Painter &p, int width, int height) {
	auto border = st::windowShadowFg->c;
	border.setAlpha(kCardBorderAlpha);
	p.setPen(QPen(border));
	p.setBrush(CardBackgroundColor());
	p.drawRoundedRect(
		QRectF(0, 0, width, height),
		st::luxuryWatcherCardRadius,
		st::luxuryWatcherCardRadius);
}

void PaintClock(
		Painter &p,
		const QPointF &topLeft,
		float64 size,
		const QColor &color) {
	const auto pen = QPen(
		color,
		std::max(1., size / 7.),
		Qt::SolidLine,
		Qt::RoundCap,
		Qt::RoundJoin);
	p.setPen(pen);
	p.setBrush(Qt::NoBrush);
	const auto rect = QRectF(topLeft, QSizeF(size, size));
	p.drawEllipse(rect);
	const auto center = rect.center();
	p.drawLine(center, QPointF(center.x(), rect.top() + size * 0.22));
	p.drawLine(center, QPointF(center.x() + size * 0.29, center.y()));
}

void PaintRefreshGlyph(Painter &p, float64 base, const QColor &color) {
	// A ~300 degree arc with an arrowhead at its end: the standard
	// circular-arrow "refresh" mark, drawn instead of an icon asset.
	const auto radius = base;
	const auto endDegrees = 90. + 300.;
	const auto radians = endDegrees * kPi / 180.;
	const auto end = QPointF(
		radius * std::cos(radians),
		-radius * std::sin(radians));
	// Unit tangent along the counterclockwise sweep at the arc end.
	const auto tangent = QPointF(-std::sin(radians), -std::cos(radians));
	const auto normal = QPointF(-tangent.y(), tangent.x());
	const auto head = 0.42 * base;
	const auto half = 0.28 * base;
	p.drawArc(
		QRectF(-radius, -radius, 2 * radius, 2 * radius),
		90 * 16,
		300 * 16);
	const auto arrow = QPolygonF({
		end + tangent * head,
		end + normal * half,
		end - normal * half,
	});
	p.setPen(Qt::NoPen);
	p.setBrush(color);
	p.drawConvexPolygon(arrow);
}

void PaintGearGlyph(
		Painter &p,
		float64 base,
		const QColor &color,
		float64 penWidth) {
	// Ring, eight teeth and a center hole: a painted gear.
	const auto ring = 0.78 * base;
	const auto teethInner = 0.88 * base;
	const auto teethOuter = 1.18 * base;
	const auto hole = 0.30 * base;
	p.drawEllipse(QPointF(0, 0), ring, ring);
	for (auto i = 0; i != 8; ++i) {
		const auto radians = (i * 45.) * kPi / 180.;
		const auto c = std::cos(radians);
		const auto s = std::sin(radians);
		p.drawLine(
			QPointF(teethInner * c, teethInner * s),
			QPointF(teethOuter * c, teethOuter * s));
	}
	p.setPen(QPen(
		color,
		std::max(1., penWidth / 2.),
		Qt::SolidLine,
		Qt::RoundCap,
		Qt::RoundJoin));
	p.drawEllipse(QPointF(0, 0), hole, hole);
}

struct OnlineSession {
	std::optional<int> start;
	std::optional<int> end;
};

// Pairs presence transitions into stays online, oldest first. The loader
// reports newest first, so this walks the events back to front: an online
// followed by an offline is one stay. A repeated online only confirms the
// stay is ongoing -- the earliest one stays the start. An offline with no
// online in the loaded set began before tracking started or past the load
// window and keeps an empty start instead of an invented one; a trailing
// online with no offline yet is still open.
std::vector<OnlineSession> PairOnlineSessions(
		const std::vector<OnlineEvent> &events) {
	auto result = std::vector<OnlineSession>();
	result.reserve(events.size());
	auto start = std::optional<int>();
	for (auto i = events.rbegin(); i != events.rend(); ++i) {
		if (i->online) {
			if (!start) {
				start = i->at;
			}
		} else if (start) {
			result.push_back({ start, i->at });
			start = std::nullopt;
		} else {
			result.push_back({ std::nullopt, i->at });
		}
	}
	if (start) {
		result.push_back({ start, std::nullopt });
	}
	return result;
}

// One sentence per watched event: gifts name the giver and the stored
// label, profile edits name the old and new values. Peer names resolve
// when the box opens -- never stored, they change, which is the point. An
// unresolvable giver reads as anonymous, never as a blank.
[[nodiscard]] QString WatchEventText(
		const WatchEvent &event,
		not_null<PeerData*> peer) {
	switch (static_cast<WatchKind>(event.kind)) {
	case WatchKind::GiftSent:
		return tr::luxury_WatchGiftSent(
			tr::now,
			lt_cost,
			QString::fromStdString(event.title),
			lt_user,
			peer->name());
	case WatchKind::GiftReceived: {
		const auto cost = QString::fromStdString(event.title);
		const auto giver = event.otherPeerId
			? peer->session().data().peerLoaded(
				static_cast<PeerId>(event.otherPeerId))
			: nullptr;
		return giver
			? tr::luxury_WatchGiftReceived(
				tr::now,
				lt_user,
				giver->name(),
				lt_cost,
				cost)
			: tr::luxury_WatchGiftReceivedAnonymous(
				tr::now,
				lt_cost,
				cost);
	}
	case WatchKind::NameChanged:
		return tr::luxury_WatchNameChanged(tr::now)
			+ u": "_q
			+ QString::fromStdString(event.title)
			+ u" → "_q
			+ QString::fromStdString(event.extra);
	case WatchKind::UsernameChanged: {
		const auto oldName = QString::fromStdString(event.title);
		const auto newName = QString::fromStdString(event.extra);
		return tr::luxury_WatchUsernameChanged(tr::now)
			+ u": "_q
			+ (oldName.isEmpty() ? u"—"_q : u"@"_q + oldName)
			+ u" → "_q
			+ (newName.isEmpty() ? u"—"_q : u"@"_q + newName);
	}
	case WatchKind::PhotoUpdated:
		return tr::luxury_WatchPhotoUpdated(tr::now);
	default:
		return QString();
	}
}

[[nodiscard]] QString ChipLabelFor(int kind) {
	switch (static_cast<WatchKind>(kind)) {
	case WatchKind::GiftSent:
	case WatchKind::GiftReceived:
		return tr::luxury_OnlineHistoryKindGift(tr::now);
	case WatchKind::NameChanged:
		return tr::luxury_OnlineHistoryKindName(tr::now);
	case WatchKind::UsernameChanged:
		return tr::luxury_OnlineHistoryKindUsername(tr::now);
	case WatchKind::PhotoUpdated:
		return tr::luxury_OnlineHistoryKindPhoto(tr::now);
	default:
		return QString();
	}
}

[[nodiscard]] style::color ChipColorFor(int kind) {
	switch (static_cast<WatchKind>(kind)) {
	case WatchKind::GiftSent:
	case WatchKind::GiftReceived:
		return kGiftFg.color();
	case WatchKind::NameChanged:
		return kNameFg.color();
	case WatchKind::UsernameChanged:
		return kUsernameFg.color();
	case WatchKind::PhotoUpdated:
		return kPhotoFg.color();
	default:
		return st::windowSubTextFg;
	}
}

class ToolButton : public Ui::RpWidget {
public:
	ToolButton(QWidget *parent, ToolIcon icon);

	[[nodiscard]] rpl::producer<> clicks() const {
		return _clicks.events();
	}
	void spin();

protected:
	void paintEvent(QPaintEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void enterEventHook(QEnterEvent *e) override;
	void leaveEventHook(QEvent *e) override;

private:
	void paintIcon(Painter &p, const QColor &color);

	ToolIcon _icon = ToolIcon::Refresh;
	bool _hovered = false;
	bool _pressed = false;
	Ui::Animations::Simple _spin;
	rpl::event_stream<> _clicks;
	std::unique_ptr<Ui::RippleAnimation> _ripple;

};

class SortControl : public Ui::RpWidget {
public:
	SortControl(QWidget *parent);

	[[nodiscard]] rpl::producer<int> changes() const {
		return _changes.events();
	}
	[[nodiscard]] int contentWidth() const;
	void setOrder(int order);
	void setLongestEnabled(bool enabled);

protected:
	void paintEvent(QPaintEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void enterEventHook(QEnterEvent *e) override;
	void leaveEventHook(QEvent *e) override;

private:
	[[nodiscard]] int segmentLeft(int index) const;
	[[nodiscard]] int segmentWidth(int index) const;
	void select(int order);

	std::array<QString, 3> _labels;
	int _selected = 0;
	bool _longestEnabled = true;
	Ui::Animations::Simple _pillLeft;
	Ui::Animations::Simple _pillWidth;
	rpl::event_stream<int> _changes;

};

class WatcherToolbar : public Ui::RpWidget {
public:
	WatcherToolbar(QWidget *parent);

	[[nodiscard]] rpl::producer<int> sortChanges() const {
		return _sort->changes();
	}
	[[nodiscard]] rpl::producer<> refreshClicks() const {
		return _refresh->clicks();
	}
	[[nodiscard]] rpl::producer<> settingsClicks() const {
		return _settings->clicks();
	}
	[[nodiscard]] Ui::RpWidget *gearButton() const {
		return _settings;
	}
	void setSortOrder(int order);
	void setLongestEnabled(bool enabled);
	void startRefreshSpin();

protected:
	int resizeGetHeight(int newWidth) override;

private:
	SortControl *_sort = nullptr;
	ToolButton *_refresh = nullptr;
	ToolButton *_settings = nullptr;

};

class WatcherCard : public Ui::RpWidget {
public:
	WatcherCard(QWidget *parent, crl::time stagger);

protected:
	void paintEvent(QPaintEvent *e) override;
	virtual void paintCard(Painter &p) = 0;

private:
	Ui::Animations::Simple _appear;

};

class SessionCard final : public WatcherCard {
public:
	SessionCard(QWidget *parent, const OnlineSession &session, int index);

protected:
	int resizeGetHeight(int newWidth) override;
	void paintCard(Painter &p) override;

private:
	void paintStatus(Painter &p);
	void paintDuration(Painter &p);
	void computeLayout(int newWidth);

	OnlineSession _session;
	QString _startLabel;
	QString _endLabel;
	QString _startValue;
	QString _endValue;
	QString _durationText;
	QString _startValueElided;
	QString _endValueElided;
	Ui::Animations::Basic _pulse;
	float64 _pulsePhase = 0.;
	int _lineLeft = 0;
	int _lineWidth = 0;
	int _pillWidth = 0;

};

class EventCard final : public WatcherCard {
public:
	EventCard(
		QWidget *parent,
		const WatchEvent &event,
		not_null<PeerData*> peer,
		int index);

protected:
	int resizeGetHeight(int newWidth) override;
	void paintCard(Painter &p) override;

private:
	QString _chipLabel;
	QString _dateText;
	Ui::Text::String _text;
	int _kind = 0;
	int _chipWidth = 0;
	int _textLeft = 0;
	int _textWidth = 0;

};

class WatcherBody : public Ui::RpWidget {
public:
	WatcherBody(QWidget *parent, not_null<PeerData*> peer);

	void setTab(Tab tab);
	void setOrder(Order order);
	void refresh();

protected:
	int resizeGetHeight(int newWidth) override;

private:
	void reload();
	void rebuild();
	[[nodiscard]] std::vector<OnlineSession> sortedSessions() const;
	[[nodiscard]] std::vector<WatchEvent> sortedWatches() const;
	void refreshEarlier();

	not_null<PeerData*> _peer;
	std::vector<OnlineEvent> _events;
	std::vector<WatchEvent> _watches;
	std::vector<OnlineSession> _sessions;
	Tab _tab = Tab::All;
	Order _order = Order::Newest;
	int _sessionOverflow = 0;
	int _watchOverflow = 0;
	Ui::FlatLabel *_sessionsTitle = nullptr;
	Ui::FlatLabel *_eventsTitle = nullptr;
	Ui::FlatLabel *_sessionEmpty = nullptr;
	Ui::FlatLabel *_eventEmpty = nullptr;
	Ui::FlatLabel *_earlier = nullptr;
	std::vector<SessionCard*> _sessionCards;
	std::vector<EventCard*> _eventCards;

};

// Lays out one section: title, empty-state label and the cards, all inside
// the body width with the box row padding. Returns the new vertical offset.
template <typename Card>
int LayoutCards(
		int newWidth,
		int y,
		Ui::FlatLabel *title,
		Ui::FlatLabel *empty,
		const std::vector<Card*> &cards) {
	if (!title->isHidden()) {
		const auto left = st::defaultSubsectionTitlePadding.left();
		const auto width = newWidth
			- left
			- st::defaultSubsectionTitlePadding.right();
		title->resizeToWidth(width);
		title->moveToLeft(left, y + st::defaultSubsectionTitlePadding.top(), newWidth);
		y += st::defaultSubsectionTitlePadding.top()
			+ title->height()
			+ st::defaultSubsectionTitlePadding.bottom();
	}
	if (!empty->isHidden()) {
		const auto left = st::boxRowPadding.left();
		const auto width = newWidth - left - st::boxRowPadding.right();
		empty->resizeToWidth(width);
		empty->moveToLeft(left, y, newWidth);
		y += empty->height();
	}
	const auto left = st::boxRowPadding.left();
	const auto width = newWidth - left - st::boxRowPadding.right();
	for (const auto card : cards) {
		card->resizeToWidth(width);
		card->moveToLeft(left, y, newWidth);
		card->show();
		y += card->height() + st::luxuryWatcherCardSkip;
	}
	if (!cards.empty()) {
		y -= st::luxuryWatcherCardSkip;
	}
	return y;
}

ToolButton::ToolButton(QWidget *parent, ToolIcon icon)
: RpWidget(parent)
, _icon(icon) {
	setFixedSize(st::luxuryWatcherIconSize, st::luxuryWatcherIconSize);
	setCursor(style::cur_pointer);
}

void ToolButton::spin() {
	_spin.start(
		[=](float64) { update(); },
		0.,
		1.,
		kSpinDuration,
		anim::easeOutCubic);
}

void ToolButton::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	auto hq = PainterHighQualityEnabler(p);
	if (_ripple) {
		_ripple->paint(p, 0, 0, width());
		if (_ripple->empty()) {
			_ripple.reset();
		}
	}
	paintIcon(p, (_hovered ? st::windowFg : st::windowSubTextFg)->c);
}

void ToolButton::paintIcon(Painter &p, const QColor &color) {
	const auto base = st::luxuryWatcherIconSize / 4.;
	const auto penWidth = std::max(1., base / 4.);
	p.save();
	p.translate(QPointF(width() / 2., height() / 2.));
	p.rotate(_spin.value(1.) * 360.);
	p.setPen(QPen(
		color,
		penWidth,
		Qt::SolidLine,
		Qt::RoundCap,
		Qt::RoundJoin));
	p.setBrush(Qt::NoBrush);
	if (_icon == ToolIcon::Refresh) {
		PaintRefreshGlyph(p, base, color);
	} else {
		PaintGearGlyph(p, base, color, penWidth);
	}
	p.restore();
}

void ToolButton::mousePressEvent(QMouseEvent *e) {
	if (e->button() == Qt::LeftButton) {
		_pressed = true;
		if (!_ripple) {
			_ripple = std::make_unique<Ui::RippleAnimation>(
				st::defaultRippleAnimation,
				Ui::RippleAnimation::RoundRectMask(
					size(),
					st::luxuryWatcherIconSize / 2),
				[=] { update(); });
		}
		_ripple->add(e->pos());
	}
}

void ToolButton::mouseReleaseEvent(QMouseEvent *e) {
	if (base::take(_pressed)) {
		if (_ripple) {
			_ripple->lastStop();
		}
		if (rect().contains(e->pos())) {
			_clicks.fire({});
		}
	}
}

void ToolButton::enterEventHook(QEnterEvent *e) {
	_hovered = true;
	update();
	RpWidget::enterEventHook(e);
}

void ToolButton::leaveEventHook(QEvent *e) {
	_hovered = false;
	update();
	RpWidget::leaveEventHook(e);
}

SortControl::SortControl(QWidget *parent)
: RpWidget(parent) {
	_labels = {
		tr::luxury_OnlineHistorySortNewest(tr::now),
		tr::luxury_OnlineHistorySortOldest(tr::now),
		tr::luxury_OnlineHistorySortLongest(tr::now),
	};
	resize(contentWidth(), st::luxuryWatcherSortHeight);
	setCursor(style::cur_pointer);
}

int SortControl::contentWidth() const {
	auto result = 0;
	for (auto i = 0; i != int(_labels.size()); ++i) {
		result += segmentWidth(i) + st::luxuryWatcherSortSkip;
	}
	return result - st::luxuryWatcherSortSkip;
}

int SortControl::segmentWidth(int index) const {
	return st::luxuryWatcherChipFont->width(_labels[index])
		+ st::luxuryWatcherPillPadding.left()
		+ st::luxuryWatcherPillPadding.right();
}

int SortControl::segmentLeft(int index) const {
	auto result = 0;
	for (auto i = 0; i != index; ++i) {
		result += segmentWidth(i) + st::luxuryWatcherSortSkip;
	}
	return result;
}

void SortControl::setOrder(int order) {
	if (order == _selected) {
		return;
	}
	_selected = order;
	// A programmatic move is a correction, not a gesture: snap the pill.
	_pillLeft.stop();
	_pillWidth.stop();
	update();
}

void SortControl::setLongestEnabled(bool enabled) {
	if (_longestEnabled != enabled) {
		_longestEnabled = enabled;
		update();
	}
}

void SortControl::select(int order) {
	if (order == _selected) {
		return;
	}
	const auto fromLeft = segmentLeft(_selected);
	const auto fromWidth = segmentWidth(_selected);
	_selected = order;
	const auto toLeft = segmentLeft(_selected);
	const auto toWidth = segmentWidth(_selected);
	const auto updater = [=] { update(); };
	// easeOutCubic: the pill glides out fast and settles smoothly, the
	// same curve the section sliders use.
	_pillLeft.start(
		updater,
		fromLeft,
		toLeft,
		kSortDuration,
		anim::easeOutCubic);
	_pillWidth.start(
		updater,
		fromWidth,
		toWidth,
		kSortDuration,
		anim::easeOutCubic);
	_changes.fire_copy(_selected);
}

void SortControl::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	auto hq = PainterHighQualityEnabler(p);

	const auto pillLeft = _pillLeft.value(segmentLeft(_selected));
	const auto pillWidth = _pillWidth.value(segmentWidth(_selected));
	auto pillColor = QColor(st::windowActiveTextFg->c);
	pillColor.setAlphaF(kSegmentPillAlpha);
	p.setPen(Qt::NoPen);
	p.setBrush(pillColor);
	p.drawRoundedRect(
		QRectF(pillLeft, 0, pillWidth, height()),
		height() / 2.,
		height() / 2.);

	const auto &font = st::luxuryWatcherChipFont;
	p.setFont(font);
	const auto textTop = (height() - font->height) / 2;
	for (auto i = 0; i != int(_labels.size()); ++i) {
		const auto disabled = (i == 2 && !_longestEnabled);
		if (i == _selected) {
			p.setPen(QPen(st::windowActiveTextFg->c));
		} else {
			auto inactive = QColor(st::windowSubTextFg->c);
			if (disabled) {
				inactive.setAlphaF(kDisabledLabelAlpha);
			}
			p.setPen(QPen(inactive));
		}
		const auto left = segmentLeft(i);
		const auto width = segmentWidth(i);
		const auto textWidth = font->width(_labels[i]);
		p.drawText(
			QPointF(left + (width - textWidth) / 2, textTop + font->ascent),
			_labels[i]);
	}
}

void SortControl::mousePressEvent(QMouseEvent *e) {
	if (e->button() != Qt::LeftButton) {
		return;
	}
	const auto x = e->pos().x();
	for (auto i = 0; i != int(_labels.size()); ++i) {
		const auto left = segmentLeft(i);
		if (x < left + segmentWidth(i) + st::luxuryWatcherSortSkip) {
			// "Longest" has no meaning for events and paints disabled
			// there; the press bounces off instead of selecting it.
			if (!(i == 2 && !_longestEnabled)) {
				select(i);
			}
			return;
		}
	}
}

void SortControl::enterEventHook(QEnterEvent *e) {
	update();
	RpWidget::enterEventHook(e);
}

void SortControl::leaveEventHook(QEvent *e) {
	update();
	RpWidget::leaveEventHook(e);
}

WatcherToolbar::WatcherToolbar(QWidget *parent)
: RpWidget(parent) {
	_sort = Ui::CreateChild<SortControl>(this);
	_refresh = Ui::CreateChild<ToolButton>(this, ToolIcon::Refresh);
	_refresh->setToolTip(tr::luxury_OnlineHistoryRefresh(tr::now));
	_settings = Ui::CreateChild<ToolButton>(this, ToolIcon::Gear);
	_settings->setToolTip(tr::luxury_OnlineHistorySettings(tr::now));
}

void WatcherToolbar::setSortOrder(int order) {
	_sort->setOrder(order);
}

void WatcherToolbar::setLongestEnabled(bool enabled) {
	_sort->setLongestEnabled(enabled);
}

void WatcherToolbar::startRefreshSpin() {
	_refresh->spin();
}

int WatcherToolbar::resizeGetHeight(int newWidth) {
	const auto icon = st::luxuryWatcherIconSize;
	const auto buttons = 2 * icon + st::luxuryWatcherIconSkip;
	// The segmented control keeps its compact natural width; the stretch
	// between it and the buttons is empty space.
	const auto available = std::max(0, newWidth - buttons - st::luxuryWatcherIconSkip);
	const auto sortWidth = std::min(_sort->contentWidth(), available);
	_sort->setGeometryToLeft(
		0,
		0,
		sortWidth,
		st::luxuryWatcherSortHeight,
		newWidth);
	_settings->moveToRight(0, 0, newWidth);
	_refresh->moveToRight(icon + st::luxuryWatcherIconSkip, 0, newWidth);
	return std::max(st::luxuryWatcherSortHeight, icon);
}

WatcherCard::WatcherCard(QWidget *parent, crl::time stagger)
: RpWidget(parent) {
	// The stagger is folded into the transition: the first stagger
	// milliseconds hold the progress at zero, then 150 ms of easeOutCubic
	// raise the card from eight pixels below.
	_appear.start(
		[=](float64) { update(); },
		0.,
		1.,
		kCardAppearDuration + stagger,
		[stagger](float64 delta, float64 dt) {
			const auto time = dt * (kCardAppearDuration + stagger);
			const auto active = std::clamp(
				(time - stagger) / float64(kCardAppearDuration),
				0.,
				1.);
			return anim::easeOutCubic(delta, active);
		});
}

void WatcherCard::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	auto hq = PainterHighQualityEnabler(p);
	const auto progress = _appear.value(1.);
	p.translate(0., (1. - progress) * st::luxuryWatcherCardRise);
	auto opacity = ScopedPainterOpacity(p, progress);
	paintCard(p);
}

SessionCard::SessionCard(
		QWidget *parent,
		const OnlineSession &session,
		int index)
: WatcherCard(
		parent,
		std::min<crl::time>(index * kCardStagger, kCardStaggerMax))
, _session(session) {
	_startLabel = tr::luxury_OnlineHistoryStart(tr::now) + u":"_q;
	_endLabel = tr::luxury_OnlineHistoryEnd(tr::now) + u":"_q;
	if (_session.start) {
		_startValue = formatDateTime(base::unixtime::parse(*_session.start));
	} else {
		_startValue = tr::luxury_OnlineHistoryUnknown(tr::now);
	}
	if (!_session.end) {
		_endValue = tr::luxury_OnlineHistoryOpen(tr::now);
	} else {
		_endValue = formatDateTime(base::unixtime::parse(*_session.end));
	}
	if (_session.start && _session.end) {
		// Same-second flaps and clock steps clamp at zero -- the row
		// stays, flaps are data and are never merged or dropped.
		const auto seconds = std::max<qint64>(
			0,
			qint64(*_session.end) - qint64(*_session.start));
		_durationText = Ui::FormatDurationText(seconds);
	} else if (_session.start) {
		// Open by construction only with a start set (see the pairing
		// above); it reads as "since <start>", like the row always did.
		_durationText = tr::luxury_OnlineHistorySince(
			tr::now,
			lt_time,
			_startValue);
	}
	if (!_session.end) {
		_pulse.init([=](crl::time now) {
			_pulsePhase = (now % kPulseDuration) / float64(kPulseDuration);
			update();
			return true;
		});
		_pulse.start();
	}
}

void SessionCard::computeLayout(int newWidth) {
	const auto &font = st::luxuryWatcherChipFont;
	const auto &labelFont = st::luxuryWatcherLabelFont;
	_pillWidth = _durationText.isEmpty()
		? 0
		: st::luxuryWatcherPillPadding.left()
			+ st::luxuryWatcherClockSize
			+ st::luxuryWatcherPillPadding.left()
			+ font->width(_durationText)
			+ st::luxuryWatcherPillPadding.right();
	_lineLeft = st::luxuryWatcherCardPadding.left()
		+ st::luxuryWatcherDotSize
		+ st::luxuryWatcherCardSkip;
	_lineWidth = std::max(
		0,
		newWidth
			- _lineLeft
			- st::luxuryWatcherCardPadding.right()
			- (_pillWidth ? _pillWidth + st::luxuryWatcherCardSkip : 0));
	_startValueElided = st::luxuryWatcherValueFont->elided(
		_startValue,
		std::max(
			0,
			_lineWidth - labelFont->width(_startLabel) - labelFont->spacew));
	_endValueElided = st::luxuryWatcherValueFont->elided(
		_endValue,
		std::max(
			0,
			_lineWidth - labelFont->width(_endLabel) - labelFont->spacew));
}

int SessionCard::resizeGetHeight(int newWidth) {
	computeLayout(newWidth);
	return st::luxuryWatcherCardHeight;
}

void SessionCard::paintCard(Painter &p) {
	PaintCardShell(p, width(), height());
	paintStatus(p);

	const auto &labelFont = st::luxuryWatcherLabelFont;
	const auto &valueFont = st::luxuryWatcherValueFont;
	const auto lineH = valueFont->height;
	const auto blockH = 2 * lineH + st::luxuryWatcherCardSkip;
	auto top = (height() - blockH) / 2;
	for (auto line = 0; line != 2; ++line) {
		const auto &label = line ? _endLabel : _startLabel;
		const auto &value = line ? _endValueElided : _startValueElided;
		// Only the End line of an open session carries the accent.
		const auto valuePen = (!line || _session.end)
			? QPen(st::windowFg->c)
			: QPen(st::windowActiveTextFg->c);
		const auto lineRect = style::rtlrect(
			_lineLeft,
			top,
			_lineWidth,
			lineH,
			width());
		p.setFont(labelFont);
		p.setPen(st::windowSubTextFg);
		p.drawText(
			QPointF(lineRect.x(), top + labelFont->ascent),
			label);
		p.setFont(valueFont);
		p.setPen(valuePen);
		p.drawText(
			QPointF(
				lineRect.x() + labelFont->width(label) + labelFont->spacew,
				top + valueFont->ascent),
			value);
		top += lineH + st::luxuryWatcherCardSkip;
	}
	paintDuration(p);
}

void SessionCard::paintStatus(Painter &p) {
	const auto dot = st::luxuryWatcherDotSize;
	const auto rect = style::rtlrect(
		st::luxuryWatcherCardPadding.left(),
		(height() - dot) / 2,
		dot,
		dot,
		width());
	p.setPen(Qt::NoPen);
	if (!_session.end) {
		// Open: a dot with a halo that grows and fades on a sine loop.
		const auto eased = 0.5 - 0.5 * std::cos(2 * kPi * _pulsePhase);
		const auto halo = st::luxuryWatcherDotSize
			+ (st::luxuryWatcherPulseDiameter - st::luxuryWatcherDotSize)
				* eased;
		const auto center = QPointF(
			rect.x() + rect.width() / 2.,
			rect.y() + rect.height() / 2.);
		auto haloColor = QColor(st::windowActiveTextFg->c);
		haloColor.setAlphaF(kPulseHaloAlpha * (1. - eased));
		p.setBrush(haloColor);
		p.drawEllipse(center, halo / 2., halo / 2.);
		p.setBrush(st::windowActiveTextFg);
		p.drawEllipse(QRectF(rect));
	} else if (!_session.start) {
		// Unknown start: an outlined circle with a question mark.
		p.setPen(QPen(st::windowSubTextFg->c, dot / 5.));
		p.setBrush(Qt::NoBrush);
		p.drawEllipse(QRectF(rect));
		const auto &font = st::luxuryWatcherMarkFont;
		const auto mark = u"?"_q;
		p.setFont(font);
		p.setPen(st::windowSubTextFg);
		p.drawText(
			QPointF(
				rect.x() + (rect.width() - font->width(mark)) / 2,
				rect.y() + (rect.height() - font->height) / 2
					+ font->ascent),
			mark);
	} else {
		// Closed normally: a plain green dot.
		p.setBrush(kOkFg.color());
		p.drawEllipse(QRectF(rect));
	}
}

void SessionCard::paintDuration(Painter &p) {
	if (!_pillWidth) {
		return;
	}
	const auto &font = st::luxuryWatcherChipFont;
	const auto pillH = font->height
		+ st::luxuryWatcherPillPadding.top()
		+ st::luxuryWatcherPillPadding.bottom();
	const auto pillRect = style::rtlrect(
		width()
			- st::luxuryWatcherCardPadding.right()
			- _pillWidth,
		(height() - pillH) / 2,
		_pillWidth,
		pillH,
		width());
	auto pillColor = QColor(st::windowActiveTextFg->c);
	pillColor.setAlphaF(kDurationPillAlpha);
	p.setPen(Qt::NoPen);
	p.setBrush(pillColor);
	p.drawRoundedRect(
		QRectF(pillRect),
		st::luxuryWatcherPillRadius,
		st::luxuryWatcherPillRadius);
	const auto clock = st::luxuryWatcherClockSize;
	const auto clockX = pillRect.x() + st::luxuryWatcherPillPadding.left();
	PaintClock(
		p,
		QPointF(clockX, pillRect.y() + (pillH - clock) / 2),
		clock,
		st::windowActiveTextFg->c);
	p.setFont(font);
	p.setPen(st::windowActiveTextFg);
	p.drawText(
		QPointF(
			clockX + clock + st::luxuryWatcherPillPadding.left(),
			pillRect.y() + (pillH - font->height) / 2 + font->ascent),
		_durationText);
}

EventCard::EventCard(
		QWidget *parent,
		const WatchEvent &event,
		not_null<PeerData*> peer,
		int index)
: WatcherCard(
		parent,
		std::min<crl::time>(index * kCardStagger, kCardStaggerMax))
, _kind(event.kind) {
	_chipLabel = ChipLabelFor(_kind);
	_dateText = formatDateTime(base::unixtime::parse(event.at));
	_text.setText(st::boxTextStyle, WatchEventText(event, peer));
}

int EventCard::resizeGetHeight(int newWidth) {
	const auto &padding = st::luxuryWatcherCardPadding;
	_chipWidth = _chipLabel.isEmpty()
		? 0
		: st::luxuryWatcherChipFont->width(_chipLabel)
			+ st::luxuryWatcherPillPadding.left()
			+ st::luxuryWatcherPillPadding.right();
	const auto dateWidth = st::luxuryWatcherLabelFont->width(_dateText);
	_textLeft = padding.left()
		+ (_chipWidth ? _chipWidth + st::luxuryWatcherCardSkip : 0);
	_textWidth = std::max(
		0,
		newWidth
			- _textLeft
			- dateWidth
			- st::luxuryWatcherCardSkip
			- padding.right());
	const auto textHeight = _text.countHeight(_textWidth);
	const auto chipHeight = _chipWidth
		? st::luxuryWatcherChipFont->height
			+ st::luxuryWatcherPillPadding.top()
			+ st::luxuryWatcherPillPadding.bottom()
		: 0;
	return padding.top() + padding.bottom() + std::max(textHeight, chipHeight);
}

void EventCard::paintCard(Painter &p) {
	PaintCardShell(p, width(), height());
	const auto &padding = st::luxuryWatcherCardPadding;

	const auto &labelFont = st::luxuryWatcherLabelFont;
	const auto dateWidth = labelFont->width(_dateText);
	const auto dateRect = style::rtlrect(
		width() - padding.right() - dateWidth,
		padding.top(),
		dateWidth,
		labelFont->height,
		width());
	p.setFont(labelFont);
	p.setPen(st::windowSubTextFg);
	p.drawText(
		QPointF(dateRect.x(), dateRect.y() + labelFont->ascent),
		_dateText);

	if (_chipWidth) {
		const auto &font = st::luxuryWatcherChipFont;
		const auto chipH = font->height
			+ st::luxuryWatcherPillPadding.top()
			+ st::luxuryWatcherPillPadding.bottom();
		const auto chipRect = style::rtlrect(
			padding.left(),
			(height() - chipH) / 2,
			_chipWidth,
			chipH,
			width());
		const auto color = ChipColorFor(_kind);
		auto chipBg = QColor(color->c);
		chipBg.setAlphaF(kChipAlpha);
		p.setPen(Qt::NoPen);
		p.setBrush(chipBg);
		p.drawRoundedRect(
			QRectF(chipRect),
			st::luxuryWatcherPillRadius,
			st::luxuryWatcherPillRadius);
		p.setFont(font);
		p.setPen(color);
		p.drawText(
			QPointF(
				chipRect.x() + st::luxuryWatcherPillPadding.left(),
				chipRect.y() + (chipH - font->height) / 2 + font->ascent),
			_chipLabel);
	}

	p.setPen(st::windowFg);
	_text.draw(p, {
		.position = QPoint(_textLeft, padding.top()),
		.outerWidth = width(),
		.availableWidth = _textWidth,
		.align = style::al_left,
	});
}

WatcherBody::WatcherBody(QWidget *parent, not_null<PeerData*> peer)
: RpWidget(parent)
, _peer(peer) {
	_sessionsTitle = Ui::CreateChild<Ui::FlatLabel>(
		this,
		tr::luxury_OnlineHistorySessions(),
		st::defaultSubsectionTitle);
	_eventsTitle = Ui::CreateChild<Ui::FlatLabel>(
		this,
		tr::luxury_OnlineHistoryEvents(),
		st::defaultSubsectionTitle);
	_sessionEmpty = Ui::CreateChild<Ui::FlatLabel>(
		this,
		tr::luxury_OnlineHistorySessionsEmpty(),
		st::luxuryWatcherEmptyLabel);
	_eventEmpty = Ui::CreateChild<Ui::FlatLabel>(
		this,
		tr::luxury_OnlineHistoryEventsEmpty(),
		st::luxuryWatcherEmptyLabel);
	_earlier = Ui::CreateChild<Ui::FlatLabel>(
		this,
		QString(),
		st::luxuryWatcherFootnoteLabel);
	reload();
	rebuild();
}

void WatcherBody::setTab(Tab tab) {
	if (_tab == tab) {
		return;
	}
	_tab = tab;
	rebuild();
}

void WatcherBody::setOrder(Order order) {
	if (_order == order) {
		return;
	}
	_order = order;
	rebuild();
}

void WatcherBody::refresh() {
	reload();
	rebuild();
}

void WatcherBody::reload() {
	_events = LuxuryOnline::getHistory(_peer, kHistoryReadLimit);
	_watches = LuxuryOnline::getWatchEvents(_peer, kHistoryReadLimit);
	_sessions = PairOnlineSessions(_events);
}

std::vector<OnlineSession> WatcherBody::sortedSessions() const {
	auto list = std::vector<OnlineSession>(_sessions);
	switch (_order) {
	case Order::Oldest:
		// PairOnlineSessions is oldest-first already.
		break;
	case Order::Longest:
		std::sort(list.begin(), list.end(), [](
				const OnlineSession &a,
				const OnlineSession &b) {
			const auto left = a.start && a.end
				? qint64(*a.end) - qint64(*a.start)
				: -1;
			const auto right = b.start && b.end
				? qint64(*b.end) - qint64(*b.start)
				: -1;
			return left > right;
		});
		break;
	case Order::Newest:
	default:
		std::reverse(list.begin(), list.end());
		break;
	}
	return list;
}

std::vector<WatchEvent> WatcherBody::sortedWatches() const {
	auto list = std::vector<WatchEvent>(_watches);
	switch (_order) {
	case Order::Oldest:
		std::reverse(list.begin(), list.end());
		break;
	case Order::Newest:
	case Order::Longest:
	default:
		// getWatchEvents returns newest-first.
		break;
	}
	return list;
}

void WatcherBody::refreshEarlier() {
	const auto hidden = _sessionOverflow + _watchOverflow;
	const auto visible = hidden > 0
		&& (_tab == Tab::All
			|| (_tab == Tab::Sessions && _sessionOverflow > 0)
			|| (_tab == Tab::Events && _watchOverflow > 0));
	_earlier->setVisible(visible);
	if (visible) {
		_earlier->setText(tr::luxury_OnlineHistoryEarlier(
			tr::now,
			lt_count,
			_tab == Tab::Sessions
				? _sessionOverflow
				: _tab == Tab::Events
				? _watchOverflow
				: hidden));
	}
}

void WatcherBody::rebuild() {
	// Rebuild swaps the card children; deleteLater keeps the destruction
	// out of this paint event, hiding them so nothing overlaps first.
	for (const auto card : _sessionCards) {
		card->hide();
		card->deleteLater();
	}
	_sessionCards.clear();
	for (const auto card : _eventCards) {
		card->hide();
		card->deleteLater();
	}
	_eventCards.clear();

	const auto sessionList = sortedSessions();
	const auto watchList = sortedWatches();
	const auto sessionsVisible = _tab != Tab::Events;
	const auto eventsVisible = _tab != Tab::Sessions;
	_sessionOverflow = 0;
	_watchOverflow = 0;

	_sessionsTitle->setVisible(sessionsVisible);
	_eventsTitle->setVisible(eventsVisible);
	if (sessionsVisible) {
		if (sessionList.empty()) {
			_sessionEmpty->show();
		} else {
			_sessionEmpty->hide();
			const auto total = int(sessionList.size());
			const auto shown = std::min(total, kMaxOnlineHistoryRows);
			_sessionOverflow = total - shown;
			for (auto i = 0; i != shown; ++i) {
				_sessionCards.push_back(Ui::CreateChild<SessionCard>(
					this,
					sessionList[i],
					i));
			}
		}
	} else {
		_sessionEmpty->hide();
	}
	if (eventsVisible) {
		if (watchList.empty()) {
			_eventEmpty->show();
		} else {
			_eventEmpty->hide();
			const auto total = int(watchList.size());
			const auto shown = std::min(total, kMaxWatchEventRows);
			_watchOverflow = total - shown;
			for (auto i = 0; i != shown; ++i) {
				_eventCards.push_back(Ui::CreateChild<EventCard>(
					this,
					watchList[i],
					_peer,
					i));
			}
		}
	} else {
		_eventEmpty->hide();
	}
	refreshEarlier();
	if (width() > 0) {
		resizeToWidth(width());
	}
}

int WatcherBody::resizeGetHeight(int newWidth) {
	if (newWidth <= 0) {
		return 0;
	}
	auto y = 0;
	y = LayoutCards(newWidth, y, _sessionsTitle, _sessionEmpty, _sessionCards);
	y = LayoutCards(newWidth, y, _eventsTitle, _eventEmpty, _eventCards);
	if (!_earlier->isHidden()) {
		const auto left = st::boxRowPadding.left();
		const auto width = newWidth - left - st::boxRowPadding.right();
		_earlier->resizeToWidth(width);
		_earlier->moveToLeft(
			left,
			y + st::defaultSubsectionTitlePadding.top(),
			newWidth);
		y += st::defaultSubsectionTitlePadding.top() + _earlier->height();
	}
	return y;
}

void FillWatcherBox(
		not_null<Ui::GenericBox*> box,
		not_null<PeerData*> peer) {
	// Tabs pick what is shown (All / Sessions / Events), the segmented
	// control picks the order (Newest / Oldest / Longest -- sessions only,
	// events have no duration), and the two icon buttons reload the data
	// and open the tracking settings.
	box->setTitle(tr::luxury_OnlineHistoryTitle());
	box->setWidth(st::aboutWidth);
	box->verticalLayout()->resizeToWidth(box->width());

	struct BoxState {
		Tab tab = Tab::All;
		Order order = Order::Newest;
		base::unique_qptr<Ui::PopupMenu> menu;
	};
	const auto state = box->lifetime().make_state<BoxState>();

	// Everything below captures only pointers that outlive the handlers:
	// state (box lifetime), and the widget pointers (box layout).

	Ui::AddSkip(box->verticalLayout());

	const auto tabSlider = box->verticalLayout()->add(
		object_ptr<Ui::SettingsSlider>(
			box->verticalLayout(),
			st::defaultSettingsSlider),
		st::boxRowPadding);
	tabSlider->addSection(tr::luxury_OnlineHistorySortAll(tr::now));
	tabSlider->addSection(tr::luxury_OnlineHistorySortSessions(tr::now));
	tabSlider->addSection(tr::luxury_OnlineHistorySortEvents(tr::now));
	tabSlider->setActiveSectionFast(0);

	Ui::AddSkip(box->verticalLayout());

	const auto toolbar = box->verticalLayout()->add(
		object_ptr<WatcherToolbar>(box->verticalLayout()),
		st::boxRowPadding);

	Ui::AddSkip(box->verticalLayout());

	// Full width: the body insets its rows itself (box row padding for
	// cards, subsection title padding for section titles).
	const auto body = box->verticalLayout()->add(
		object_ptr<WatcherBody>(box->verticalLayout(), peer));

	tabSlider->sectionActivated(
	) | rpl::on_next([=](int index) {
		state->tab = static_cast<Tab>(index);
		// "Longest" only makes sense for sessions; disable the segment
		// and fall back to Newest when the Events tab makes it
		// meaningless.
		toolbar->setLongestEnabled(state->tab != Tab::Events);
		if (state->tab == Tab::Events && state->order == Order::Longest) {
			state->order = Order::Newest;
			body->setOrder(Order::Newest);
			toolbar->setSortOrder(static_cast<int>(Order::Newest));
		}
		body->setTab(state->tab);
	}, tabSlider->lifetime());

	toolbar->sortChanges(
	) | rpl::on_next([=](int index) {
		// Selecting Longest on the Events tab is a no-op; keep Newest.
		if (state->tab == Tab::Events
			&& static_cast<Order>(index) == Order::Longest) {
			toolbar->setSortOrder(static_cast<int>(state->order));
			return;
		}
		state->order = static_cast<Order>(index);
		body->setOrder(state->order);
	}, box->lifetime());

	toolbar->refreshClicks(
	) | rpl::on_next([=] {
		// The read is synchronous and local, so the spin is the only
		// feedback the refresh needs.
		toolbar->startRefreshSpin();
		body->refresh();
	}, box->lifetime());

	toolbar->settingsClicks(
	) | rpl::on_next([=] {
		state->menu = base::make_unique_q<Ui::PopupMenu>(
			toolbar,
			st::defaultPopupMenu);
		const auto addAction = Ui::Menu::CreateAddActionCallback(state->menu);
		const auto on = tr::luxury_OnlineHistorySettingOn(tr::now);
		const auto off = tr::luxury_OnlineHistorySettingOff(tr::now);
		const auto addToggle = [&](
				const QString &label,
				bool value,
				Fn<void(bool)> setter) {
			addAction(
				label + u": "_q + (value ? on : off),
				[=] { setter(!value); },
				nullptr);
		};
		// LuxurySettings is a singleton with a deleted copy constructor:
		// resolve it inside the handler, never capture a reference.
		addToggle(
			tr::luxury_TrackOnlineHistory(tr::now),
			LuxurySettings::getInstance().trackOnlineHistory(),
			[](bool value) {
				LuxurySettings::getInstance().setTrackOnlineHistory(value);
			});
		addToggle(
			tr::luxury_TrackOnlineEvenWhenLocked(tr::now),
			LuxurySettings::getInstance().trackOnlineEvenWhenLocked(),
			[](bool value) {
				LuxurySettings::getInstance().setTrackOnlineEvenWhenLocked(
					value);
			});
		const auto gear = toolbar->gearButton();
		state->menu->popup(
			gear->mapToGlobal(QPoint(0, gear->height())));
	}, box->lifetime());

	Ui::AddSkip(box->verticalLayout());
	box->addButton(tr::lng_close(), [=] { box->closeBox(); });
}

} // namespace

void Show(
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer) {
	controller->show(Box(FillWatcherBox, peer));
}

} // namespace LuxuryWatcher
