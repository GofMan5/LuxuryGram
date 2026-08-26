// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

#include "luxury/data/entities.h"
#include "base/weak_ptr.h"
#include "boxes/peer_list_box.h"
#include "boxes/peer_list_controllers.h"
#include "data/data_peer.h"
#include "history/history.h"

class RegexFilterGlobalExclusion;

namespace Main {
class Session;
} // namespace Main

namespace Settings {

class PerDialogFiltersListRow final : public PeerListRow
{
public:
	explicit PerDialogFiltersListRow(ID dialogId);
	[[nodiscard]] ID dialogId() const;
	QString generateName() override;
	PaintRoundImageCallback generatePaintUserpicCallback(bool forceRound) override;

private:
	ID _dialogId = 0;
	PeerId peerId;
};

class PerDialogFiltersListController final
	: public PeerListController
	// prepare() reads the filter tables on a worker thread and appends the rows
	// when they come back, and the section can be closed in between.
	, public base::has_weak_ptr
{
public:
	// One list widget, three sources of dialog ids. An enum rather than a bool
	// per source, so there is no such thing as two of them at once.
	enum class Mode {
		Filters,
		ShadowBan,
		Watched,
	};

	explicit PerDialogFiltersListController(not_null<Main::Session*> session,
											not_null<Window::SessionController*> controller,
											Mode mode = Mode::Filters);

	[[nodiscard]] Main::Session &session() const override;

	void prepare() override;

	void rowClicked(not_null<PeerListRow*> row) override;

	// Public only because prepare() hands it to a crl::on_main continuation.
	void fillCounts(
		const std::vector<RegexFilter> &filters,
		const std::vector<RegexFilterGlobalExclusion> &exclusions);

private:
	void prepareFromSetting(const std::unordered_set<ID> &ids);

	struct FilterCounts
	{
		int filters = 0;
		int exclusions = 0;
	};

	std::unordered_map<ID, FilterCounts> countsByDialogIds;

	const not_null<Main::Session*> _session;
	not_null<Window::SessionController*> _controller;
	Mode _mode = Mode::Filters;
};

} // namespace Settings
