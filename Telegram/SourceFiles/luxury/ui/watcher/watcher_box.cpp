// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "luxury/ui/watcher/watcher_box.h"

#include "base/timer.h"
#include "base/unixtime.h"
#include "base/unique_qptr.h"
#include "base/weak_ptr.h"
#include "crl/crl_on_main.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history_item.h"
#include "lang_auto.h"
#include "luxury/data/luxury_database.h"
#include "luxury/data/messages_storage.h"
#include "luxury/features/watch/presence_history.h"
#include "luxury/luxury_settings.h"
#include "luxury/ui/watcher/watcher_components.h"
#include "luxury/ui/watcher/watcher_revisions.h"
#include "luxury/utils/telegram_helpers.h"
#include "main/main_session.h"
#include "ui/effects/animations.h"
#include "ui/layers/generic_box.h"
#include "ui/style/style_core_color.h"
#include "ui/text/format_values.h"
#include "ui/text/text.h"
#include "ui/widgets/menu/menu.h"
#include "ui/widgets/menu/menu_toggle.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "ui/painter.h"
#include "ui/qt_object_factory.h"
#include "window/window_session_controller.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <set>
#include <tuple>
#include <vector>
#include <QtCore/QLocale>
#include <QtWidgets/QApplication>

#include "styles/style_basic.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_luxury_styles.h"
#include "styles/style_settings.h"
#include "styles/style_widgets.h"

namespace LuxuryWatcher {
namespace {

constexpr auto kPi = 3.14159265358979323846;

// Timeline kinds beyond the stored WatchKind set: the deleted and edited
// message tables feed the same Events timeline, so their kinds live here
// where no database value can collide with them.
constexpr auto kKindDeleted = 100;
constexpr auto kKindEdited = 101;

// Timing (not geometry, so plain constants are fine here).
constexpr auto kPulseDuration = crl::time(1600);

constexpr auto kCardBgBlend = 0.025;
constexpr auto kCardBorderAlpha = 28;
constexpr auto kDurationPillAlpha = 0.10;
constexpr auto kPulseHaloAlpha = 0.5;

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
constexpr auto kMoreRows = 30;

using LuxuryFeatures::Watch::OnlineSession;
using LuxuryFeatures::Watch::PairOnlineSessions;

constexpr auto kRefreshInterval = crl::time(5000);

// One row of the merged Events timeline: a stored watch event, or a
// deleted/edited message rendered as one.
struct EventDetail {
	QString label;
	QString text;
};

struct TrackedEvent {
	int at = 0;
	int kind = 0;
	QString summary;
	std::vector<EventDetail> details;
	QString context;
	QString note;
	ID messageId = 0;
	ID rowId = 0;
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
	auto bg = CardBackgroundColor();
	p.setPen(QPen(border, st::lineWidth));
	p.setBrush(bg);
	const auto inset = st::lineWidth / 2.;
	p.drawRoundedRect(
		QRectF(inset, inset, width - 2 * inset, height - 2 * inset),
		st::luxuryWatcherCardRadius,
		st::luxuryWatcherCardRadius);
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
		return tr::luxury_WatchNameChanged(tr::now);
	case WatchKind::UsernameChanged:
		return tr::luxury_WatchUsernameChanged(tr::now);
	case WatchKind::PhotoUpdated:
		return tr::luxury_WatchPhotoUpdated(tr::now);
	case WatchKind::MessageEdited:
		return tr::luxury_WatcherMessageEdited(tr::now);
	case WatchKind::ReactionsChanged:
		return event.title.empty()
			? tr::luxury_WatcherReactionsRemoved(tr::now)
			: tr::luxury_WatchReactions(
				tr::now,
				lt_list,
				QString::fromStdString(event.title));
	default:
		return QString();
	}
}

[[nodiscard]] QString MessageText(const LuxuryMessageBase &message) {
	return QString::fromStdString(message.text);
}

[[nodiscard]] QString MessageContext(
		not_null<PeerData*> peer,
		ID messageId,
		ID fromId = 0) {
	const auto author = fromId
		? peer->session().data().peerLoaded(static_cast<PeerId>(fromId))
		: nullptr;
	return (author ? author->name() + u" · "_q : QString())
		+ tr::luxury_WatcherMessageId(
			tr::now,
			lt_id,
			QString::number(messageId));
}

[[nodiscard]] TrackedEvent WatchEventDetails(
		const WatchEvent &event,
		not_null<PeerData*> peer) {
	auto result = TrackedEvent();
	result.at = event.at;
	result.rowId = event.fakeId;
	result.kind = event.kind;
	result.messageId = event.messageId;
	result.summary = WatchEventText(event, peer);
	if (event.messageId) {
		result.context = MessageContext(peer, event.messageId);
	}
	const auto kind = static_cast<WatchKind>(event.kind);
	if (kind == WatchKind::NameChanged
		|| kind == WatchKind::UsernameChanged
		|| kind == WatchKind::MessageEdited) {
		const auto username = (kind == WatchKind::UsernameChanged);
		const auto value = [=](const std::string &text) {
			const auto string = QString::fromStdString(text);
			return string.isEmpty()
				? tr::luxury_WatcherEmptyValue(tr::now)
				: (username ? u"@"_q + string : string);
		};
		result.details = {
			{ tr::luxury_WatchEditWas(tr::now), value(event.title) },
			{ tr::luxury_WatchEditNow(tr::now), value(event.extra) },
		};
	} else if (kind == WatchKind::ReactionsChanged) {
		auto label = tr::luxury_WatcherMessageText(tr::now);
		auto context = QString::fromStdString(event.extra);
		if (context.isEmpty()) {
			if (const auto item = peer->session().data().message(
					peer,
					MsgId(event.messageId))) {
				context = item->originalText().text;
				label = tr::luxury_WatcherCurrentText(tr::now);
			}
		}
		if (context.isEmpty()) {
			result.note = tr::luxury_WatcherContextUnavailable(tr::now);
		} else {
			result.details.push_back({
				label,
				context,
			});
		}
	}
	return result;
}

// A timeline timestamp that stays short: bare time for today, day and
// month added within the current year, the full date further back. The
// long "dd.MM.yyyy at HH:mm:ss" form made every card noisy.
[[nodiscard]] QString CompactTimestamp(int at) {
	const auto time = base::unixtime::parse(at);
	const auto today = QDateTime::currentDateTime().date();
	const auto date = time.date();
	if (date == today) {
		return time.toString(u"HH:mm:ss"_q);
	}
	const auto timePart = u" "_q + time.toString(u"HH:mm:ss"_q);
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
	case WatchKind::MessageEdited:
		return tr::luxury_OnlineHistoryKindEdited(tr::now);
	default:
		return QString(QChar(0x2014));
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
	WatcherCard(QWidget *parent);

protected:
	void paintEvent(QPaintEvent *e) override;
	virtual void paintCard(Painter &p) = 0;

};

class SessionCard final : public WatcherCard {
public:
	SessionCard(QWidget *parent, const OnlineSession &session, bool active);
	void tick();
	void setActive(bool active);
	[[nodiscard]] const OnlineSession &session() const { return _session; }

protected:
	int resizeGetHeight(int newWidth) override;
	void paintCard(Painter &p) override;

private:
	void paintStatus(Painter &p);
	void paintDuration(Painter &p);
	void computeLayout(int newWidth);
	void updateAccessibleName();

