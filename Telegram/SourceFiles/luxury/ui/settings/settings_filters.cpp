// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "luxury/ui/settings/settings_filters.h"

#include "lang_auto.h"
#include "luxury/luxury_settings.h"
#include "luxury/data/luxury_database.h"
#include "luxury/features/filters/filters_cache_controller.h"
#include "luxury/ui/boxes/import_filters_box.h"
#include "luxury/ui/settings/luxury_builder.h"
#include "luxury/ui/settings/settings_main.h"
#include "luxury/ui/toasts.h"
#include "luxury/utils/telegram_helpers.h"
#include "boxes/abstract_box.h"
#include "boxes/peer_list_box.h"
#include "core/application.h"
#include "filters/per_dialog_filter.h"
#include "filters/settings_filters_list.h"
#include "inline_bots/bot_attach_web_view.h"
#include "settings/settings_builder.h"
#include "settings/settings_common.h"
#include "styles/style_luxury_icons.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "ui/vertical_list.h"
#include "ui/boxes/confirm_box.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_controller.h"
#include "window/window_peer_menu.h"
#include "window/window_session_controller.h"

namespace Settings {

using namespace Builder;
using namespace LuxuryBuilder;

namespace {

void BuildFiltersSettings(SectionBuilder &builder) {
	auto *settings = &LuxurySettings::getInstance();

	builder.addSkip();
	builder.addSubsectionTitle(tr::luxury_RegexFilters());

	const auto enabledButton = builder.addButton({
		.id = u"luxury/filtersEnabled"_q,
		.title = tr::luxury_RegexFiltersEnable(),
		.st = &st::settingsButtonNoIcon,
		.toggled = rpl::single(settings->filtersEnabled()),
	});
	if (enabledButton) {
		enabledButton->toggledValue(
		) | rpl::filter([=](bool enabled) {
			return (enabled != settings->filtersEnabled());
		}) | on_next([=](bool enabled) {
			LuxurySettings::getInstance().setFiltersEnabled(enabled);
			// None of these three feed into the compiled patterns, they only
			// change where the existing ones apply, so the verdicts are enough.
			FiltersCacheController::dropResults();
		}, enabledButton->lifetime());
	}

	const auto sharedButton = builder.addButton({
		.id = u"luxury/filtersEnabledInChats"_q,
		.altIds = { u"luxury/filtersInChats"_q },
		.title = tr::luxury_RegexFiltersEnableSharedInChats(),
		.st = &st::settingsButtonNoIcon,
		.toggled = rpl::single(settings->filtersEnabledInChats()),
	});
	if (sharedButton) {
		sharedButton->toggledValue(
		) | rpl::filter([=](bool enabled) {
			return (enabled != settings->filtersEnabledInChats());
		}) | on_next([=](bool enabled) {
			LuxurySettings::getInstance().setFiltersEnabledInChats(enabled);
			FiltersCacheController::dropResults();
		}, sharedButton->lifetime());
	}

	const auto blockedButton = builder.addButton({
		.id = u"luxury/hideFromBlocked"_q,
		.title = tr::luxury_FiltersHideFromBlocked(),
		.st = &st::settingsButtonNoIcon,
		.toggled = rpl::single(settings->hideFromBlocked()),
	});
	if (blockedButton) {
		blockedButton->toggledValue(
		) | rpl::filter([=](bool enabled) {
			return (enabled != settings->hideFromBlocked());
		}) | on_next([=](bool enabled) {
			LuxurySettings::getInstance().setHideFromBlocked(enabled);
			FiltersCacheController::dropResults();
		}, blockedButton->lifetime());
	}

	builder.addSkip();
}

void BuildShared(SectionBuilder &builder) {
	builder.addDivider();
	builder.addSkip();

	const auto controller = builder.controller();
	builder.addButton({
		.id = u"luxury/sharedFilters"_q,
		.title = tr::luxury_RegexFiltersShared(),
		.st = &st::settingsButtonNoIcon,
		.onClick = [=] {
			controller->luxuryFilters = { .showExclude = false };
			controller->showSettings(LuxuryFiltersList::Id());
		},
	});
}

void BuildShadowBan(SectionBuilder &builder) {
	const auto controller = builder.controller();

	builder.addButton({
		.id = u"luxury/shadowBanIds"_q,
		.altIds = { u"luxury/shadowBanList"_q },
		.title = tr::luxury_FiltersShadowBan(),
		.st = &st::settingsButtonNoIcon,
		.onClick = [=] {
			controller->luxuryFilters = {
				.showExclude = false,
				.shadowBan = true,
			};
			controller->showSettings(LuxuryFiltersList::Id());
		},
	});
}

void BuildPerDialog(SectionBuilder &builder) {
	builder.add([](const BuildContext &ctx) {
		v::match(ctx, [&](const WidgetContext &wctx) {
			if (!LuxuryDatabase::hasPerDialogFilters()) {
				return;
			}

			const auto container = wctx.container;
			const auto controller = wctx.controller;

			AddSkip(container);
			AddDivider(container);

			auto ctrl = container->lifetime().make_state<PerDialogFiltersListController>(
				&controller->session(),
				controller);

			auto list = object_ptr<Ui::PaddingWrap<PeerListContent>>(
				container,
				object_ptr<PeerListContent>(
					container,
					ctrl),
				QMargins(0, -st::peerListBox.padding.top(), 0, -st::peerListBox.padding.bottom()));
			AddSkip(container);
			const auto content = container->add(std::move(list));
			AddSkip(container);
			auto delegate = container->lifetime().make_state<PeerListContentDelegateSimple>();
			delegate->setContent(content->entity());
			ctrl->setDelegate(delegate);
		}, [&](const SearchContext &) {
		});
	});
}

const auto kMeta = BuildHelper({
	.id = LuxuryFilters::Id(),
	.parentId = LuxuryMain::Id(),
	.title = &tr::luxury_CategoryFilters,
	.icon = &st::menuIconTagFilter,
}, [](SectionBuilder &builder) {
	BuildFiltersSettings(builder);
	BuildShared(builder);
	BuildShadowBan(builder);
	BuildPerDialog(builder);
});

} // namespace

rpl::producer<QString> LuxuryFilters::title() {
	return tr::luxury_CategoryFilters();
}

void LuxuryFilters::fillTopBarMenu(const Ui::Menu::MenuCallback &addAction) {
	addAction(
		tr::luxury_FiltersMenuSelectChat(tr::now),
		[=] {
			if (const auto window = Core::App().activeWindow()) {
				if (const auto controller = window->sessionController()) {
					auto types = InlineBots::PeerTypes();
					types |= InlineBots::PeerType::Bot;
					types |= InlineBots::PeerType::Group;
					types |= InlineBots::PeerType::Broadcast;

					Window::ShowChooseRecipientBox(
						controller,
						[=](not_null<Data::Thread*> thread) {
							const auto peer = thread->peer();
							controller->luxuryFilters = {
								.dialogId = getDialogIdFromPeer(peer),
								.showExclude = true,
							};
							controller->showSettings(LuxuryFiltersList::Id());
							return true;
						},
						tr::luxury_FiltersMenuSelectChat(),
						nullptr,
						types);
				}
			}
		},
		&st::menuIconSearch);
	addAction({ .isSeparator = true });
	addAction(
		tr::luxury_FiltersMenuImport(tr::now),
		[=] {
			auto box = Box(Ui::FillImportFiltersBox, true);
			Ui::show(std::move(box));
		},
		&st::menuIconArchive);
	if (LuxuryDatabase::hasFilters()) {
		addAction(
			tr::luxury_FiltersMenuExport(tr::now),
			[=] {
				auto box = Box(Ui::FillImportFiltersBox, false);
				Ui::show(std::move(box));
			},
			&st::menuIconUnarchive);
	}
	addAction({ .isSeparator = true });
	addAction({
		.text = tr::luxury_FiltersMenuClear(tr::now),
		.handler = [=] {
			auto callback = [=](Fn<void()> &&close) {
				close();
				LuxuryDatabase::async([] {
					// Both, always: && would leave the exclusions behind
					// whenever clearing the filters themselves failed.
					const auto filters = LuxuryDatabase::deleteAllFilters();
					const auto exclusions =
						LuxuryDatabase::deleteAllExclusions();
					// Already off the main thread.
					FiltersCacheController::reloadNow();
					crl::on_main([=] {
						if (!filters || !exclusions) {
							Luxury::Ui::ShowDatabaseError();
						}
						FiltersCacheController::fireUpdate();
					});
				});
			};
			auto box = Ui::MakeConfirmBox({
				.text = tr::luxury_FiltersClearPopupText(),
				.confirmed = callback,
				.confirmText = tr::luxury_FiltersClearPopupActionText(),
				.confirmStyle = &st::attentionBoxButton,
			});
			Ui::show(std::move(box));
		},
		.icon = &st::menuIconClearAttention,
		.isAttention = true,
	});
}

LuxuryFilters::LuxuryFilters(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

void LuxuryFilters::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kMeta.build);
	Ui::ResizeFitChild(this, content);
}

} // namespace Settings
