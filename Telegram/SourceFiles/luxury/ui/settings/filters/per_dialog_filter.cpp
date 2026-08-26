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
#include "styles/style_menu_icons.h"
#include "ui/painter.h"
#include "window/window_session_controller.h"

#include <utility>

namespace Settings {

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
	return QString("UNKNOWN (ID: %1)").arg(QString::number(peerId.value & PeerId::kChatTypeMask));
}

PaintRoundImageCallback PerDialogFiltersListRow::generatePaintUserpicCallback(bool forceRound) {
	if (const auto from = getPeerFromDialogId(peerId.value & PeerId::kChatTypeMask)) {
		this->setPeer(from);
		return PeerListRow::generatePaintUserpicCallback(forceRound);
	}

	return [=](Painter &p, int x, int y, int outerWidth, int size) mutable
	{
		using namespace Ui;
		const auto realId = peerId.value & PeerId::kChatTypeMask;
		auto _userpicEmpty = std::make_unique<EmptyUserpic>(
			EmptyUserpic::UserpicColor(realId % 7),
			QString("U")); // U - Unknown
		_userpicEmpty->paintCircle(p, x, y, outerWidth, size);
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
	// being built. Append the rows when they come back instead.
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
	if (filters.empty() && exclusions.empty()) {
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
		auto _contextMenu = new Ui::PopupMenu(nullptr, st::popupMenuWithIcons);
		_contextMenu->setAttribute(Qt::WA_DeleteOnClose);

		_contextMenu->addAction(
			tr::lng_theme_delete(tr::now),
			[=]
			{
				if (LuxurySettings::getInstance().isShadowBanned(did)) {
					LuxurySettings::getInstance().removeShadowBan(did);
				} else {
					LuxurySettings::getInstance().addShadowBan(did);
				}
			},
			&st::menuIconDelete);

		_contextMenu->popup(QCursor::pos());
		return;
	} else if (_mode == Mode::Watched) {
		auto menu = new Ui::PopupMenu(nullptr, st::popupMenuWithIcons);
		menu->setAttribute(Qt::WA_DeleteOnClose);

		menu->addAction(
			tr::luxury_WatchChatStop(tr::now),
			crl::guard(this, [=] {
				LuxurySettings::getInstance().setWatched(did, false);
				// The row is the only thing pointing at that chat here, so it goes
				// with the setting: leaving it would say the chat is still watched.
				if (const auto row = delegate()->peerListFindRow(
						PeerListRowId(did))) {
					delegate()->peerListRemoveRow(row);
					delegate()->peerListRefreshRows();
				}
			}),
			&st::menuIconDelete);

		menu->popup(QCursor::pos());
		return;
	}
	_controller->luxuryFilters = {
		.dialogId = did,
		.showExclude = true,
	};
	_controller->showSettings(LuxuryFiltersList::Id());
}

}
