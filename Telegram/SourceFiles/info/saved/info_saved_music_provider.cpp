/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/saved/info_saved_music_provider.h"

#include "base/unixtime.h"
#include "core/application.h"
#include "data/data_changes.h"
#include "data/data_channel.h"
#include "data/data_document.h"
#include "data/data_media_types.h"
#include "data/data_saved_music.h"
#include "data/data_session.h"
#include "history/history_item.h"
#include "history/history_item_helpers.h"
#include "history/history.h"
#include "info/media/info_media_widget.h"
#include "info/media/info_media_list_section.h"
#include "info/info_controller.h"
#include "main/main_account.h"
#include "main/main_session.h"
#include "layout/layout_selection.h"
#include "storage/storage_shared_media.h"
#include "styles/style_overview.h"

namespace Info::Saved {
namespace {

using namespace Media;

constexpr auto kPreloadedScreensCount = 4;
constexpr auto kPreloadedScreensCountFull
	= kPreloadedScreensCount + 1 + kPreloadedScreensCount;

} // namespace

MusicProvider::MusicProvider(not_null<AbstractController*> controller)
: _controller(controller)
, _peer(controller->key().musicPeer())
, _history(_peer->owner().history(_peer)) {
	_controller->session().data().itemRemoved(
	) | rpl::on_next([this](auto item) {
		itemRemoved(item);
	}, _lifetime);

	style::PaletteChanged(
	) | rpl::on_next([=] {
		for (auto &layout : _layouts) {
			layout.second.item->invalidateCache();
		}
	}, _lifetime);
}

MusicProvider::~MusicProvider() {
	clear();
}

Type MusicProvider::type() {
	return Type::MusicFile;
}

bool MusicProvider::hasSelectRestriction() {
	//if (_peer->session().frozen()) {
	//	return true;
	//}
	return true;
}

rpl::producer<bool> MusicProvider::hasSelectRestrictionChanges() {
	return rpl::never<bool>();
}

bool MusicProvider::sectionHasFloatingHeader() {
	return false;
}

QString MusicProvider::sectionTitle(not_null<const BaseLayout*> item) {
	return QString();
}

bool MusicProvider::sectionItemBelongsHere(
		not_null<const BaseLayout*> item,
		not_null<const BaseLayout*> previous) {
	return true;
}

bool MusicProvider::isPossiblyMyItem(not_null<const HistoryItem*> item) {
	return true;
}

std::optional<int> MusicProvider::fullCount() {
	return _slice.fullCount();
}

void MusicProvider::clear() {
	_layouts.clear();
	_aroundId = nullptr;
	_idsLimit = kMinimalIdsLimit;
	_slice = Data::SavedMusicSlice();
}

void MusicProvider::restart() {
	clear();
	refreshViewer();
}

void MusicProvider::checkPreload(
		QSize viewport,
		not_null<BaseLayout*> topLayout,
		not_null<BaseLayout*> bottomLayout,
		bool preloadTop,
		bool preloadBottom) {
	const auto visibleWidth = viewport.width();
	const auto visibleHeight = viewport.height();
	const auto preloadedHeight = kPreloadedScreensCountFull * visibleHeight;
	const auto minItemHeight = MinItemHeight(
		Type::MusicFile,
		visibleWidth);
	const auto preloadedCount = preloadedHeight / minItemHeight;
	const auto preloadIdsLimitMin = (preloadedCount / 2) + 1;
	const auto preloadIdsLimit = preloadIdsLimitMin
		+ (visibleHeight / minItemHeight);
	const auto after = _slice.skippedAfter();
	const auto topLoaded = after && (*after == 0);
	const auto before = _slice.skippedBefore();
	const auto bottomLoaded = before && (*before == 0);

	const auto minScreenDelta = kPreloadedScreensCount
		- kPreloadIfLessThanScreens;
	const auto minIdDelta = (minScreenDelta * visibleHeight)
		/ minItemHeight;
	const auto preloadAroundItem = [&](not_null<BaseLayout*> layout) {
		auto preloadRequired = false;
		const auto item = layout->getItem();
		if (!preloadRequired) {
			preloadRequired = (_idsLimit < preloadIdsLimitMin);
		}
		if (!preloadRequired) {
			auto delta = _slice.distance(_aroundId, item);
			Assert(delta != std::nullopt);
			preloadRequired = (std::abs(*delta) >= minIdDelta);
		}
		if (preloadRequired) {
			_idsLimit = preloadIdsLimit;
			_aroundId = item;
			refreshViewer();
		}
	};

	if (preloadTop && !topLoaded) {
		preloadAroundItem(topLayout);
	} else if (preloadBottom && !bottomLoaded) {
		preloadAroundItem(bottomLayout);
	}
}

void MusicProvider::setSearchQuery(QString query) {
}

void MusicProvider::jumpToMessage(MsgId messageId, Fn<void(FullMsgId)>) {
}

void MusicProvider::refreshViewer() {
	_viewerLifetime.destroy();
	const auto aroundId = _aroundId;
 	auto ids = Data::SavedMusicList(_peer, aroundId, _idsLimit);
	std::move(
		ids
	) | rpl::on_next([=](Data::SavedMusicSlice &&slice) {
		if (!slice.fullCount()) {
			// Don't display anything while full count is unknown.
			return;
		}
		_slice = std::move(slice);

		auto nearestId = (HistoryItem*)nullptr;
		for (auto i = 0; i != _slice.size(); ++i) {
			if (_slice[i] == aroundId) {
				nearestId = aroundId;
				break;
			}
		}
		if (!nearestId && _slice.size() > 0) {
			_aroundId = _slice[_slice.size() / 2];
		}
		_refreshed.fire({});
	}, _viewerLifetime);
}

rpl::producer<> MusicProvider::refreshed() {
	return _refreshed.events();
}

std::vector<ListSection> MusicProvider::fillSections(
		not_null<Overview::Layout::Delegate*> delegate) {
	markLayoutsStale();
	const auto guard = gsl::finally([&] { clearStaleLayouts(); });

	auto result = std::vector<ListSection>();
	auto section = ListSection(Type::MusicFile, sectionDelegate());
	auto count = _slice.size();
	for (auto i = 0; i != count; ++i) {
		const auto item = _slice[i];
		if (const auto layout = getLayout(item, delegate)) {
			if (!section.addItem(layout)) {
				section.finishSection();
				result.push_back(std::move(section));
				section = ListSection(Type::MusicFile, sectionDelegate());
				section.addItem(layout);
			}
		}
	}
	if (!section.empty()) {
		section.finishSection();
		result.push_back(std::move(section));
	}
	return result;
}

void MusicProvider::itemRemoved(not_null<const HistoryItem*> item) {
	const auto i = _layouts.find(item);
	if (i == end(_layouts)) {
		return;
	}
	_layoutRemoved.fire(i->second.item.get());
	// The list widget handles layoutRemoved() synchronously and may
	// refresh its height from there, which can reach refreshViewer()
	// -> refreshRows() -> fillSections() -> clearStaleLayouts() before
	// we get back here, erasing this very entry, so look it up again.
	if (const auto j = _layouts.find(item); j != end(_layouts)) {
		_layouts.erase(j);
	}
}

void MusicProvider::markLayoutsStale() {
	for (auto &layout : _layouts) {
		layout.second.stale = true;
	}
}

void MusicProvider::clearStaleLayouts() {
	for (auto i = _layouts.begin(); i != _layouts.end();) {
		if (i->second.stale) {
			_layoutRemoved.fire(i->second.item.get());
			i = _layouts.erase(i);
		} else {
			++i;
		}
	}
}

rpl::producer<not_null<BaseLayout*>> MusicProvider::layoutRemoved() {
	return _layoutRemoved.events();
}

BaseLayout *MusicProvider::lookupLayout(const HistoryItem *item) {
	if (!item) {
		return nullptr;
	}
	const auto i = _layouts.find(item);
	return (i != _layouts.end()) ? i->second.item.get() : nullptr;
}

bool MusicProvider::isMyItem(not_null<const HistoryItem*> item) {
	return item->isSavedMusicItem() && (item->history()->peer == _peer);
}

bool MusicProvider::isAfter(
		not_null<const HistoryItem*> a,
		not_null<const HistoryItem*> b) {
	return (a->id < b->id);
}

BaseLayout *MusicProvider::getLayout(
		not_null<HistoryItem*> item,
		not_null<Overview::Layout::Delegate*> delegate) {
	auto it = _layouts.find(item);
	if (it == _layouts.end()) {
		if (auto layout = createLayout(item, delegate)) {
			layout->initDimensions();
			it = _layouts.emplace(item, std::move(layout)).first;
		} else {
			return nullptr;
		}
	}
	it->second.stale = false;
	return it->second.item.get();
}

std::unique_ptr<BaseLayout> MusicProvider::createLayout(
		not_null<HistoryItem*> item,
		not_null<Overview::Layout::Delegate*> delegate) {
	using namespace Overview::Layout;
	if (const auto media = item->media()) {
		if (const auto file = media->document()) {
			return std::make_unique<Document>(
				delegate,
				item,
				DocumentFields{ file },
				st::overviewFileLayout);
		}
	}
	return nullptr;
}

ListItemSelectionData MusicProvider::computeSelectionData(
		not_null<const HistoryItem*> item,
		TextSelection selection) {
	auto result = ListItemSelectionData(selection);
	result.canDelete = item->history()->peer->isSelf();
	result.canForward = true;// item->allowsForward();
	return result;
}

void MusicProvider::applyDragSelection(
		ListSelectedMap &selected,
		not_null<const HistoryItem*> fromItem,
		bool skipFrom,
		not_null<const HistoryItem*> tillItem,
		bool skipTill) {
	const auto fromId = fromItem->id - (skipFrom ? 1 : 0);
	const auto tillId = tillItem->id - (skipTill ? 0 : 1);
	for (auto i = selected.begin(); i != selected.end();) {
		const auto itemId = i->first->id;
		if (itemId > fromId || itemId <= tillId) {
			i = selected.erase(i);
		} else {
			++i;
		}
	}
	for (auto &layoutItem : _layouts) {
		const auto item = layoutItem.first;
		if (item->id <= fromId && item->id > tillId) {
			ChangeItemSelection(
				selected,
				item,
				computeSelectionData(item, FullSelection));
		}
	}
}

bool MusicProvider::allowSaveFileAs(
		not_null<const HistoryItem*> item,
		not_null<DocumentData*> document) {
	return true;
}

QString MusicProvider::showInFolderPath(
		not_null<const HistoryItem*> item,
		not_null<DocumentData*> document) {
	return document->filepath(true);
}

int64 MusicProvider::scrollTopStatePosition(not_null<HistoryItem*> item) {
	return item->id.bare;
}

HistoryItem *MusicProvider::scrollTopStateItem(ListScrollTopState state) {
	if (state.item && _slice.indexOf(state.item)) {
		return state.item;
	}

	auto nearest = (HistoryItem*)nullptr;
	auto distance = uint64(0);
	for (auto i = 0; i != _slice.size(); ++i) {
		const auto item = _slice[i].get();
		const auto position = item->id.bare;
		const auto delta = (position > state.position)
			? uint64(position) - uint64(state.position)
			: uint64(state.position) - uint64(position);
		if (!nearest || delta < distance) {
			nearest = item;
			distance = delta;
		}
	}
	return nearest;
}

void MusicProvider::saveState(
		not_null<Media::Memento*> memento,
		ListScrollTopState scrollState) {
	if (scrollState.item && isMyItem(scrollState.item)) {
		memento->setAroundId(scrollState.item->fullId());
		memento->setIdsLimit(_idsLimit);
		memento->setScrollTopItem(scrollState.item->globalId());
		memento->setScrollTopItemPosition(scrollState.position);
		memento->setScrollTopShift(scrollState.shift);
	}
}

void MusicProvider::restoreState(
		not_null<Media::Memento*> memento,
		Fn<void(ListScrollTopState)> restoreScrollState) {
	const auto limit = memento->idsLimit();
	const auto wasAroundId = memento->aroundId();
	if (limit <= 0 || wasAroundId.peer != _peer->id) {
		return;
	}
	const auto scrollItem = MessageByGlobalId(memento->scrollTopItem());
	const auto item = (scrollItem && isMyItem(scrollItem))
		? scrollItem
		: nullptr;
	const auto around = _controller->session().data().message(wasAroundId);
	_aroundId = (around && isMyItem(around)) ? around : item;
	_idsLimit = limit;
	const auto weak = base::make_weak(this);
	restoreScrollState({
		.position = memento->scrollTopItemPosition(),
		.item = item,
		.shift = memento->scrollTopShift(),
	});
	if (weak) {
		refreshViewer();
	}
}

} // namespace Info::Saved