	OnlineSession _session;
	bool _active = false;
	QString _startLabel;
	QString _endLabel;
	QString _startValue;
	QString _endValue;
	QString _durationText;
	QString _durationElided;
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
	EventCard(QWidget *parent, const TrackedEvent &event, bool expanded);
	~EventCard();

	[[nodiscard]] rpl::producer<> toggleRequests() const {
		return _toggles.events();
	}
	[[nodiscard]] rpl::producer<> openRequests() const {
		return _opens.events();
	}

protected:
	int resizeGetHeight(int newWidth) override;
	void paintCard(Painter &p) override;

private:
	struct Detail {
		QString heading;
		Ui::FlatLabel *value = nullptr;
		QRect headingRect;
		QRect background;
	};
	void toggleExpanded();

	QString _chipLabel;
	QString _dateText;
	style::FlatLabel _detailStyle;
	Ui::FlatLabel *_summary = nullptr;
	Ui::FlatLabel *_context = nullptr;
	Ui::FlatLabel *_note = nullptr;
	LuxuryUi::ActionButton *_expand = nullptr;
	LuxuryUi::ActionButton *_open = nullptr;
	std::vector<Detail> _details;
	bool _expanded = false;
	rpl::event_stream<> _toggles;
	rpl::event_stream<> _opens;
	int _kind = 0;
	int _rowHeight = 0;
	QString _chipElided;
	QString _dateElided;

};

class WatcherBody : public Ui::RpWidget {
public:
	WatcherBody(QWidget *parent, not_null<PeerData*> peer);

	void setTab(Tab tab);
	void setOrder(Order order);
	void refresh();
	void tick();
	[[nodiscard]] rpl::producer<> rebuilds() const {
		return _rebuilds.events();
	}
	[[nodiscard]] rpl::producer<bool> reloads() const {
		return _reloads.events();
	}
	[[nodiscard]] bool rebuildPending() const {
		return _rebuildPending;
	}
	[[nodiscard]] rpl::producer<ID> openMessages() const {
		return _openMessages.events();
	}
	[[nodiscard]] rpl::producer<std::array<int, 3>> countsChanged() const {
		return _counts.events();
	}
	// Counts describe the loaded window, including rows revealed by Show more.
	// The box reads the first state before subscribing to countsChanged().
	[[nodiscard]] std::array<int, 3> counts() const {
		const auto sessions = int(_sessions.size());
		const auto events = int(sortedEvents().size());
		return { sessions + events, sessions, events };
	}

protected:
	int resizeGetHeight(int newWidth) override;

private:
	void reload();
	void rebuild();
	[[nodiscard]] bool sessionActive(const OnlineSession &session) const;
	[[nodiscard]] std::vector<OnlineSession> sortedSessions() const;
	[[nodiscard]] std::vector<TrackedEvent> sortedEvents() const;

	void refreshEarlier();

