// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "luxury/ui/settings/filters/per_dialog_filter.h"

#include "lang_auto.h"
#include "luxury/luxury_settings.h"
#include "luxury/data/luxury_database.h"
#include "luxury/ui/settings/filters/settings_filters_list.h"
#include "luxury/utils/telegram_helpers.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "main/main_session.h"
#include "ui/painter.h"
#include "window/window_session_controller.h"

#include <utility>

namespace Settings {

namespace {

// Not a dialog id: the loading placeholder owns this row id so that
// rowClicked can tell it apart from every dialog-backed row.
constexpr auto kLoadingRowId = PeerListRowId(-1);

// A "special" row (no peer) standing in for the regex rows while the
// database read runs, so an existing list never reads as empty while it
// is merely not finished loading.
class LoadingRow final : public PeerListRow {
public:
	explicit LoadingRow(PeerListRowId id)
		: PeerListRow(id) {
		setDisabledState(State::Disabled);
	}

	QString generateName() override {
		return tr::luxury_RegexFiltersLoading(tr::now);
	}
};

} // namespace

PerDialogFiltersListRow::PerDialogFiltersListRow(ID dialogId)
	: PeerListRow(PeerListRowId(dialogId))
	  , _dialogId(dialogId)
	  , peerId(PeerId(PeerIdHelper(getBareDialogId(dialogId)))) {
}

ID PerDialogFiltersListRow::dialogId() const {
	return _dialogId;
}

QString PerDialogFiltersListRow::generateName() {
	if (const auto from = getPeerFromDialogId(peerId.value & PeerId::kChatTypeMask)) {
		this->setPeer(from);
		return PeerListRow::generateName();
	}
	return tr::luxury_UnknownPeer(
		tr::now,
		lt_id,
		QString::number(peerId.value & PeerId::kChatTypeMask));
}

PaintRoundImageCallback PerDialogFiltersListRow::generatePaintUserpicCallback(bool forceRound) {
	if (const auto from = getPeerFromDialogId(peerId.value & PeerId::kChatTypeMask)) {
		this->setPeer(from);
		return PeerListRow::generatePaintUserpicCallback(forceRound);
	}

	const auto name = tr::luxury_UnknownPeer(
		tr::now,
		lt_id,
		QString::number(peerId.value & PeerId::kChatTypeMask));
	return [=](Painter &p, int x, int y, int outerWidth, int size) mutable
	{
		using namespace Ui;
		const auto realId = peerId.value & PeerId::kChatTypeMask;
		auto userpicEmpty = std::make_unique<EmptyUserpic>(
			EmptyUserpic::UserpicColor(EmptyUserpic::ColorIndex(realId)),
			name);
		userpicEmpty->paintCircle(p, x, y, outerWidth, size);
	};
}

PerDialogFiltersListController::PerDialogFiltersListController(not_null<Main::Session*> session,
															   not_null<Window::SessionController*> controller,
															   Mode mode)
	: _session(session)
	  , _controller(controller)
	  , _mode(mode) {
}

Main::Session &PerDialogFiltersListController::session() const {
	return *_session;
}

void PerDialogFiltersListController::prepareFromSetting(
		const std::unordered_set<ID> &ids) {
	for (const auto id : ids) {
		delegate()->peerListAppendRow(std::make_unique<PerDialogFiltersListRow>(id));
	}
}

void PerDialogFiltersListController::prepare() {
	const auto &settings = LuxurySettings::getInstance();
	if (_mode == Mode::ShadowBan) {
		prepareFromSetting(settings.shadowBanIds());
		return;
	} else if (_mode == Mode::Watched) {
		prepareFromSetting(settings.watchedDialogs());
		return;
	}
	// Two full-table reads, and prepare() runs while the section it belongs to is
	// being built. Append the rows when they come back instead, and keep a
	// disabled placeholder until then: a credible empty state for that moment
	// invites acting on a filter list that is about to appear.
	delegate()->peerListAppendRow(std::make_unique<LoadingRow>(kLoadingRowId));
	const auto weak = base::make_weak(this);
	LuxuryDatabase::async([=] {
		auto filters = LuxuryDatabase::getAllRegexFilters();
		auto exclusions = LuxuryDatabase::getAllFiltersExclusions();
		crl::on_main([=,
				filters = std::move(filters),
				exclusions = std::move(exclusions)] {
			if (const auto strong = weak.get()) {
				strong->fillCounts(filters, exclusions);
			}
		});
	});
}

void PerDialogFiltersListController::fillCounts(
		const std::vector<RegexFilter> &filters,
		const std::vector<RegexFilterGlobalExclusion> &exclusions) {
	// The placeholder goes even when the read comes back empty: that is the
	// real state, not "still loading".
	if (const auto row = delegate()->peerListFindRow(kLoadingRowId)) {
		delegate()->peerListRemoveRow(row);
	}
	if (filters.empty() && exclusions.empty()) {
		delegate()->peerListRefreshRows();
		return;
	}

	countsByDialogIds.clear();

	for (const auto &filter : filters) {
		if (filter.dialogId.has_value()) {
			countsByDialogIds[*filter.dialogId].filters++;
		}
	}
	for (const auto &exclusion : exclusions) {
		countsByDialogIds[exclusion.dialogId].exclusions++;
	}

	for (const auto &[id, count] : countsByDialogIds) {
		auto row = std::make_unique<PerDialogFiltersListRow>(id);
		auto status = QString();
		if (count.filters > 0) {
			status += tr::luxury_RegexFiltersAmount(tr::now, lt_count, count.filters);
			if (count.exclusions > 0) {
				status += ", ";
			}
		}
		if (count.exclusions > 0) {
			status += tr::luxury_RegexFiltersExcludedAmount(tr::now, lt_count, count.exclusions);
		}

		row->setCustomStatus(status, false);

		delegate()->peerListAppendRow(std::move(row));
	}

	// sortByName();

	delegate()->peerListRefreshRows();
}

void PerDialogFiltersListController::rowClicked(not_null<PeerListRow*> peer) {
	if (peer->id() == kLoadingRowId) {
		return;
	}
	ID did;
	if (const auto row = dynamic_cast<PerDialogFiltersListRow*>(peer.get())) {
		did = row->dialogId();
	} else if (peer->special()) {
		const auto pred = static_cast<long long>(peer->id() & PeerId::kChatTypeMask);
		did = countsByDialogIds.contains(pred) ? pred : -pred;
	} else {
		did = getDialogIdFromPeer(peer->peer());
	}
	if (_mode == Mode::ShadowBan) {
		// Every row's only action is lifting that chat's ban, so activation
		// performs it directly. The old one-item popup was anchored at the
		// cursor, which a keyboard activation cannot aim at the row it
		// belongs to, and its label was a generic "Delete".
		LuxurySettings::getInstance().removeShadowBan(did);
		// The row is the only thing pointing at that chat here, so it goes
		// with the setting: leaving it would say the chat is still banned.
		if (const auto row = delegate()->peerListFindRow(PeerListRowId(did))) {
			delegate()->peerListRemoveRow(row);
			delegate()->peerListRefreshRows();
		}
		_controller->showToast(tr::luxury_ShadowBanDisabled(tr::now));
		return;
	} else if (_mode == Mode::Watched) {
		LuxurySettings::getInstance().setWatched(did, false);
		// The row is the only thing pointing at that chat here, so it goes
		// with the setting: leaving it would say the chat is still watched.
		if (const auto row = delegate()->peerListFindRow(PeerListRowId(did))) {
			delegate()->peerListRemoveRow(row);
			delegate()->peerListRefreshRows();
		}
		_controller->showToast(tr::luxury_WatchChatDisabled(tr::now));
		return;
	}
	_controller->luxuryFilters = {
		.dialogId = did,
		.showExclude = true,
	};
	_controller->showSettings(LuxuryFiltersList::Id());
}

}
