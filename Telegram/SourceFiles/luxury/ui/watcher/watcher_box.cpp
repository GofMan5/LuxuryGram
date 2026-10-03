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
#include "luxury/ui/watcher/watcher_components.h"
#include "luxury/utils/telegram_helpers.h"
#include "main/main_session.h"
#include "ui/effects/animations.h"
#include "ui/layers/generic_box.h"
#include "ui/style/style_core_color.h"
#include "ui/text/format_values.h"
#include "ui/text/text.h"
#include "ui/widgets/menu/menu.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "ui/widgets/menu/menu_add_action_callback_factory.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/scroll_area.h"
#include "ui/painter.h"
#include "ui/qt_object_factory.h"
#include "window/window_session_controller.h"

#include <QtGui/QPainterPath>

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
const auto kDeletedFg = style::internal::OwnedColor(QColor(0xef, 0x5b, 0x5b));
const auto kEditedFg = style::internal::OwnedColor(QColor(0x38, 0xb6, 0xe3));
const auto kReactionFg = style::internal::OwnedColor(QColor(0xf4, 0x7f, 0xb6));

// Timeline kinds beyond the stored WatchKind set: the deleted and edited
// message tables feed the same Events timeline, so their kinds live here
// where no database value can collide with them.
constexpr auto kKindDeleted = 100;
constexpr auto kKindEdited = 101;

// Timing (not geometry, so plain constants are fine here).
constexpr auto kCardAppearDuration = crl::time(150);
constexpr auto kCardStagger = crl::time(20);
constexpr auto kCardStaggerMax = crl::time(200);
constexpr auto kPulseDuration = crl::time(1600);

constexpr auto kCardBgBlend = 0.04;
constexpr auto kCardBorderAlpha = 40;
constexpr auto kDurationPillAlpha = 0.12;
constexpr auto kChipAlpha = 0.18;
constexpr auto kPulseHaloAlpha = 0.5;
constexpr auto kHeaderHairlineAlpha = 60;

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

constexpr auto kMaxOnlineHistoryRows = 50;
constexpr auto kMaxWatchEventRows = 30;
constexpr auto kHistoryReadLimit = 200;
constexpr auto kPreviewLimit = 160;

struct OnlineSession {
	std::optional<int> start;
	std::optional<int> end;
};

// One row of the merged Events timeline: a stored watch event, or a
// deleted/edited message rendered as one.
struct TrackedEvent {
	int at = 0;
	int kind = 0;
	QString text;
};

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
	case WatchKind::ReactionsChanged:
		return tr::luxury_WatchReactions(
			tr::now,
			lt_list,
			QString::fromStdString(event.title));
	default:
		return QString();
	}
}

// A message-preview row: the plain text, flattened (newlines to spaces)
// so a card stays a card; the collapsed card elides it further and the
// expanded one shows it whole.
[[nodiscard]] QString MessagePreview(const LuxuryMessageBase &message) {
	auto text = QString::fromStdString(message.text);
	text.replace(u'\n', u' ');
	return text.isEmpty() ? u"—"_q : text;
}

// What a collapsed card shows: the flattened text capped hard.
[[nodiscard]] QString ElidedPreview(const QString &text) {
	return text.size() > kPreviewLimit
		? text.left(kPreviewLimit) + u"…"_q
		: text;
}

// A timeline timestamp that stays short: bare time for today, day and
// month added within the current year, the full date further back. The
// long "dd.MM.yyyy at HH:mm:ss" form made every card noisy.
[[nodiscard]] QString CompactTimestamp(int at) {
	const auto time = base::unixtime::parse(at);
	const auto today = QDateTime::currentDateTime().date();
	const auto date = time.date();
	if (date == today) {
		return time.toString(u"HH:mm"_q);
	}
	const auto timePart = u" "_q + time.toString(u"HH:mm"_q);
	return (date.year() == today.year())
		? date.toString(u"dd.MM"_q) + timePart
		: date.toString(u"dd.MM.yyyy"_q) + timePart;
}