	not_null<PeerData*> _peer;
	std::vector<OnlineEvent> _events;
	std::vector<WatchEvent> _watches;
	std::vector<LuxuryMessageBase> _deleted;
	std::vector<LuxuryMessageBase> _edits;
	std::vector<OnlineSession> _sessions;
	std::set<std::pair<int, ID>> _expandedEvents;
	bool _loading = false;
	bool _reloadAgain = false;
	bool _rebuildPending = false;
	Tab _tab = Tab::All;
	Order _order = Order::Newest;
	int _sessionOverflow = 0;
	int _eventOverflow = 0;
	Ui::FlatLabel *_sessionsTitle = nullptr;
	Ui::FlatLabel *_eventsTitle = nullptr;
	LuxuryUi::EmptyBlock *_sessionEmpty = nullptr;
	LuxuryUi::EmptyBlock *_eventEmpty = nullptr;
	LuxuryUi::ActionButton *_earlier = nullptr;
	int _sessionLimit = kMaxOnlineHistoryRows;
	int _eventLimit = kMaxWatchEventRows;
	rpl::event_stream<ID> _openMessages;
	std::vector<SessionCard*> _sessionCards;
	std::vector<EventCard*> _eventCards;
	rpl::event_stream<std::array<int, 3>> _counts;
	rpl::event_stream<bool> _reloads;
	rpl::event_stream<> _rebuilds;

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
	void updateContext(not_null<PeerData*> peer, bool updatesDeferred);

protected:
	void paintEvent(QPaintEvent *e) override;
	int resizeGetHeight(int newWidth) override;

private:
	Ui::FlatLabel *_identity = nullptr;
	Ui::FlatLabel *_status = nullptr;

	LuxuryUi::TabsBar *_tabs = nullptr;
	WatcherToolbar *_toolbar = nullptr;

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
	_refresh->setAccessibleName(tr::luxury_OnlineHistoryRefresh(tr::now));
	_settings = Ui::CreateChild<LuxuryUi::ToolButton>(
		this,
		LuxuryUi::ToolGlyph::Gear);
	_settings->setToolTip(tr::luxury_OnlineHistorySettings(tr::now));
	_settings->setAccessibleName(tr::luxury_OnlineHistorySettings(tr::now));
}

void WatcherToolbar::setSortIndex(int index) {
	_sort->setIndex(index);
}

void WatcherToolbar::setLastEnabled(bool enabled) {
	_sort->setLastEnabled(enabled);
	resizeToWidth(width());
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

WatcherCard::WatcherCard(QWidget *parent)
: RpWidget(parent) {
}

void WatcherCard::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	auto hq = PainterHighQualityEnabler(p);
	paintCard(p);
}

SessionCard::SessionCard(
		QWidget *parent,
		const OnlineSession &session,
		bool active)
: WatcherCard(parent)
, _session(session)
, _active(active) {
	_startLabel = tr::luxury_OnlineHistoryStart(tr::now) + u":"_q;
	_endLabel = tr::luxury_OnlineHistoryEnd(tr::now) + u":"_q;
	if (_session.start) {
		_startValue = CompactTimestamp(*_session.start);
	} else {
		_startValue = tr::luxury_OnlineHistoryUnknown(tr::now);
	}
	if (!_session.end) {
		_endValue = _active
			? tr::luxury_OnlineHistoryOpen(tr::now)
			: tr::luxury_WatcherEndUnobserved(tr::now);
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
	} else if (_active && _session.start) {
		_durationText = Ui::FormatDurationText(std::max<qint64>(
			0,
			base::unixtime::now() - qint64(*_session.start)));
	}
	_pulse.init([=](crl::time now) {
		_pulsePhase = (now % kPulseDuration) / float64(kPulseDuration);
		update();
		return true;
	});
	if (_active) {
		_pulse.start();
	}
	setToolTip(_startLabel + u" "_q + _startValue
		+ u"\n"_q + _endLabel + u" "_q + _endValue);
	updateAccessibleName();
}

void SessionCard::updateAccessibleName() {
	// The card paints its lines instead of hosting child widgets, so the
	// painted content needs mirroring in the accessible name.
	auto name = _startLabel + u" "_q + _startValue
		+ u", "_q + _endLabel + u" "_q + _endValue;
	if (!_durationText.isEmpty()) {
		name += u", "_q + _durationText;
	}
	setAccessibleName(name);
}

void SessionCard::setActive(bool active) {
	if (_active == active) {
		return;
	}
	_active = active;
	_endValue = active
		? tr::luxury_OnlineHistoryOpen(tr::now)
		: tr::luxury_WatcherEndUnobserved(tr::now);
	if (active) {
		_pulse.start();
		tick();
	} else {
		_pulse.stop();
		_durationText = QString();
		computeLayout(width());
		update();
	}
	setToolTip(_startLabel + u" "_q + _startValue
		+ u"\n"_q + _endLabel + u" "_q + _endValue);
	updateAccessibleName();
	resizeToWidth(width());
}

void SessionCard::tick() {
	if (!_active || !_session.start) {
		return;
	}
	_durationText = Ui::FormatDurationText(std::max<qint64>(
		0,
		base::unixtime::now() - qint64(*_session.start)));
	computeLayout(width());
	update();
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
			- st::luxuryWatcherCardPadding.right());
	_pillWidth = std::min(_pillWidth, _lineWidth);
	_durationElided = font->elided(_durationText,
		std::max(0, _pillWidth - st::luxuryWatcherClockSize
			- 2 * st::luxuryWatcherPillPadding.left()
			- st::luxuryWatcherPillPadding.right()));
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
	const auto pillHeight = _pillWidth
		? st::luxuryWatcherChipFont->height
			+ st::luxuryWatcherPillPadding.top()
			+ st::luxuryWatcherPillPadding.bottom()
			+ st::luxuryWatcherDetailSkip
		: 0;
	return st::luxuryWatcherCardPadding.top()
		+ 2 * st::luxuryWatcherValueFont->height
		+ st::luxuryWatcherMetaSkip + pillHeight
		+ st::luxuryWatcherCardPadding.bottom();
}

