// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/ui/settings/settings_appearance.h"

#include "lang_auto.h"
#include "ayu/ayu_settings.h"
#include "ayu/ayu_ui_settings.h"
#include "ayu/ui/boxes/font_selector.h"
#include "ayu/ui/components/avatar_corners_preview.h"
#include "ayu/ui/components/icon_picker.h"
#include "ayu/ui/settings/ayu_builder.h"
#include "ayu/ui/settings/settings_ayu_utils.h"
#include "ayu/ui/settings/settings_main.h"
#include "inline_bots/bot_attach_web_view.h"
#include "main/main_session.h"
#include "settings/settings_builder.h"
#include "settings/settings_common.h"
#include "styles/style_ayu_icons.h"
#include "styles/style_ayu_styles.h"
#include "styles/style_dialogs.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "ui/painter.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"

namespace Settings {

using namespace Builder;
using namespace LuxuryBuilder;

namespace {

bool HasDrawerBots(not_null<Window::SessionController*> controller) {
	// todo: maybe iterate through all accounts
	const auto bots = &controller->session().attachWebView();
	for (const auto &bot : bots->attachBots()) {
		if (!bot.inMainMenu || !bot.media) {
			continue;
		}
		return true;
	}
	return false;
}

void BuildAppIcon(SectionBuilder &builder, LuxurySectionBuilder &luxury) {
	builder.addSubsectionTitle({
		.id = u"ayu/appIcon"_q,
		.title = tr::luxury_AppIconHeader(),
	});

	builder.add([](const WidgetContext &ctx) -> SectionBuilder::WidgetToAdd {
		return {
			.widget = object_ptr<IconPicker>(ctx.container),
			.margin = st::settingsButtonNoIcon.padding,
		};
	});

#if defined Q_OS_WIN || defined Q_OS_MAC
	builder.addDivider();
	builder.addSkip();
	luxury.addSettingToggle({
		.id = u"ayu/hideNotificationBadge"_q,
		.title = tr::luxury_HideNotificationBadge(),
		.getter = &LuxurySettings::hideNotificationBadge,
		.setter = &LuxurySettings::setHideNotificationBadge,
	});
	builder.addSkip();
	builder.addDividerText(tr::luxury_HideNotificationBadgeDescription());
	builder.addSkip();
#else
    builder.addDivider();
    builder.addSkip();
#endif
}

void BuildAvatarCorners(SectionBuilder &builder, LuxurySectionBuilder &luxury) {
	auto *settings = &LuxurySettings::getInstance();
	const auto controller = builder.controller();

	const auto mapRadius = [](int val)
	{
		if (val == 0) {
			return tr::luxury_AvatarCornersSquare(tr::now).toUpper();
		} else if (val == LuxuryUiSettings::kMaxAvatarCorners) {
			return tr::luxury_AvatarCornersCircle(tr::now).toUpper();
		}
		return QString::number(val);
	};

	builder.add([=](const WidgetContext &ctx) -> SectionBuilder::WidgetToAdd {
		const auto container = ctx.container;
		auto title = object_ptr<Ui::FlatLabel>(
			container,
			tr::luxury_AvatarCorners(),
			st::defaultSubsectionTitle);
		const auto titleRaw = title.data();

		const auto badge = Ui::CreateChild<Ui::PaddingWrap<Ui::FlatLabel>>(
			container,
			object_ptr<Ui::FlatLabel>(
				container,
				settings->avatarCornersValue() | rpl::map(mapRadius),
				st::settingsPremiumNewBadge),
			st::luxuryBetaBadgePadding);
		badge->show();
		badge->setAttribute(Qt::WA_TransparentForMouseEvents);
		badge->paintRequest() | rpl::on_next([=] {
			auto p = QPainter(badge);
			auto hq = PainterHighQualityEnabler(p);
			p.setPen(Qt::NoPen);
			p.setBrush(st::windowBgActive);
			const auto r = st::luxuryBetaBadgePadding.left();
			p.drawRoundedRect(badge->rect(), r, r);
		}, badge->lifetime());

		titleRaw->geometryValue() | rpl::on_next([=](QRect geometry) {
			badge->moveToLeft(
				geometry.x()
					+ titleRaw->textMaxWidth()
					+ st::settingsPremiumNewBadgePosition.x(),
				geometry.y()
					+ (geometry.height() - badge->height()) / 2);
		}, badge->lifetime());

		return {
			.widget = std::move(title),
			.margin = st::defaultSubsectionTitlePadding,
		};
	}, [] {
		return SearchEntry{
			.id = u"ayu/avatarCorners"_q,
			.title = tr::luxury_AvatarCorners(tr::now),
		};
	});

	auto *previewRaw = static_cast<AvatarCornersPreview*>(nullptr);
	builder.add([&](const Builder::WidgetContext &ctx) -> SectionBuilder::WidgetToAdd {
		auto preview = object_ptr<AvatarCornersPreview>(
			ctx.container,
			controller);
		previewRaw = preview.data();
		const auto vMargin = st::settingsButtonNoIcon.padding
			- st::defaultDialogRow.padding;
		return {
			.widget = std::move(preview),
			.margin = QMargins(0, vMargin.top(), 0, vMargin.bottom()),
		};
	});

	luxury.addSlider({
		.id = u"ayu/avatarCornersSlider"_q,
		.title = rpl::single(QString()),
		.showTitle = false,
		.steps = LuxuryUiSettings::kMaxAvatarCorners + 1,
		.current = settings->avatarCorners(),
		.onChanged = [=](int val) {
			LuxurySettings::getInstance().setAvatarCorners(val);
			if (previewRaw) {
				previewRaw->update();
			}
		},
		.onFinalChanged = [=](int val) {
			LuxurySettings::getInstance().setAvatarCorners(val);
			ShowRestartPrompt(controller);
		},
	});

	luxury.addSettingToggle({
		.id = u"ayu/singleCornerRadius"_q,
		.title = tr::luxury_SingleCornerRadius(),
		.getter = &LuxurySettings::singleCornerRadius,
		.setter = &LuxurySettings::setSingleCornerRadius,
	});

	builder.addSkip();
	builder.addDividerText(tr::luxury_SingleCornerRadiusDescription());
	builder.addSkip();
}

void BuildAppearance(SectionBuilder &builder, LuxurySectionBuilder &luxury) {
	auto *settings = &LuxurySettings::getInstance();

	builder.addSubsectionTitle(tr::luxury_CategoryAppearance());

	luxury.addSettingToggle({
		.id = u"ayu/materialSwitches"_q,
		.altIds = { u"ayu/newSwitchStyle"_q },
		.title = tr::luxury_MaterialSwitches(),
		.getter = &LuxurySettings::materialSwitches,
		.setter = &LuxurySettings::setMaterialSwitches,
	});
	luxury.addSettingToggle({
		.id = u"ayu/disableCustomBackgrounds"_q,
		.altIds = { u"ayu/customThemes"_q },
		.title = tr::luxury_DisableCustomBackgrounds(),
		.getter = &LuxurySettings::disableCustomBackgrounds,
		.setter = &LuxurySettings::setDisableCustomBackgrounds,
	});
	luxury.addSettingToggle({
		.id = u"ayu/hidePremiumStatuses"_q,
		.title = tr::luxury_HidePremiumStatuses(),
		.getter = &LuxurySettings::hidePremiumStatuses,
		.setter = &LuxurySettings::setHidePremiumStatuses,
	});

	const auto controller = builder.controller();
	builder.addButton({
		.id = u"ayu/monoFont"_q,
		.title = tr::luxury_MonospaceFont(),
		.st = &st::settingsButtonNoIcon,
		.label = rpl::single(
			settings->monoFont().isEmpty()
				? tr::luxury_FontDefault(tr::now)
				: settings->monoFont()),
		.onClick = [=] {
			LuxuryUi::FontSelectorBox::Show(
				controller,
				[=](const QString &font) {
					LuxurySettings::getInstance().setMonoFont(font);
				});
		},
	});

	luxury.addSectionDivider();
}

void BuildChatFolders(SectionBuilder &builder, LuxurySectionBuilder &luxury) {
	builder.addSubsectionTitle(tr::luxury_ChatFoldersHeader());

	luxury.addSettingToggle({
		.id = u"ayu/hideNotificationCounters"_q,
		.altIds = { u"ayu/tabCounter"_q },
		.title = tr::luxury_HideNotificationCounters(),
		.getter = &LuxurySettings::hideNotificationCounters,
		.setter = &LuxurySettings::setHideNotificationCounters,
	});
	luxury.addSettingToggle({
		.id = u"ayu/hideAllChatsFolder"_q,
		.altIds = { u"ayu/hideAllChats"_q },
		.title = tr::luxury_HideAllChats(),
		.getter = &LuxurySettings::hideAllChatsFolder,
		.setter = &LuxurySettings::setHideAllChatsFolder,
	});

	luxury.addSectionDivider();
}

void BuildTrayElements(SectionBuilder &builder, LuxurySectionBuilder &luxury) {
	builder.addSubsectionTitle(tr::luxury_TrayElementsHeader());

	luxury.addSettingToggle({
		.id = u"ayu/showGhostToggleInTray"_q,
		.title = tr::luxury_EnableGhostModeTray(),
		.getter = &LuxurySettings::showGhostToggleInTray,
		.setter = &LuxurySettings::setShowGhostToggleInTray,
	});

#if defined Q_OS_WIN || defined Q_OS_MAC
	luxury.addSettingToggle({
		.id = u"ayu/showStreamerToggleInTray"_q,
		.title = tr::luxury_EnableStreamerModeTray(),
		.getter = &LuxurySettings::showStreamerToggleInTray,
		.setter = &LuxurySettings::setShowStreamerToggleInTray,
	});
#endif

	luxury.addSectionDivider();
}

void BuildDrawerElements(SectionBuilder &builder, LuxurySectionBuilder &luxury) {
	builder.addSubsectionTitle(tr::luxury_DrawerElementsHeader());

	luxury.addSettingToggle({
		.id = u"ayu/showMyProfileInDrawer"_q,
		.title = tr::lng_menu_my_profile(),
		.getter = &LuxurySettings::showMyProfileInDrawer,
		.setter = &LuxurySettings::setShowMyProfileInDrawer,
		.icon = { &st::menuIconProfile },
	});

	const auto controller = builder.controller();
	if (controller && HasDrawerBots(controller)) {
		luxury.addSettingToggle({
			.id = u"ayu/showBotsInDrawer"_q,
			.title = tr::lng_filters_type_bots(),
			.getter = &LuxurySettings::showBotsInDrawer,
			.setter = &LuxurySettings::setShowBotsInDrawer,
			.icon = { &st::menuIconBot },
		});
	}

	luxury.addSettingToggle({
		.id = u"ayu/showNewGroupInDrawer"_q,
		.title = tr::lng_create_group_title(),
		.getter = &LuxurySettings::showNewGroupInDrawer,
		.setter = &LuxurySettings::setShowNewGroupInDrawer,
		.icon = { &st::menuIconGroups },
	});
	luxury.addSettingToggle({
		.id = u"ayu/showNewChannelInDrawer"_q,
		.title = tr::lng_create_channel_title(),
		.getter = &LuxurySettings::showNewChannelInDrawer,
		.setter = &LuxurySettings::setShowNewChannelInDrawer,
		.icon = { &st::menuIconChannel },
	});
	luxury.addSettingToggle({
		.id = u"ayu/showContactsInDrawer"_q,
		.title = tr::lng_menu_contacts(),
		.getter = &LuxurySettings::showContactsInDrawer,
		.setter = &LuxurySettings::setShowContactsInDrawer,
		.icon = { &st::menuIconUserShow },
	});
	luxury.addSettingToggle({
		.id = u"ayu/showCallsInDrawer"_q,
		.title = tr::lng_menu_calls(),
		.getter = &LuxurySettings::showCallsInDrawer,
		.setter = &LuxurySettings::setShowCallsInDrawer,
		.icon = { &st::menuIconPhone },
	});
	luxury.addSettingToggle({
		.id = u"ayu/showSavedMessagesInDrawer"_q,
		.title = tr::lng_saved_messages(),
		.getter = &LuxurySettings::showSavedMessagesInDrawer,
		.setter = &LuxurySettings::setShowSavedMessagesInDrawer,
		.icon = { &st::menuIconSavedMessages },
	});
	luxury.addSettingToggle({
		.id = u"ayu/showLReadToggleInDrawer"_q,
		.title = tr::luxury_LReadMessages(),
		.getter = &LuxurySettings::showLReadToggleInDrawer,
		.setter = &LuxurySettings::setShowLReadToggleInDrawer,
		.icon = { &st::luxuryLReadMenuIcon },
	});
	luxury.addSettingToggle({
		.id = u"ayu/showSReadToggleInDrawer"_q,
		.title = tr::luxury_SReadMessages(),
		.getter = &LuxurySettings::showSReadToggleInDrawer,
		.setter = &LuxurySettings::setShowSReadToggleInDrawer,
		.icon = { &st::luxurySReadMenuIcon },
	});
	luxury.addSettingToggle({
		.id = u"ayu/showNightModeToggleInDrawer"_q,
		.title = tr::lng_menu_night_mode(),
		.getter = &LuxurySettings::showNightModeToggleInDrawer,
		.setter = &LuxurySettings::setShowNightModeToggleInDrawer,
		.icon = { &st::menuIconNightMode },
	});
	luxury.addSettingToggle({
		.id = u"ayu/showGhostToggleInDrawer"_q,
		.title = tr::luxury_GhostModeToggle(),
		.getter = &LuxurySettings::showGhostToggleInDrawer,
		.setter = &LuxurySettings::setShowGhostToggleInDrawer,
		.icon = { &st::luxuryGhostIcon },
	});

#if defined Q_OS_WIN || defined Q_OS_MAC
	luxury.addSettingToggle({
		.id = u"ayu/showStreamerToggleInDrawer"_q,
		.title = tr::luxury_StreamerModeToggle(),
		.getter = &LuxurySettings::showStreamerToggleInDrawer,
		.setter = &LuxurySettings::setShowStreamerToggleInDrawer,
		.icon = { &st::luxuryStreamerModeMenuIcon },
	});
#endif

	builder.addSkip();
}

const auto kMeta = BuildHelper({
	.id = LuxuryAppearance::Id(),
	.parentId = LuxuryMain::Id(),
	.title = &tr::luxury_CategoryAppearance,
	.icon = &st::menuIconPalette,
}, [](SectionBuilder &builder) {
	auto luxury = LuxurySectionBuilder(builder);

	builder.addSkip();
	BuildAppIcon(builder, luxury);
	BuildAvatarCorners(builder, luxury);
	BuildAppearance(builder, luxury);
	BuildChatFolders(builder, luxury);
	BuildTrayElements(builder, luxury);
	BuildDrawerElements(builder, luxury);
	builder.addSkip();
});

} // namespace

rpl::producer<QString> LuxuryAppearance::title() {
	return tr::luxury_CategoryAppearance();
}

LuxuryAppearance::LuxuryAppearance(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

void LuxuryAppearance::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kMeta.build);
	Ui::ResizeFitChild(this, content);
}

Type LuxuryAppearanceId() {
	return LuxuryAppearance::Id();
}

} // namespace Settings