// When the row was written beats when the message claims to have been
// sent: the timeline cares about the moment the thing was noticed.
[[nodiscard]] int MessageTimestamp(const LuxuryMessageBase &message) {
	return message.entityCreateDate
		? message.entityCreateDate
		: message.editDate
		? message.editDate
		: message.date;
}

[[nodiscard]] QString ChipLabelFor(int kind) {
	switch (kind) {
	case kKindDeleted:
		return tr::luxury_OnlineHistoryKindDeleted(tr::now);
	case kKindEdited:
		return tr::luxury_OnlineHistoryKindEdited(tr::now);
	default:
		break;
	}
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
	case WatchKind::ReactionsChanged:
		return tr::luxury_OnlineHistoryKindReaction(tr::now);
	default:
		return QString();
	}
}

[[nodiscard]] style::color ChipColorFor(int kind) {
	switch (kind) {
	case kKindDeleted:
		return kDeletedFg.color();
	case kKindEdited:
		return kEditedFg.color();
	default:
		break;
	}
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
	case WatchKind::ReactionsChanged:
		return kReactionFg.color();
	default:
		return st::windowSubTextFg;
	}
}

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
	void setSortIndex(int index);
	void setLastEnabled(bool enabled);
	void startRefreshSpin();

protected:
	int resizeGetHeight(int newWidth) override;

private:
	LuxuryUi::SegmentControl *_sort = nullptr;
	LuxuryUi::ToolButton *_refresh = nullptr;
	LuxuryUi::ToolButton *_settings = nullptr;

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
	EventCard(QWidget *parent, const TrackedEvent &event, int index);

	// A card with a text longer than the collapsed cap can be clicked to
	// show the whole text; the body relayouts on every toggle. The
	// chevron in the corner marks the cards that expand.
	[[nodiscard]] rpl::producer<> toggleRequests() const {
		return _toggles.events();
	}

protected:
	int resizeGetHeight(int newWidth) override;
	void paintCard(Painter &p) override;
	void mousePressEvent(QMouseEvent *e) override;

private:
	QString _chipLabel;
	QString _dateText;
	Ui::Text::String _preview;
	Ui::Text::String _text;
	bool _expandable = false;
	bool _expanded = false;
	rpl::event_stream<> _toggles;
	int _kind = 0;
	int _chipWidth = 0;
	int _chipHeight = 0;
	int _rowHeight = 0;
	int _textTop = 0;
	int _textWidth = 0;

};

class WatcherBody : public Ui::RpWidget {
public:
	WatcherBody(QWidget *parent, not_null<PeerData*> peer);

	void setTab(Tab tab);
	void setOrder(Order order);
	void refresh();
	[[nodiscard]] rpl::producer<std::array<int, 3>> countsChanged() const {
		return _counts.events();
	}
	// The tab labels show counts, and the box is built before anyone
	// subscribes to countsChanged(), so the first state is read directly.
	[[nodiscard]] std::array<int, 3> counts() const {
		const auto sessions = int(_sessions.size());
		const auto events = int(
			_watches.size() + _deleted.size() + _edits.size());
		return { sessions + events, sessions, events };
	}

protected:
	int resizeGetHeight(int newWidth) override;

private:
	void reload();
	void rebuild();
	[[nodiscard]] std::vector<OnlineSession> sortedSessions() const;
	[[nodiscard]] std::vector<TrackedEvent> sortedEvents() const;
	void refreshEarlier();