void SessionCard::paintCard(Painter &p) {
	PaintCardShell(p, width(), height());
	paintStatus(p);

	const auto &labelFont = st::luxuryWatcherLabelFont;
	const auto &valueFont = st::luxuryWatcherValueFont;
	const auto lineH = valueFont->height;
	auto top = st::luxuryWatcherCardPadding.top();
	for (auto line = 0; line != 2; ++line) {
		const auto &label = line ? _endLabel : _startLabel;
		const auto &value = line ? _endValueElided : _startValueElided;
		// Only the End line of an open session carries the accent.
		const auto valuePen = (!line || !_active)
			? QPen(st::windowFg->c)
			: QPen(LuxuryUi::AccentColor());
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
		top += lineH + st::luxuryWatcherMetaSkip;
	}
	paintDuration(p);
}

void SessionCard::paintStatus(Painter &p) {
	const auto dot = st::luxuryWatcherDotSize;
	const auto rect = style::rtlrect(
		st::luxuryWatcherCardPadding.left(),
		st::luxuryWatcherCardPadding.top()
			+ (st::luxuryWatcherValueFont->height - dot) / 2,
		dot,
		dot,
		width());
	p.setPen(Qt::NoPen);
	if (_active) {
		// Open: an accent dot with a halo that grows and fades on a sine
		// loop -- the live "online right now" beacon.
		const auto accent = LuxuryUi::AccentColor();
		const auto eased = 0.5 - 0.5 * std::cos(2 * kPi * _pulsePhase);
		const auto halo = st::luxuryWatcherDotSize
			+ (st::luxuryWatcherPulseDiameter - st::luxuryWatcherDotSize)
				* eased;
		const auto center = QPointF(
			rect.x() + rect.width() / 2.,
			rect.y() + rect.height() / 2.);
		p.setBrush(LuxuryUi::WithAlpha(accent, kPulseHaloAlpha * (1. - eased)));
		p.drawEllipse(center, halo / 2., halo / 2.);
		p.setBrush(accent);
		p.drawEllipse(QRectF(rect));
	} else if (!_session.start || !_session.end) {
		// Incomplete interval: an outlined circle with a question mark.
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
		// Closed normally: the theme's online-status color, without a halo.
		p.setBrush(st::luxuryWatcherOkFg->c);
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
		height() - st::luxuryWatcherCardPadding.bottom() - pillH,
		_pillWidth,
		pillH,
		width());
	const auto accent = LuxuryUi::AccentColor();
	p.setPen(Qt::NoPen);
	p.setBrush(LuxuryUi::WithAlpha(accent, kDurationPillAlpha));
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
		accent);
	p.setFont(font);
	p.setPen(accent);
	p.drawText(
		QPointF(
			clockX + clock + st::luxuryWatcherPillPadding.left(),
			pillRect.y() + (pillH - font->height) / 2 + font->ascent),
		_durationElided);
}

EventCard::EventCard(
		QWidget *parent,
		const TrackedEvent &event,
		bool expanded)
: WatcherCard(parent)
, _detailStyle(st::luxuryWatcherValueLabel)
, _expanded(expanded)
, _kind(event.kind) {
	_chipLabel = ChipLabelFor(_kind);
	_dateText = CompactTimestamp(event.at);
	setToolTip(QLocale().toString(base::unixtime::parse(event.at), QLocale::ShortFormat));
	_summary = Ui::CreateChild<Ui::FlatLabel>(
		this,
		event.summary,
		st::luxuryWatcherSummaryLabel);
	_summary->setSelectable(true);
	_context = Ui::CreateChild<Ui::FlatLabel>(
		this,
		event.context,
		st::luxuryWatcherMetaLabel);
	_context->setSelectable(true);
	_context->setVisible(!event.context.isEmpty());
	_note = Ui::CreateChild<Ui::FlatLabel>(
		this,
		event.note,
		st::luxuryWatcherMetaLabel);
	_note->setVisible(!event.note.isEmpty());
	for (const auto &detail : event.details) {
		const auto label = Ui::CreateChild<Ui::FlatLabel>(
			this,
			detail.text.isEmpty()
				? tr::luxury_WatcherEmptyValue(tr::now)
				: detail.text,
			_detailStyle);
		label->setSelectable(true);
		_details.push_back({ detail.label, label });
	}
	_expand = Ui::CreateChild<LuxuryUi::ActionButton>(
		this,
		tr::luxury_WatcherExpand(tr::now));
	_expand->setClickedCallback([=] { toggleExpanded(); });
	_expand->hide();
	_open = Ui::CreateChild<LuxuryUi::ActionButton>(
		this,
		tr::luxury_WatcherOpenMessage(tr::now));
	_open->setVisible(event.messageId != 0 && _kind != kKindDeleted);
	_open->setClickedCallback([=] { _opens.fire({}); });
}

EventCard::~EventCard() {
	// FlatLabel keeps a style reference; destroy these children while
	// the card's mutable style is still alive, not in QWidget's destructor.
	for (const auto &detail : _details) {
		delete detail.value;
	}
}

void EventCard::toggleExpanded() {
	_expanded = !_expanded;
	_expand->setText(_expanded
		? tr::luxury_WatcherCollapse(tr::now)
		: tr::luxury_WatcherExpand(tr::now));
	_toggles.fire({});
}

