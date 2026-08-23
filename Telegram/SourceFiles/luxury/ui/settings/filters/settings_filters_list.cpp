// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "luxury/ui/settings/filters/settings_filters_list.h"

#include "lang_auto.h"
#include "luxury/luxury_settings.h"
#include "luxury/data/luxury_database.h"
#include "luxury/features/filters/filters_cache_controller.h"
#include "luxury/features/filters/filters_utils.h"
#include "luxury/ui/settings/settings_luxury_utils.h"
#include "luxury/ui/settings/filters/edit_filter.h"
#include "luxury/ui/settings/filters/per_dialog_filter.h"
#include "luxury/ui/toasts.h"
#include "luxury/utils/telegram_helpers.h"
#include "boxes/connection_box.h"
#include "data/data_channel.h"
#include "info/info_wrap_widget.h"
#include "settings/settings_common.h"
#include "storage/localstorage.h"
#include "styles/style_boxes.h"
#include "styles/style_media_view.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "styles/style_widgets.h"
#include "ui/qt_object_factory.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/popup_menu.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"

namespace Settings {

rpl::producer<QString> LuxuryFiltersList::title() {
	if (shadowBan) {
		return tr::luxury_FiltersShadowBan();
	}
	if (!dialogId.has_value()) {
		return tr::luxury_RegexFiltersShared();
	}

	const auto did = getBareDialogId(*dialogId);
	const auto from = getPeerFromDialogId(did);

	// todo: shorten based on available space
	// because it may break on custom fonts
	QString res;
	if (from) {
		auto name = from->topBarNameText();
		if (name.length() > 18) {
			name = name.left(17) + "…";
		}
		res = name;
	} else {
		res = tr::luxury_RegexFiltersHeader(tr::now) + " (" + QString::number(did) + ")";
	}

	return rpl::single(res);
}

LuxuryFiltersList::LuxuryFiltersList(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
	: Section(parent, controller), _controller(controller), _content(Ui::CreateChild<Ui::VerticalLayout>(this)),
	  dialogId(_controller->luxuryFilters.dialogId),
	  showExclude(_controller->luxuryFilters.showExclude),
	  shadowBan(_controller->luxuryFilters.shadowBan) {
	setupContent(controller);
}

void LuxuryFiltersList::checkBeforeClose(Fn<void()> close) {
	_controller->luxuryFilters.showExclude = true;
	_controller->luxuryFilters.shadowBan = false;
	close();
}

void LuxuryFiltersList::addNewFilter(const RegexFilter &filter, bool exclusion) {
	const auto state = lifetime().make_state<RegexFilter>(filter);
	const auto button = _content->add(
	object_ptr<Button>(
			_content,
			rpl::single(QString::fromStdString(state->text).replace("\n", " ")),
			st::settingsButtonNoIcon
		)
	);

	if (!state->enabled) {
		button->setColorOverride(st::storiesComposeGrayText->c);
	}

	auto defaultClickHandler = [=, dialogId = dialogId]() mutable
	{
		auto _contextMenu = new Ui::PopupMenu(this, st::popupMenuWithIcons);
		_contextMenu->setAttribute(Qt::WA_DeleteOnClose);

		_contextMenu->addAction(
			tr::lng_theme_edit(tr::now),
			[=]
			{
				_controller->show(RegexEditBox(state, dialogId));
			},
			&st::menuIconEdit);

		_contextMenu->addAction(
			state->enabled ? tr::lng_settings_auto_night_disable(tr::now) : tr::lng_sure_enable(tr::now),
			[=, weak = base::make_weak(this)]
			{
				// The row is drawn from this state, so flip it now and put it
				// back if the write does not land.
				state->enabled = !state->enabled;
				const auto wanted = *state;
				LuxuryDatabase::async([=] {
					const auto saved = LuxuryDatabase::updateRegexFilter(
						wanted);
					if (saved) {
						// Already off the main thread.
						FiltersCacheController::reloadNow();
					}
					crl::on_main([=] {
						if (saved) {
							FiltersCacheController::fireUpdate();
							return;
						}
						Luxury::Ui::ShowDatabaseError();
						// state belongs to the section's lifetime.
						if (weak) {
							state->enabled = !state->enabled;
						}
					});
				});
			},
			state->enabled ? &st::menuIconBlock : &st::menuIconUnblock);

		_contextMenu->addSeparator();

		_contextMenu->addAction(
			tr::lng_theme_delete(tr::now),
			[=]
			{
				const auto id = state->id;
				LuxuryDatabase::async([=] {
					// Both, always: dropping the filter but keeping its
					// exclusions would leave rows pointing at nothing.
					const auto removed = LuxuryDatabase::deleteFilter(id);
					const auto cleaned =
						LuxuryDatabase::deleteExclusionsByFilterId(id);
					// Already off the main thread.
					FiltersCacheController::reloadNow();
					crl::on_main([=] {
						if (!removed || !cleaned) {
							Luxury::Ui::ShowDatabaseError();
						}
						FiltersCacheController::fireUpdate();
					});
				});
			},
			&st::menuIconDelete);

		_contextMenu->popup(QCursor::pos());
	};

	// we've opened filters list from top "Exclude" button
	// on click, close the section
	auto exclusionsClickHandler = [=, controller = _controller, dialogId = dialogId]() mutable
	{
		Expects(dialogId.has_value());

		/*
		└── class Info::WrapWidget
			└── class Info::Settings::Widget
				└── class Ui::ScrollArea
					└── class QWidget
						└── class Ui::PaddingWrap<class Ui::RpWidget>
							└── class Settings::LuxuryFiltersList
		 */
		// controller->showBackFromStack() doesn't work (closes box completely)
		// so as a workaround, use WrapWidget
		Info::WrapWidget *wrap = nullptr;
		for (auto ancestor = parentWidget(); ancestor; ancestor = ancestor->parentWidget()) {
			wrap = dynamic_cast<Info::WrapWidget*>(ancestor);
			if (wrap) {
				break;
			}
		}
		if (!wrap) {
			return;
		}

		const RegexFilterGlobalExclusion newExclusion = {
			.dialogId = *dialogId,
			.filterId = state->id
		};

		const auto weakWrap = base::make_weak(wrap);
		LuxuryDatabase::async([=] {
			const auto saved = LuxuryDatabase::addRegexExclusion(newExclusion);
			if (saved) {
				// Already off the main thread.
				FiltersCacheController::reloadNow();
			}
			crl::on_main([=] {
				if (!saved) {
					Luxury::Ui::ShowDatabaseError();
					return;
				}
				FiltersCacheController::fireUpdate();
				// The user can navigate away while the write is in flight, and
				// then there is nothing left to go back from.
				if (const auto strong = weakWrap.get()) {
					controller->luxuryFilters = {
						.dialogId = dialogId,
						.showExclude = true,
					};
					strong->showBackFromStackInternal(
						Window::SectionShow(anim::type::normal));
				}
			});
		});
	};
	auto deleteExclusionsClickHandler = [=, this]() mutable
	{
		auto _contextMenu = new Ui::PopupMenu(this, st::popupMenuWithIcons);
		_contextMenu->setAttribute(Qt::WA_DeleteOnClose);

		_contextMenu->addAction(
			tr::lng_theme_delete(tr::now),
			[=, this]
			{
				Expects(dialogId.has_value());

				const auto did = *dialogId;
				const auto id = state->id;
				LuxuryDatabase::async([=] {
					const auto removed = LuxuryDatabase::deleteExclusion(
						did,
						id);
					if (removed) {
						// Already off the main thread.
						FiltersCacheController::reloadNow();
					}
					crl::on_main([=] {
						if (!removed) {
							Luxury::Ui::ShowDatabaseError();
							return;
						}
						FiltersCacheController::fireUpdate();
					});
				});
			},
			&st::menuIconDelete);

		_contextMenu->popup(QCursor::pos());
	};

	if (exclusion) {
		button->addClickHandler(deleteExclusionsClickHandler);
	} else if (dialogId && showExclude && !*showExclude) {
		button->addClickHandler(exclusionsClickHandler);
	} else {
		button->addClickHandler(defaultClickHandler);
	}


	crl::on_main(
		this,
		[=, this]
		{
			adjustSize();
			updateGeometry();
		});
}

void LuxuryFiltersList::initializeSharedFilters(
	not_null<Ui::VerticalLayout*> container) {
	// Up to three full-table reads, and the section is built during a navigation
	// animation, so they do not belong on the main thread. The rows land a frame
	// or two later; the empty state waits with them, because "no filters" is only
	// known once the read is back.
	const auto perDialog = dialogId && showExclude && *showExclude;
	const auto forExcluding = dialogId && showExclude && !*showExclude;
	const auto did = dialogId.value_or(0);
	const auto weak = base::make_weak(this);
	LuxuryDatabase::async([=] {
		auto loadedFilters = perDialog
			? LuxuryDatabase::getByDialogId(did)
			: LuxuryDatabase::getShared();
		auto loadedExclusions = perDialog
			? LuxuryDatabase::getExcludedByDialogId(did)
			: std::vector<RegexFilter>();

		// remove shared filters that already excluded for that peer exclusion
		if (forExcluding) {
			const auto excludedForDialogId =
				LuxuryDatabase::getExcludedByDialogId(did);

			auto rangeToRemove = std::ranges::remove_if(
				loadedFilters,
				[&](const RegexFilter &filter)
				{
					for (const auto &excluded : excludedForDialogId) {
						if (excluded == filter) {
							return true;
						}
					}
					return false;
				});
			loadedFilters.erase(rangeToRemove.begin(), rangeToRemove.end());
		}

		crl::on_main([=,
				loadedFilters = std::move(loadedFilters),
				loadedExclusions = std::move(loadedExclusions)]() mutable {
			const auto strong = weak.get();
			if (!strong) {
				return;
			}
			strong->filters = std::move(loadedFilters);
			strong->exclusions = std::move(loadedExclusions);
			strong->fillLoadedFilters(container);
		});
	});
}

void LuxuryFiltersList::fillLoadedFilters(
	not_null<Ui::VerticalLayout*> container) {
	if (!filters.empty()) {
		AddSkip(container);
		filtersTitle = AddSubsectionTitle(container, tr::luxury_RegexFiltersHeader());

		for (const auto &filter : filters) {
			addNewFilter(filter);
		}
	}

	if (!exclusions.empty()) {
		if (!filters.empty()) {
			AddSectionDivider(container);
		}

		excludedTitle = AddSubsectionTitle(container, tr::luxury_RegexFiltersExcluded());

		for (const auto &exclusion : exclusions) {
			addNewFilter(exclusion, true);
		}
	}

	if (filters.empty() && exclusions.empty()) {
		Ui::AddDividerText(container, tr::luxury_RegexFiltersListEmpty());
	}
}

void LuxuryFiltersList::initializeShadowBan(not_null<Ui::VerticalLayout*> container) {
	auto ctrl = container->lifetime().make_state<PerDialogFiltersListController>(
		&_controller->session(),
		_controller,
		true // shadowBan
	);

	auto list = object_ptr<Ui::PaddingWrap<PeerListContent>>(
		container,
		object_ptr<PeerListContent>(
			container,
			ctrl),
		QMargins(0, -st::peerListBox.padding.top(), 0, -st::peerListBox.padding.bottom()));

	// delegate is not initialized at this moment
	if (LuxurySettings::getInstance().shadowBanIds().size() > 0) {
		AddSkip(container);

		filtersTitle = AddSubsectionTitle(container, tr::luxury_RegexFiltersHeader());
		const auto content = container->add(std::move(list));

		AddSkip(container);

		auto delegate = container->lifetime().make_state<PeerListContentDelegateSimple>();
		delegate->setContent(content->entity());
		ctrl->setDelegate(delegate);
	} else {
		Ui::AddDividerText(container, tr::luxury_RegexFiltersListEmpty());
	}
}

void LuxuryFiltersList::setupContent(not_null<Window::SessionController*> controller) {
	if (shadowBan) {
		initializeShadowBan(_content);
	} else {
		initializeSharedFilters(_content);
	}

	ResizeFitChild(this, _content);
}

} // namespace Settings