	not_null<PeerData*> _peer;
	std::vector<OnlineEvent> _events;
	std::vector<WatchEvent> _watches;
	std::vector<LuxuryMessageBase> _deleted;
	std::vector<LuxuryMessageBase> _edits;
	std::vector<OnlineSession> _sessions;
	Tab _tab = Tab::All;
	Order _order = Order::Newest;
	int _sessionOverflow = 0;
	int _eventOverflow = 0;
	Ui::FlatLabel *_sessionsTitle = nullptr;
	Ui::FlatLabel *_eventsTitle = nullptr;
	LuxuryUi::EmptyBlock *_sessionEmpty = nullptr;
	LuxuryUi::EmptyBlock *_eventEmpty = nullptr;
	Ui::FlatLabel *_earlier = nullptr;
	std::vector<SessionCard*> _sessionCards;
	std::vector<EventCard*> _eventCards;
	rpl::event_stream<std::array<int, 3>> _counts;

};

// The pinned part of the box: tabs over the toolbar, an opaque plate with
// a hairline at the bottom. The list scrolls under it and is clipped
// below it, so nothing ever slides behind the tabs.
class WatcherHeader : public Ui::RpWidget {
public:
	WatcherHeader(QWidget *parent);

	[[nodiscard]] rpl::producer<int> tabChanges() const {
		return _tabs->tabChanges();
	}
	[[nodiscard]] rpl::producer<int> sortChanges() const {
		return _toolbar->sortChanges();
	}
	[[nodiscard]] rpl::producer<> refreshClicks() const {
		return _toolbar->refreshClicks();
	}
	[[nodiscard]] rpl::producer<> settingsClicks() const {
		return _toolbar->settingsClicks();
	}
	[[nodiscard]] Ui::RpWidget *gearButton() const {
		return _toolbar->gearButton();
	}
	void setSortIndex(int index);
	void setLastEnabled(bool enabled);
	void startRefreshSpin();
	void setTabCounts(std::array<int, 3> counts);

protected:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

private:
	[[nodiscard]] int contentHeight() const;

	LuxuryUi::TabsBar *_tabs = nullptr;
	WatcherToolbar *_toolbar = nullptr;

};

// One fixed-height view: the pinned header on top, the scrolling list
// below. Owning the scroll here is what keeps the tabs in place while the
// list moves.
class WatcherView : public Ui::RpWidget {
public:
	WatcherView(QWidget *parent, not_null<PeerData*> peer);

protected:
	void resizeEvent(QResizeEvent *e) override;

private:
	void showSettingsMenu();

	WatcherHeader *_header = nullptr;
	Ui::ScrollArea *_scroll = nullptr;
	WatcherBody *_body = nullptr;
	Tab _tab = Tab::All;
	Order _order = Order::Newest;
	base::unique_qptr<Ui::PopupMenu> _menu;

};