int EventCard::resizeGetHeight(int newWidth) {
	const auto &padding = st::luxuryWatcherCardPadding;
	const auto contentWidth = std::max(1,
		newWidth - padding.left() - padding.right());
	const auto &font = st::luxuryWatcherLabelFont;
	_rowHeight = std::max(font->height, st::luxuryWatcherChipFont->height);
	const auto dateWidth = std::min(font->width(_dateText), contentWidth / 2);
	_dateElided = font->elided(_dateText, dateWidth);
	_chipElided = st::luxuryWatcherChipFont->elided(
		_chipLabel,
		std::max(0, contentWidth - dateWidth - st::luxuryWatcherDetailSkip));
	auto y = padding.top() + _rowHeight + st::luxuryWatcherDetailSkip;
	_summary->resizeToWidth(contentWidth);
	_summary->moveToLeft(padding.left(), y, newWidth);
	y += _summary->height();
	if (!_context->isHidden()) {
		y += st::luxuryWatcherMetaSkip;
		_context->resizeToWidth(contentWidth);
		_context->moveToLeft(padding.left(), y, newWidth);
		y += _context->height();
	}
	auto expandable = false;
	for (auto &detail : _details) {
		y += st::luxuryWatcherDetailSkip;
		const auto top = y;
		const auto inset = st::luxuryWatcherDetailPadding;
		const auto textWidth = std::max(1, contentWidth - 2 * inset);
		detail.headingRect = style::rtlrect(
			padding.left() + inset,
			y + inset,
			textWidth,
			font->height,
			newWidth);
		y += inset + font->height + st::luxuryWatcherMetaSkip;
		_detailStyle.maxHeight = 0;
		detail.value->resizeToWidth(textWidth);
		const auto fullHeight = detail.value->height();
		const auto lineHeight = std::max(_detailStyle.style.lineHeight,
			_detailStyle.style.font->height);
		const auto previewHeight = std::max(1,
			st::luxuryWatcherPreviewHeight / lineHeight) * lineHeight;
		expandable = expandable || fullHeight > previewHeight;
		_detailStyle.maxHeight = _expanded ? 0 : previewHeight;
		detail.value->resizeToWidth(textWidth);
		detail.value->moveToLeft(padding.left() + inset, y, newWidth);
		y += detail.value->height() + inset;
		detail.background = style::rtlrect(
			padding.left(), top, contentWidth, y - top, newWidth);
	}
	if (!_note->isHidden()) {
		y += st::luxuryWatcherDetailSkip;
		_note->resizeToWidth(contentWidth);
		_note->moveToLeft(padding.left(), y, newWidth);
		y += _note->height();
	}
	_expand->setText(_expanded
		? tr::luxury_WatcherCollapse(tr::now)
		: tr::luxury_WatcherExpand(tr::now));
	_expand->setVisible(expandable);
	if (expandable || !_open->isHidden()) {
		y += st::luxuryWatcherDetailSkip;
		if (expandable) {
			_expand->moveToLeft(padding.left(), y, newWidth);
		}
		if (!_open->isHidden()) {
			if (expandable
				&& _expand->width() + _open->width()
					+ st::luxuryWatcherDetailSkip > contentWidth) {
				y += _expand->height() + st::luxuryWatcherMetaSkip;
			}
			_open->moveToRight(padding.right(), y, newWidth);
		}
		y += std::max(_expand->isHidden() ? 0 : _expand->height(),
			_open->isHidden() ? 0 : _open->height());
	}
	return y + padding.bottom();
}