// Lays out one section: title, empty-state block and the cards, all
// inside the body width with the box row padding. Returns the new
// vertical offset.
template <typename Card>
int LayoutCards(
		int newWidth,
		int y,
		Ui::FlatLabel *title,
		LuxuryUi::EmptyBlock *empty,
		const std::vector<Card*> &cards) {
	if (!title->isHidden()) {
		// The same row padding the cards use, so a title and its cards
		// share one left edge (subsection padding is 2 px narrower).
		const auto left = st::boxRowPadding.left();
		const auto width = newWidth
			- left
			- st::boxRowPadding.right();
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
		empty->moveToLeft(left, y + st::luxuryWatcherCardSkip, newWidth);
		y += 2 * st::luxuryWatcherCardSkip + empty->height();
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

WatcherToolbar::WatcherToolbar(QWidget *parent)
: RpWidget(parent) {
	_sort = Ui::CreateChild<LuxuryUi::SegmentControl>(
		this,
		std::array<QString, 3>{
			tr::luxury_OnlineHistorySortNewest(tr::now),
			tr::luxury_OnlineHistorySortOldest(tr::now),
			tr::luxury_OnlineHistorySortLongest(tr::now),
		});
	_refresh = Ui::CreateChild<LuxuryUi::ToolButton>(
		this,
		LuxuryUi::ToolGlyph::Refresh);
	_refresh->setToolTip(tr::luxury_OnlineHistoryRefresh(tr::now));
	_settings = Ui::CreateChild<LuxuryUi::ToolButton>(
		this,
		LuxuryUi::ToolGlyph::Gear);
	_settings->setToolTip(tr::luxury_OnlineHistorySettings(tr::now));
}

void WatcherToolbar::setSortIndex(int index) {
	_sort->setIndex(index);
}

void WatcherToolbar::setLastEnabled(bool enabled) {
	_sort->setLastEnabled(enabled);
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
		_startValue = CompactTimestamp(*_session.start);
	} else {
		_startValue = tr::luxury_OnlineHistoryUnknown(tr::now);
	}
	if (!_session.end) {
		_endValue = tr::luxury_OnlineHistoryOpen(tr::now);
	} else {
		_endValue = CompactTimestamp(*_session.end);
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
	LuxuryUi::PaintClockGlyph(
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
		const TrackedEvent &event,
		int index)
: WatcherCard(
		parent,
		std::min<crl::time>(index * kCardStagger, kCardStaggerMax))
, _kind(event.kind) {
	_chipLabel = ChipLabelFor(_kind);
	_dateText = CompactTimestamp(event.at);
	_text.setText(st::boxTextStyle, event.text);
	_preview.setText(st::boxTextStyle, ElidedPreview(event.text));
	_expandable = event.text.size() > kPreviewLimit;
	if (_expandable) {
		setCursor(style::cur_pointer);
	}
}

void EventCard::mousePressEvent(QMouseEvent *e) {
	if (e->button() == Qt::LeftButton && _expandable) {
		_expanded = !_expanded;
		_toggles.fire({});
		update();
	}
}

int EventCard::resizeGetHeight(int newWidth) {
	const auto &padding = st::luxuryWatcherCardPadding;
	_chipWidth = _chipLabel.isEmpty()
		? 0
		: st::luxuryWatcherChipFont->width(_chipLabel)
			+ st::luxuryWatcherPillPadding.left()
			+ st::luxuryWatcherPillPadding.right();
	_chipHeight = _chipWidth
		? st::luxuryWatcherChipFont->height
			+ st::luxuryWatcherPillPadding.top()
			+ st::luxuryWatcherPillPadding.bottom()
		: 0;
	// The chip and the date share one top row; the text always starts
	// below that row at full width, so a long text is never squeezed
	// between the chip and the date.
	_rowHeight = std::max(_chipHeight, st::luxuryWatcherLabelFont->height);
	_textTop = padding.top() + _rowHeight + st::luxuryWatcherCardSkip;
	_textWidth = std::max(0, newWidth - padding.left() - padding.right());
	const auto &active = _expanded ? _text : _preview;
	const auto textHeight = active.countHeight(_textWidth);
	const auto stripHeight = _expandable
		? st::luxuryWatcherExpandGlyphHeight + st::luxuryWatcherCardSkip
		: 0;
	return _textTop + textHeight + stripHeight + padding.bottom();
}

void EventCard::paintCard(Painter &p) {
	PaintCardShell(p, width(), height());
	const auto &padding = st::luxuryWatcherCardPadding;

	const auto &labelFont = st::luxuryWatcherLabelFont;
	const auto dateWidth = labelFont->width(_dateText);
	const auto dateRect = style::rtlrect(
		width() - padding.right() - dateWidth,
		padding.top() + (_rowHeight - labelFont->height) / 2,
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
		const auto chipRect = style::rtlrect(
			padding.left(),
			padding.top() + (_rowHeight - _chipHeight) / 2,
			_chipWidth,
			_chipHeight,
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
				chipRect.y() + (_chipHeight - font->height) / 2 + font->ascent),
			_chipLabel);
	}

	p.setPen(st::windowFg);
	const auto &active = _expanded ? _text : _preview;
	active.draw(p, {
		.position = QPoint(padding.left(), _textTop),
		.outerWidth = width(),
		.availableWidth = _textWidth,
		.align = style::al_left,
	});

	if (_expandable) {
		// The chevron marks the card as expandable: down while more text
		// is hidden, up when the full text is shown.
		const auto w = st::luxuryWatcherExpandGlyphWidth;
		const auto h = st::luxuryWatcherExpandGlyphHeight;
		const auto x = float64(width() - padding.right() - w);
		const auto y = float64(height() - padding.bottom() - h);
		auto path = QPainterPath();
		if (_expanded) {
			path.moveTo(x, y + h);
			path.lineTo(x + w / 2., y);
			path.lineTo(x + w, y + h);
		} else {
			path.moveTo(x, y);
			path.lineTo(x + w / 2., y + h);
			path.lineTo(x + w, y);
		}
		path.closeSubpath();
		p.setPen(Qt::NoPen);
		p.setBrush(st::windowSubTextFg);
		p.drawPath(path);
	}
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
	_sessionEmpty = Ui::CreateChild<LuxuryUi::EmptyBlock>(
		this,
		tr::luxury_OnlineHistorySessionsEmpty(tr::now));
	_eventEmpty = Ui::CreateChild<LuxuryUi::EmptyBlock>(
		this,
		tr::luxury_OnlineHistoryEventsEmpty(tr::now));
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
	_deleted = LuxuryMessages::getDeletedMessages(
		_peer,
		0,
		0,
		0,
		kHistoryReadLimit);
	_edits = LuxuryMessages::getEditedMessagesForDialog(
		_peer,
		kHistoryReadLimit);
	_sessions = PairOnlineSessions(_events);
	_counts.fire(counts());
}

std::vector<OnlineSession> WatcherBody::sortedSessions() const {
	auto list = std::vector<OnlineSession>(_sessions);
	switch (_order) {
	case Order::Oldest:
		// PairOnlineSessions is oldest-first already.
		break;
	case Order::Longest:
		// A stable order: rows with no measurable duration keep their
		// relative position across rebuilds.
		std::stable_sort(list.begin(), list.end(), [](
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

std::vector<TrackedEvent> WatcherBody::sortedEvents() const {
	auto list = std::vector<TrackedEvent>();
	list.reserve(_watches.size() + _deleted.size() + _edits.size());
	for (const auto &watch : _watches) {
		list.push_back({ watch.at, watch.kind, WatchEventText(watch, _peer) });
	}
	for (const auto &deleted : _deleted) {
		list.push_back({
			MessageTimestamp(deleted),
			kKindDeleted,
			MessagePreview(deleted),
		});
	}
	for (const auto &edited : _edits) {
		list.push_back({
			MessageTimestamp(edited),
			kKindEdited,
			MessagePreview(edited),
		});
	}
	// A stable order: same-second rows (a delete and an edit recorded in
	// one second) keep their relative position across rebuilds.
	std::stable_sort(list.begin(), list.end(), [](
			const TrackedEvent &a,
			const TrackedEvent &b) {
		return a.at > b.at;
	});
	if (_order == Order::Oldest) {
		std::reverse(list.begin(), list.end());
	}
	return list;
}

void WatcherBody::refreshEarlier() {
	const auto hidden = _sessionOverflow + _eventOverflow;
	const auto visible = hidden > 0
		&& (_tab == Tab::All
			|| (_tab == Tab::Sessions && _sessionOverflow > 0)
			|| (_tab == Tab::Events && _eventOverflow > 0));
	_earlier->setVisible(visible);
	if (visible) {
		_earlier->setText(tr::luxury_OnlineHistoryEarlier(
			tr::now,
			lt_count,
			_tab == Tab::Sessions
				? _sessionOverflow
				: _tab == Tab::Events
				? _eventOverflow
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
	const auto eventList = sortedEvents();
	const auto sessionsVisible = _tab != Tab::Events;
	const auto eventsVisible = _tab != Tab::Sessions;
	_sessionOverflow = 0;
	_eventOverflow = 0;

	_sessionsTitle->setVisible(sessionsVisible);
	_eventsTitle->setVisible(eventsVisible);
	if (sessionsVisible) {
		_sessionsTitle->setText(
			tr::luxury_OnlineHistorySessions(tr::now)
			+ u" · "_q
			+ QString::number(_sessions.size()));
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
		_eventsTitle->setText(
			tr::luxury_OnlineHistoryEvents(tr::now)
			+ u" · "_q
			+ QString::number(
				_watches.size() + _deleted.size() + _edits.size()));
		if (eventList.empty()) {
			_eventEmpty->show();
		} else {
			_eventEmpty->hide();
			const auto total = int(eventList.size());
			const auto shown = std::min(total, kMaxWatchEventRows);
			_eventOverflow = total - shown;
			for (auto i = 0; i != shown; ++i) {
				const auto card = Ui::CreateChild<EventCard>(
					this,
					eventList[i],
					i);
				_eventCards.push_back(card);
				// An expanding card changes its own height: re-run the
				// layout so the cards below it move out of the way.
				card->toggleRequests(
				) | rpl::on_next([=] {
					resizeToWidth(width());
				}, card->lifetime());
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

int WatcherHeader::contentHeight() const {
	return st::luxuryWatcherHeaderSkip
		+ st::luxuryWatcherTabHeight
		+ st::luxuryWatcherHeaderSkip
		+ std::max(st::luxuryWatcherSortHeight, st::luxuryWatcherIconSize)
		+ st::luxuryWatcherHeaderSkip;
}

WatcherHeader::WatcherHeader(QWidget *parent)
: RpWidget(parent) {
	_tabs = Ui::CreateChild<LuxuryUi::TabsBar>(
		this,
		std::array<QString, 3>{
			tr::luxury_OnlineHistorySortAll(tr::now),
			tr::luxury_OnlineHistorySortSessions(tr::now),
			tr::luxury_OnlineHistorySortEvents(tr::now),
		});
	_toolbar = Ui::CreateChild<WatcherToolbar>(this);
	setFixedHeight(contentHeight());
}

void WatcherHeader::setSortIndex(int index) {
	_toolbar->setSortIndex(index);
}

void WatcherHeader::setLastEnabled(bool enabled) {
	_toolbar->setLastEnabled(enabled);
}

void WatcherHeader::startRefreshSpin() {
	_toolbar->startRefreshSpin();
}

void WatcherHeader::setTabCounts(std::array<int, 3> counts) {
	_tabs->setCounts(counts);
}

void WatcherHeader::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	// An opaque plate: the list scrolls below this widget and is clipped
	// by the view geometry, so this is belt-and-braces against any
	// future overlap, plus the separator under the pinned part.
	p.fillRect(rect(), st::boxBg);
	auto hairline = st::windowShadowFg->c;
	hairline.setAlpha(kHeaderHairlineAlpha);
	p.fillRect(
		0,
		height() - st::lineWidth,
		width(),
		st::lineWidth,
		hairline);
}

void WatcherHeader::resizeEvent(QResizeEvent *e) {
	RpWidget::resizeEvent(e);
	const auto left = st::boxRowPadding.left();
	const auto width = this->width() - left - st::boxRowPadding.right();
	_tabs->setGeometryToLeft(
		left,
		st::luxuryWatcherHeaderSkip,
		width,
		st::luxuryWatcherTabHeight,
		this->width());
	// resizeToWidth (not setGeometry): the toolbar lays its children out
	// in resizeGetHeight, which only runs through it.
	_toolbar->resizeToWidth(width);
	_toolbar->moveToLeft(
		left,
		st::luxuryWatcherHeaderSkip
			+ st::luxuryWatcherTabHeight
			+ st::luxuryWatcherHeaderSkip);
}

WatcherView::WatcherView(QWidget *parent, not_null<PeerData*> peer)
: RpWidget(parent) {
	// The pinned header plus the scrolling list never exceed this: the
	// box grows to the title, the view and the button row, and taller
	// lists scroll inside the view instead of growing the box.
	setFixedHeight(st::luxuryWatcherViewHeight);

	_header = Ui::CreateChild<WatcherHeader>(this);
	_scroll = Ui::CreateChild<Ui::ScrollArea>(this, st::boxScroll);
	_body = _scroll->setOwnedWidget(
		object_ptr<WatcherBody>(_scroll, peer));

	_header->tabChanges(
	) | rpl::on_next([=](int index) {
		_tab = static_cast<Tab>(index);
		// "Longest" only makes sense for sessions; disable the segment
		// and fall back to Newest when the Events tab makes it
		// meaningless.
		_header->setLastEnabled(_tab != Tab::Events);
		if (_tab == Tab::Events && _order == Order::Longest) {
			_order = Order::Newest;
			_body->setOrder(Order::Newest);
			_header->setSortIndex(static_cast<int>(Order::Newest));
		}
		_body->setTab(_tab);
		_scroll->scrollToY(0);
	}, lifetime());

	_header->sortChanges(
	) | rpl::on_next([=](int index) {
		// Selecting Longest on the Events tab is a no-op; keep Newest.
		if (_tab == Tab::Events
			&& static_cast<Order>(index) == Order::Longest) {
			_header->setSortIndex(static_cast<int>(_order));
			return;
		}
		_order = static_cast<Order>(index);
		_body->setOrder(_order);
	}, lifetime());

	_header->refreshClicks(
	) | rpl::on_next([=] {
		// The read is synchronous and local, so the spin is the only
		// feedback the refresh needs.
		_header->startRefreshSpin();
		_body->refresh();
	}, lifetime());

	_header->settingsClicks(
	) | rpl::on_next([=] {
		showSettingsMenu();
	}, lifetime());

	_body->countsChanged(
	) | rpl::on_next([=](std::array<int, 3> counts) {
		_header->setTabCounts(counts);
	}, lifetime());
	_header->setTabCounts(_body->counts());
}

void WatcherView::showSettingsMenu() {
	_menu = base::make_unique_q<Ui::PopupMenu>(
		_header,
		st::defaultPopupMenu);
	const auto addAction = Ui::Menu::CreateAddActionCallback(_menu);
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
			LuxurySettings::getInstance().setTrackOnlineEvenWhenLocked(value);
		});
	const auto gear = _header->gearButton();
	_menu->popup(gear->mapToGlobal(QPoint(0, gear->height())));
}

void WatcherView::resizeEvent(QResizeEvent *e) {
	RpWidget::resizeEvent(e);
	_header->setGeometry(0, 0, width(), _header->height());
	_scroll->setGeometry(0, _header->height(), width(), height() - _header->height());
	// QScrollArea does not track the viewport width itself: the list is
	// sized to the scroll width, the same convention the box scroll uses
	// (the bar overlaps the row padding, the cards inset themselves).
	_body->resizeToWidth(_scroll->width());
}

void FillWatcherBox(
		not_null<Ui::GenericBox*> box,
		not_null<PeerData*> peer) {
	// The view owns its pinned header (tabs over the toolbar) and its
	// scrolling card list: the tabs stay in place while the list moves,
	// and nothing scrolls behind them.
	box->setTitle(tr::luxury_OnlineHistoryTitle());
	box->setWidth(st::aboutWidth);

	box->verticalLayout()->add(
		object_ptr<WatcherView>(box->verticalLayout(), peer),
		style::margins());

	box->addButton(tr::lng_close(), [=] { box->closeBox(); });
}

} // namespace

void Show(
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer) {
	controller->show(Box(FillWatcherBox, peer));
}

} // namespace LuxuryWatcher