void EventCard::paintCard(Painter &p) {
	PaintCardShell(p, width(), height());
	const auto &padding = st::luxuryWatcherCardPadding;
	const auto &font = st::luxuryWatcherLabelFont;
	const auto dateWidth = font->width(_dateElided);
	const auto date = style::rtlrect(
		width() - padding.right() - dateWidth,
		padding.top(), dateWidth, _rowHeight, width());
	p.setFont(font);
	p.setPen(st::windowSubTextFg);
	p.drawText(QPointF(date.x(), date.y() + font->ascent), _dateElided);
	const auto chip = style::rtlrect(
		padding.left(), padding.top(),
		st::luxuryWatcherChipFont->width(_chipElided), _rowHeight, width());
	p.setFont(st::luxuryWatcherChipFont);
	// Category text carries the meaning; color is a quiet secondary cue.
	p.setPen(st::windowSubTextFg);
	p.drawText(QPointF(chip.x(), chip.y()
		+ st::luxuryWatcherChipFont->ascent), _chipElided);
	for (const auto &detail : _details) {
		p.setPen(Qt::NoPen);
		p.setBrush(LuxuryUi::WithAlpha(st::windowFg->c, 0.035));
		p.drawRoundedRect(detail.background, st::luxuryWatcherDetailRadius,
			st::luxuryWatcherDetailRadius);
		p.setFont(font);
		p.setPen(st::windowSubTextFg);
		p.drawText(QPointF(detail.headingRect.x(), detail.headingRect.y()
			+ font->ascent), font->elided(detail.heading,
				detail.headingRect.width()));
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
	_earlier = Ui::CreateChild<LuxuryUi::ActionButton>(
		this,
		tr::luxury_WatcherShowMore(tr::now));
	_earlier->setClickedCallback([=] {
		_sessionLimit += kMoreRows;
		_eventLimit += kMoreRows;
		rebuild();
	});
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
}

bool WatcherBody::sessionActive(const OnlineSession &session) const {
	const auto user = _peer->asUser();
	return user
		&& !session.end
		&& session.start
		&& !_events.empty()
		&& _events.front().online
		&& session.startId == _events.front().fakeId
		&& LuxuryOnline::TrackingAllowed()
		&& user->trackedOnlineStart() == session.start
		&& user->serverLastseen().isOnline(base::unixtime::now());
}

void WatcherBody::tick() {
	// A deferred rebuild waits for interaction to end, so swapping the
	// list never interrupts a press or a text selection.
	if (_rebuildPending
		&& !QApplication::mouseButtons()
		&& !isAncestorOf(QApplication::focusWidget())) {
		_rebuildPending = false;
		rebuild();
		_counts.fire(counts());
		_reloads.fire_copy(true);
	}
	for (const auto card : _sessionCards) {
		card->setActive(sessionActive(card->session()));
		card->tick();
	}
	if (width() > 0) {
		resizeToWidth(width());
	}
}

void WatcherBody::reload() {
	if (_loading) {
		_reloadAgain = true;
		return;
	}
	_loading = true;
	const auto userId = static_cast<ID>(_peer->session().uniqueId());
	const auto dialogId = getDialogIdFromPeer(_peer);
	const auto weak = base::make_weak(this);
	LuxuryDatabase::async([=] {
		auto events = LuxuryDatabase::getOnlineEvents(
			userId,
			dialogId,
			kHistoryReadLimit);
		auto watches = LuxuryDatabase::getWatchEvents(
			userId,
			dialogId,
			kHistoryReadLimit);
		auto deleted = LuxuryMessages::getDeletedMessages(
			userId,
			dialogId,
			0,
			0,
			0,
			kHistoryReadLimit);
		auto edits = LuxuryMessages::getEditedMessagesForDialog(
			userId,
			dialogId,
			kHistoryReadLimit);
		crl::on_main([=,
				events = std::move(events),
				watches = std::move(watches),
				deleted = std::move(deleted),
				edits = std::move(edits)]() mutable {
			if (!weak) {
				return;
			}
			_loading = false;
			using LuxuryFeatures::Watch::SameRecordIds;
			const auto changed = !SameRecordIds(_events, events)
				|| !SameRecordIds(_watches, watches)
				|| !SameRecordIds(_deleted, deleted)
				|| !SameRecordIds(_edits, edits);
			// Fresh data always lands; only the widget swap can wait,
			// because rebuilding mid-press yanks the row being pressed.
			const auto defer = changed && (QApplication::mouseButtons()
				|| isAncestorOf(QApplication::focusWidget()));
			_events = std::move(events);
			_watches = std::move(watches);
			_deleted = std::move(deleted);
			_edits = std::move(edits);
			_sessions = PairOnlineSessions(_events);
			const auto rebuilt = !defer && (changed
				|| (_sessionCards.empty() && _eventCards.empty()));
			if (defer) {
				_rebuildPending = true;
			} else if (rebuilt) {
				rebuild();
			} else {
				tick();
			}
			_counts.fire(counts());
			_reloads.fire_copy(rebuilt);
			if (_reloadAgain) {
				_reloadAgain = false;
				reload();
			}
		});
	});
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
				? std::max<qint64>(0, qint64(*a.end) - qint64(*a.start))
				: -1;
			const auto right = b.start && b.end
				? std::max<qint64>(0, qint64(*b.end) - qint64(*b.start))
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
	auto recordedEdits = std::set<std::tuple<ID, int, std::string>>();
	for (const auto &watch : _watches) {
		list.push_back(WatchEventDetails(watch, _peer));
		if (watch.kind == static_cast<int>(WatchKind::MessageEdited)) {
			recordedEdits.emplace(watch.messageId, watch.at, watch.title);
		}
	}
	for (const auto &deleted : _deleted) {
		auto event = TrackedEvent();
		event.at = MessageTimestamp(deleted);
		event.rowId = deleted.fakeId;
		event.kind = kKindDeleted;
		event.summary = tr::luxury_WatcherMessageDeleted(tr::now);
		event.context = MessageContext(_peer, deleted.messageId, deleted.fromId);
		event.details.push_back({
			tr::luxury_WatcherMessageText(tr::now), MessageText(deleted) });
		list.push_back(std::move(event));
	}
	const auto afterStates = RevisionAfterStates(_edits);
	for (auto i = std::size_t(0); i != _edits.size(); ++i) {
		const auto &edited = _edits[i];
		const auto at = edited.entityCreateDate
			? edited.entityCreateDate : MessageTimestamp(edited);
		if (recordedEdits.contains({ edited.messageId, at, edited.text })) {
			continue;
		}
		auto event = TrackedEvent();
		event.at = at;
		event.rowId = edited.fakeId;
		event.kind = kKindEdited;
		event.messageId = edited.messageId;
		event.summary = tr::luxury_WatcherMessageEdited(tr::now);
		event.context = MessageContext(_peer, edited.messageId, edited.fromId);
		event.details.push_back({ tr::luxury_WatchEditWas(tr::now),
			MessageText(edited) });
		if (const auto after = afterStates[i]) {
			event.details.push_back({ tr::luxury_WatcherNextSavedText(tr::now),
				MessageText(*after) });
		} else {
			const auto current = _peer->session().data().message(
				_peer, MsgId(edited.messageId));
			if (current) {
				event.details.push_back({ tr::luxury_WatcherCurrentText(tr::now),
					current->originalText().text });
			} else {
				event.note = tr::luxury_WatcherRevisionUnavailable(tr::now);
			}
		}
		list.push_back(std::move(event));
	}
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
		_earlier->setText(tr::luxury_WatcherShowMore(tr::now));
	}
}

void WatcherBody::rebuild() {
	_rebuilds.fire({});
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
	std::erase_if(_expandedEvents, [&](const auto &key) {
		return std::none_of(eventList.begin(), eventList.end(), [&](const auto &event) {
			return key == std::pair(event.kind, event.rowId);
		});
	});
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
			_sessionEmpty->setText(_loading
				? tr::luxury_WatcherLoading(tr::now)
				: tr::luxury_OnlineHistorySessionsEmpty(tr::now));
			_sessionEmpty->show();
		} else {
			_sessionEmpty->hide();
			const auto total = int(sessionList.size());
			const auto shown = std::min(total, _sessionLimit);
			_sessionOverflow = total - shown;
			for (auto i = 0; i != shown; ++i) {
				_sessionCards.push_back(Ui::CreateChild<SessionCard>(
					this,
					sessionList[i],
					sessionActive(sessionList[i])));
			}
		}
	} else {
		_sessionEmpty->hide();
	}
	if (eventsVisible) {
		_eventsTitle->setText(
			tr::luxury_OnlineHistoryEvents(tr::now)
			+ u" · "_q
			+ QString::number(eventList.size()));
		if (eventList.empty()) {
			_eventEmpty->setText(_loading
				? tr::luxury_WatcherLoading(tr::now)
				: tr::luxury_OnlineHistoryEventsEmpty(tr::now));
			_eventEmpty->show();
		} else {
			_eventEmpty->hide();
			const auto total = int(eventList.size());
			const auto shown = std::min(total, _eventLimit);
			_eventOverflow = total - shown;
			for (auto i = 0; i != shown; ++i) {
				const auto key = std::pair(eventList[i].kind, eventList[i].rowId);
				const auto card = Ui::CreateChild<EventCard>(
					this,
					eventList[i],
					_expandedEvents.contains(key));
				_eventCards.push_back(card);
				const auto messageId = eventList[i].messageId;
				card->openRequests(
				) | rpl::on_next([=] {
					_openMessages.fire_copy(messageId);
				}, card->lifetime());
				// An expanding card changes its own height: re-run the
				// layout so the cards below it move out of the way.
				card->toggleRequests(
				) | rpl::on_next([=] {
					if (!_expandedEvents.erase(key)) {
						_expandedEvents.insert(key);
					}
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
		_earlier->moveToLeft(
			left,
			y + st::defaultSubsectionTitlePadding.top(),
			newWidth);
		y += st::defaultSubsectionTitlePadding.top() + _earlier->height();
	}
	return y + st::luxuryWatcherDetailSkip;
}

WatcherHeader::WatcherHeader(QWidget *parent)
: RpWidget(parent) {
	_identity = Ui::CreateChild<Ui::FlatLabel>(
		this,
		QString(),
		st::luxuryWatcherIdentityLabel);
	_identity->setSelectable(true);
	_status = Ui::CreateChild<Ui::FlatLabel>(
		this, QString(), st::luxuryWatcherMetaLabel);
	_tabs = Ui::CreateChild<LuxuryUi::TabsBar>(
		this,
		std::array<QString, 3>{
			tr::luxury_OnlineHistorySortAll(tr::now),
			tr::luxury_OnlineHistorySortSessions(tr::now),
			tr::luxury_OnlineHistorySortEvents(tr::now),
		});
	_toolbar = Ui::CreateChild<WatcherToolbar>(this);
	resizeToWidth(width());
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
	// The pinned header stays opaque above the scrolling list.
	p.fillRect(rect(), st::boxBg);
	p.fillRect(
		0,
		height() - st::lineWidth,
		width(),
		st::lineWidth,
		LuxuryUi::WithAlpha(st::windowFg->c, 0.08));
}

void WatcherHeader::updateContext(
		not_null<PeerData*> peer,
		bool updatesDeferred) {
	const auto name = peer->name();
	_identity->setText(name);
	_identity->setToolTip(name);
	if (LuxuryOnline::TrackingAllowed()) {
		auto status = tr::luxury_WatcherTrackingHint(tr::now);
		if (updatesDeferred) {
			status += QChar(' ');
			status += tr::luxury_WatcherUpdatesPaused(tr::now);
		}
		_status->setText(std::move(status));
	} else {
		_status->setText(tr::luxury_WatcherTrackingPaused(tr::now));
	}
	resizeToWidth(width());
}

int WatcherHeader::resizeGetHeight(int newWidth) {
	const auto left = st::boxRowPadding.left();
	const auto width = std::max(1, newWidth - left - st::boxRowPadding.right());
	auto y = st::luxuryWatcherHeaderSkip;
	_identity->resizeToWidth(width);
	_identity->moveToLeft(left, y, newWidth);
	y += _identity->height() + st::luxuryWatcherMetaSkip;
	_status->resizeToWidth(width);
	_status->moveToLeft(left, y, newWidth);
	y += _status->height() + st::luxuryWatcherHeaderSkip;
	_tabs->setGeometryToLeft(left, y, width, st::luxuryWatcherTabHeight, newWidth);
	y += st::luxuryWatcherTabHeight + st::luxuryWatcherHeaderSkip;
	_toolbar->resizeToWidth(width);
	_toolbar->moveToLeft(left, y, newWidth);
	return y + _toolbar->height() + st::luxuryWatcherHeaderSkip;
}

void FillWatcherBox(
		not_null<Ui::GenericBox*> box,
		not_null<PeerData*> peer,
		not_null<Window::SessionController*> controller) {
	struct State {
		Tab tab = Tab::All;
		Order order = Order::Newest;
		base::unique_qptr<Ui::PopupMenu> menu;
		base::Timer refreshTimer;
		base::Timer clockTimer;
		int scrollTop = 0;
	};
	const auto state = box->lifetime().make_state<State>();
	box->setTitle(tr::luxury_OnlineHistoryTitle());
	box->setWidth(st::luxuryWatcherWidth);
	box->setMinHeight(st::luxuryWatcherViewHeight);
	box->setMaxHeight(st::luxuryWatcherViewHeight);

	// GenericBox owns the only scroll area and adapts it to the window;
	// its pinned content stays outside that area's viewport.
	const auto header = box->setPinnedToTopContent(
		object_ptr<WatcherHeader>(box));
	const auto body = box->addRow(
		object_ptr<WatcherBody>(box, peer),
		style::margins());

	header->updateContext(peer, body->rebuildPending());
	const auto refresh = [=] {
		state->scrollTop = box->scrollTop();
		body->refresh();
	};
	body->rebuilds(
	) | rpl::on_next([=] { state->scrollTop = box->scrollTop(); }, box->lifetime());
	body->reloads(
	) | rpl::on_next([=](bool rebuilt) {
		header->updateContext(peer, body->rebuildPending());
		if (rebuilt) {
			box->scrollToY(state->scrollTop);
		}
	}, box->lifetime());
	state->refreshTimer.setCallback([=] {
		// Do not replace selectable widgets while the user is interacting.
		if (!box->isVisible() || state->menu || QApplication::mouseButtons()
			|| body->isAncestorOf(QApplication::focusWidget())) {
			return;
		}
		refresh();
	});
	state->refreshTimer.callEach(kRefreshInterval);
	state->clockTimer.setCallback([=] {
		if (box->isVisible() && !QApplication::mouseButtons()) {
			body->tick();
		}
	});
	state->clockTimer.callEach(1000);
	LuxurySettings::getInstance().trackOnlineHistoryChanges(
	) | rpl::on_next([=] { header->updateContext(peer, body->rebuildPending()); body->tick(); }, box->lifetime());
	LuxurySettings::getInstance().trackOnlineEvenWhenLockedChanges(
	) | rpl::on_next([=] { header->updateContext(peer, body->rebuildPending()); body->tick(); }, box->lifetime());

	header->tabChanges(
	) | rpl::on_next([=](int index) {
		state->tab = static_cast<Tab>(index);
		// Longest is a session sort, not an event sort.
		header->setLastEnabled(state->tab != Tab::Events);
		if (state->tab == Tab::Events && state->order == Order::Longest) {
			state->order = Order::Newest;
			body->setOrder(Order::Newest);
			header->setSortIndex(static_cast<int>(Order::Newest));
		}
		body->setTab(state->tab);
		state->scrollTop = 0;
		box->scrollToY(0);
	}, box->lifetime());

	header->sortChanges(
	) | rpl::on_next([=](int index) {
		if (state->tab == Tab::Events
			&& static_cast<Order>(index) == Order::Longest) {
			header->setSortIndex(static_cast<int>(state->order));
			return;
		}
		state->order = static_cast<Order>(index);
		body->setOrder(state->order);
		state->scrollTop = 0;
		box->scrollToY(0);
	}, box->lifetime());

	header->refreshClicks(
	) | rpl::on_next([=] {
		header->startRefreshSpin();
		refresh();
	}, box->lifetime());

	header->settingsClicks(
	) | rpl::on_next([=] {
		state->menu = base::make_unique_q<Ui::PopupMenu>(
			header,
			st::luxuryWatcherSettingsMenu);
		const auto addToggle = [&](
				const QString &label,
				Fn<bool()> getter,
				Fn<void(bool)> setter) {
			const auto value = getter();
			const auto action = state->menu->addAction(base::make_unique_q<Ui::Menu::Toggle>(
				state->menu->menu(),
				state->menu->st().menu,
				label,
				[=] { setter(!getter()); },
				nullptr,
				nullptr));
			action->setCheckable(true);
			action->setChecked(value);
		};
		// LuxurySettings is a singleton with a deleted copy constructor:
		// resolve it inside the handler, never capture a reference.
		addToggle(
			tr::luxury_TrackOnlineHistory(tr::now),
			[] { return LuxurySettings::getInstance().trackOnlineHistory(); },
			[](bool value) {
				LuxurySettings::getInstance().setTrackOnlineHistory(value);
			});
		addToggle(
			tr::luxury_TrackOnlineEvenWhenLocked(tr::now),
			[] { return LuxurySettings::getInstance().trackOnlineEvenWhenLocked(); },
			[](bool value) {
				LuxurySettings::getInstance().setTrackOnlineEvenWhenLocked(value);
			});
		const auto gear = header->gearButton();
		state->menu->popup(gear->mapToGlobal(QPoint(0, gear->height())));
	}, box->lifetime());

	body->countsChanged(
	) | rpl::on_next([=](std::array<int, 3> counts) {
		header->setTabCounts(counts);
	}, box->lifetime());
	header->setTabCounts(body->counts());
	body->openMessages(
	) | rpl::on_next([=](ID id) {
		controller->showPeerHistory(peer, {}, MsgId(id));
	}, box->lifetime());

	box->addButton(tr::lng_close(), [=] { box->closeBox(); });
}

} // namespace

void Show(
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer) {
	controller->show(Box(FillWatcherBox, peer, controller));
}

} // namespace LuxuryWatcher
