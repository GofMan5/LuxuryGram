// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "luxury/ui/settings/settings_chats.h"

#include "lang_auto.h"
#include "luxury/luxury_settings.h"
#include "luxury/ui/boxes/edit_mark_box.h"
#include "luxury/ui/components/message_preview.h"
#include "luxury/ui/settings/luxury_builder.h"
#include "luxury/ui/settings/settings_luxury_utils.h"
#include "luxury/ui/settings/settings_main.h"
#include "settings/settings_builder.h"
#include "settings/settings_common.h"
#include "styles/style_luxury_icons.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"

#include <memory>

namespace Settings {

using namespace Builder;
using namespace LuxuryBuilder;

namespace {

struct PreviewState {
	MessagePreview *widget = nullptr;
};

void BuildStickersAndEmoji(SectionBuilder &builder, LuxurySectionBuilder &luxury) {
	builder.addSubsectionTitle(tr::lng_settings_stickers_emoji());

	luxury.addSettingToggle({
		.id = u"luxury/showOnlyAddedEmojisAndStickers"_q,
		.title = tr::luxury_ShowOnlyAddedEmojisAndStickers(),
		.getter = &LuxurySettings::showOnlyAddedEmojisAndStickers,
		.setter = &LuxurySettings::setShowOnlyAddedEmojisAndStickers,
	});

	luxury.addSettingToggle({
		.id = u"luxury/unlimitedRecentStickers"_q,
		.altIds = { u"luxury/recentStickersCount"_q },
		.title = tr::luxury_SettingsUnlimitedRecentStickers(),
		.getter = &LuxurySettings::unlimitedRecentStickers,
		.setter = &LuxurySettings::setUnlimitedRecentStickers,
	});

	luxury.addCollapsibleToggle({
		.id = u"luxury/hideReactions"_q,
		.title = tr::luxury_HideReactions(),
		.checkboxes = {
			NestedEntry{
				tr::luxury_HideReactionsInChannels(tr::now),
				[] { return !LuxurySettings::getInstance().showChannelReactions(); },
				[](bool v) { LuxurySettings::getInstance().setShowChannelReactions(!v); }
			},
			NestedEntry{
				tr::luxury_HideReactionsInGroups(tr::now),
				[] { return !LuxurySettings::getInstance().showGroupReactions(); },
				[](bool v) { LuxurySettings::getInstance().setShowGroupReactions(!v); }
			},
			NestedEntry{
				tr::luxury_HideReactionsInPrivateChats(tr::now),
				[] { return !LuxurySettings::getInstance().showPrivateChatReactions(); },
				[](bool v) { LuxurySettings::getInstance().setShowPrivateChatReactions(!v); }
			}
		},
		.toggledWhenAll = false,
	});

	luxury.addSectionDivider();
}

void BuildGroupsAndChannels(SectionBuilder &builder, LuxurySectionBuilder &luxury) {
	auto *settings = &LuxurySettings::getInstance();

	builder.addSubsectionTitle(tr::lng_premium_double_limits_subtitle_channels());

	luxury.addChooseButton({
		.id = u"luxury/channelBottomButton"_q,
		.altIds = { u"luxury/bottomButton"_q },
		.title = tr::luxury_ChannelBottomButton(),
		.boxTitle = tr::luxury_ChannelBottomButton(),
		.initialSelection = static_cast<int>(settings->channelBottomButton()),
		.options = {
			tr::luxury_ChannelBottomButtonHide(tr::now),
			tr::luxury_ChannelBottomButtonMute(tr::now),
			tr::luxury_ChannelBottomButtonDiscuss(tr::now),
		},
		.setter = [](int index) {
			LuxurySettings::getInstance().setChannelBottomButton(
				static_cast<ChannelBottomButton>(index));
		},
	});

	luxury.addSettingToggle({
		.id = u"luxury/quickAdminShortcuts"_q,
		.title = tr::luxury_QuickAdminShortcuts(),
		.getter = &LuxurySettings::quickAdminShortcuts,
		.setter = &LuxurySettings::setQuickAdminShortcuts,
	});
	luxury.addSettingToggle({
		.id = u"luxury/disableGreetingSticker"_q,
		.title = tr::luxury_DisableGreetingSticker(),
		.getter = &LuxurySettings::disableGreetingSticker,
		.setter = &LuxurySettings::setDisableGreetingSticker,
	});
	luxury.addSettingToggle({
		.id = u"luxury/showMessageShot"_q,
		.title = tr::luxury_SettingsShowMessageShot(),
		.getter = &LuxurySettings::showMessageShot,
		.setter = &LuxurySettings::setShowMessageShot,
	});

	builder.addSkip();
	builder.addDividerText(tr::luxury_SettingsShowMessageShotDescription());
	builder.addSkip();
}

void BuildMarks(
		SectionBuilder &builder,
		LuxurySectionBuilder &luxury,
		std::shared_ptr<PreviewState> previewState) {
	auto *settings = &LuxurySettings::getInstance();
	const auto controller = builder.controller();

	builder.addSubsectionTitle(tr::lng_settings_messages());

	builder.add([=](const WidgetContext &ctx) -> SectionBuilder::WidgetToAdd {
		auto preview = object_ptr<MessagePreview>(ctx.container, controller);
		previewState->widget = preview.data();
		return {
			.widget = std::move(preview),
			.margin = style::margins(
				0,
				st::defaultVerticalListSkip,
				0,
				st::settingsPrivacySkipTop),
		};
	});

	luxury.addSettingToggle({
		.id = u"luxury/replaceBottomInfoWithIcons"_q,
		.altIds = { u"luxury/replaceEditedWithIcon"_q },
		.title = tr::luxury_ReplaceMarksWithIcons(),
		.getter = &LuxurySettings::replaceBottomInfoWithIcons,
		.setter = &LuxurySettings::setReplaceBottomInfoWithIcons,
	});

	builder.scope([&] {
		builder.addButton({
			.id = u"luxury/deletedMark"_q,
			.title = tr::luxury_DeletedMarkText(),
			.st = &st::settingsButtonNoIcon,
			.label = LuxurySettings::getInstance().deletedMarkValue(),
			.onClick = [=] {
				auto box = Box<EditMarkBox>(
					tr::luxury_DeletedMarkText(),
					settings->deletedMark(),
					QString("🧹"),
					[=](const QString &value) {
						LuxurySettings::getInstance().setDeletedMark(value);
					});
				Ui::show(std::move(box));
			},
		});

		builder.addButton({
			.id = u"luxury/editedMark"_q,
			.title = tr::luxury_EditedMarkText(),
			.st = &st::settingsButtonNoIcon,
			.label = LuxurySettings::getInstance().editedMarkValue(),
			.onClick = [=] {
				auto box = Box<EditMarkBox>(
					tr::luxury_EditedMarkText(),
					settings->editedMark(),
					tr::lng_edited(tr::now),
					[=](const QString &value) {
						LuxurySettings::getInstance().setEditedMark(value);
					});
				Ui::show(std::move(box));
			},
		});
	}, LuxurySettings::getInstance().replaceBottomInfoWithIconsValue()
		| rpl::map([](bool v) { return !v; }));

	luxury.addSettingToggle({
		.id = u"luxury/removeMessageTail"_q,
		.title = tr::luxury_RemoveMessageTail(),
		.getter = &LuxurySettings::removeMessageTail,
		.setter = &LuxurySettings::setRemoveMessageTail,
	});

	luxury.addSettingToggle({
		.id = u"luxury/hideFastShare"_q,
		.altIds = { u"luxury/hideShareButton"_q },
		.title = tr::luxury_HideShareButton(),
		.getter = &LuxurySettings::hideFastShare,
		.setter = &LuxurySettings::setHideFastShare,
	});
	luxury.addSettingToggle({
		.id = u"luxury/simpleQuotesAndReplies"_q,
		.altIds = { u"luxury/disableColorfulReplies"_q, u"luxury/replyElements"_q },
		.title = tr::luxury_SimpleQuotesAndReplies(),
		.getter = &LuxurySettings::simpleQuotesAndReplies,
		.setter = &LuxurySettings::setSimpleQuotesAndReplies,
	});

	const auto semiTransparent = luxury.addSettingToggle({
		.id = u"luxury/semiTransparentDeletedMessages"_q,
		.altIds = { u"luxury/translucentDeletedMessages"_q },
		.title = tr::luxury_SemiTransparentDeletedMessages(),
		.getter = &LuxurySettings::semiTransparentDeletedMessages,
		.setter = &LuxurySettings::setSemiTransparentDeletedMessages,
	});
	if (semiTransparent) {
		luxury.addBetaBadge(semiTransparent);
	}

	luxury.addSectionDivider();
}

void BuildWideMessagesMultiplier(
		SectionBuilder &builder,
		LuxurySectionBuilder &luxury,
		std::shared_ptr<PreviewState> previewState) {
	auto *settings = &LuxurySettings::getInstance();

	constexpr auto kMinSize = 1.00;
	constexpr auto kStep = 0.05;

	const auto valueToIndex = [=](double value) {
		return static_cast<int>(std::round((value - kMinSize) / kStep));
	};

	const auto controller = builder.controller();
	luxury.addSlider({
		.id = u"luxury/messageBubbleRadius"_q,
		.title = tr::luxury_MessageBubbleRadius(),
		.steps = 17,
		.current = settings->messageBubbleRadius(),
		.indexToValue = [](int index) { return index; },
		.onChanged = [=](int index) {
			if (previewState->widget) {
				previewState->widget->setBubbleRadius(index);
			}
		},
		.onFinalChanged = [=](int index) {
			if (previewState->widget) {
				previewState->widget->setBubbleRadius(index);
			}
			LuxurySettings::getInstance().setMessageBubbleRadius(index);
			ShowRestartPrompt(controller);
		},
		.formatLabel = [](int index) {
			return QString::number(index);
		},
	});

	luxury.addSectionDivider();

	luxury.addSlider({
		.id = u"luxury/wideMultiplier"_q,
		.title = tr::luxury_SettingsWideMultiplier(),
		.steps = 61, // (4.00 - 1.00) / 0.05 + 1
		.current = valueToIndex(settings->wideMultiplier()),
		.indexToValue = [](int index) { return index; },
		.onChanged = nullptr,
		.onFinalChanged = [=](int index) {
			LuxurySettings::getInstance().setWideMultiplier(
				kMinSize + index * kStep);
			ShowRestartPrompt(controller);
		},
		.formatLabel = [=](int index) {
			return QString::number(kMinSize + index * kStep, 'f', 2);
		},
	});

	builder.addSkip();
	builder.addDividerText(tr::luxury_SettingsWideMultiplierDescription());
	builder.addSkip();
}

void BuildContextMenuElements(SectionBuilder &builder, LuxurySectionBuilder &luxury) {
	auto *settings = &LuxurySettings::getInstance();

	builder.addSubsectionTitle(tr::luxury_ContextMenuElementsHeader());

	const auto options = std::vector{
		tr::luxury_SettingsContextMenuItemHidden(tr::now),
		tr::luxury_SettingsContextMenuItemShown(tr::now),
		tr::luxury_SettingsContextMenuItemExtended(tr::now),
	};

	luxury.addChooseButton({
		.id = u"luxury/showReactionsPanelInContextMenu"_q,
		.title = tr::luxury_SettingsContextMenuReactionsPanel(),
		.boxTitle = tr::luxury_SettingsContextMenuTitle(),
		.initialSelection = static_cast<int>(settings->showReactionsPanelInContextMenu()),
		.options = options,
		.setter = [](int i) { LuxurySettings::getInstance().setShowReactionsPanelInContextMenu(static_cast<ContextMenuVisibility>(i)); },
		.icon = { &st::menuIconReactions },
	});
	luxury.addChooseButton({
		.id = u"luxury/showViewsPanelInContextMenu"_q,
		.title = tr::luxury_SettingsContextMenuViewsPanel(),
		.boxTitle = tr::luxury_SettingsContextMenuTitle(),
		.initialSelection = static_cast<int>(settings->showViewsPanelInContextMenu()),
		.options = options,
		.setter = [](int i) { LuxurySettings::getInstance().setShowViewsPanelInContextMenu(static_cast<ContextMenuVisibility>(i)); },
		.icon = { &st::menuIconShowInChat },
	});
	luxury.addChooseButton({
		.id = u"luxury/showHideMessageInContextMenu"_q,
		.title = tr::luxury_ContextHideMessage(),
		.boxTitle = tr::luxury_SettingsContextMenuTitle(),
		.initialSelection = static_cast<int>(settings->showHideMessageInContextMenu()),
		.options = options,
		.setter = [](int i) { LuxurySettings::getInstance().setShowHideMessageInContextMenu(static_cast<ContextMenuVisibility>(i)); },
		.icon = { &st::menuIconClear },
	});
	luxury.addChooseButton({
		.id = u"luxury/showUserMessagesInContextMenu"_q,
		.title = tr::luxury_UserMessagesMenuText(),
		.boxTitle = tr::luxury_SettingsContextMenuTitle(),
		.initialSelection = static_cast<int>(settings->showUserMessagesInContextMenu()),
		.options = options,
		.setter = [](int i) { LuxurySettings::getInstance().setShowUserMessagesInContextMenu(static_cast<ContextMenuVisibility>(i)); },
		.icon = { &st::menuIconTTL },
	});
	luxury.addChooseButton({
		.id = u"luxury/showMessageDetailsInContextMenu"_q,
		.title = tr::luxury_MessageDetailsPC(),
		.boxTitle = tr::luxury_SettingsContextMenuTitle(),
		.initialSelection = static_cast<int>(settings->showMessageDetailsInContextMenu()),
		.options = options,
		.setter = [](int i) { LuxurySettings::getInstance().setShowMessageDetailsInContextMenu(static_cast<ContextMenuVisibility>(i)); },
		.icon = { &st::menuIconInfo },
	});
	luxury.addChooseButton({
		.id = u"luxury/showRepeatMessageInContextMenu"_q,
		.title = tr::luxury_RepeatMessage(),
		.boxTitle = tr::luxury_SettingsContextMenuTitle(),
		.initialSelection = static_cast<int>(settings->showRepeatMessageInContextMenu()),
		.options = options,
		.setter = [](int i) { LuxurySettings::getInstance().setShowRepeatMessageInContextMenu(static_cast<ContextMenuVisibility>(i)); },
		.icon = { &st::luxuryRepeatMenuIcon },
	});
	if (settings->filtersEnabled()) {
		luxury.addChooseButton({
			.id = u"luxury/showAddFilterInContextMenu"_q,
			.title = tr::luxury_RegexFilterQuickAdd(),
			.boxTitle = tr::luxury_SettingsContextMenuTitle(),
			.initialSelection = static_cast<int>(settings->showAddFilterInContextMenu()),
			.options = options,
			.setter = [](int i) { LuxurySettings::getInstance().setShowAddFilterInContextMenu(static_cast<ContextMenuVisibility>(i)); },
			.icon = { &st::menuIconAddToFolder },
		});
	}

	builder.addSkip();
	builder.addDividerText(tr::luxury_SettingsContextMenuDescription());
	builder.addSkip();
}

void BuildMessageFieldElements(SectionBuilder &builder, LuxurySectionBuilder &luxury) {
	builder.addSubsectionTitle(tr::luxury_MessageFieldElementsHeader());

	luxury.addSettingToggle({
		.id = u"luxury/showAttachButtonInMessageField"_q,
		.title = tr::luxury_MessageFieldElementAttach(),
		.getter = &LuxurySettings::showAttachButtonInMessageField,
		.setter = &LuxurySettings::setShowAttachButtonInMessageField,
		.icon = { &st::messageFieldAttachIcon },
	});
	luxury.addSettingToggle({
		.id = u"luxury/showCommandsButtonInMessageField"_q,
		.title = tr::luxury_MessageFieldElementCommands(),
		.getter = &LuxurySettings::showCommandsButtonInMessageField,
		.setter = &LuxurySettings::setShowCommandsButtonInMessageField,
		.icon = { &st::messageFieldCommandsIcon },
	});
	luxury.addSettingToggle({
		.id = u"luxury/showAutoDeleteButtonInMessageField"_q,
		.title = tr::luxury_MessageFieldElementTTL(),
		.getter = &LuxurySettings::showAutoDeleteButtonInMessageField,
		.setter = &LuxurySettings::setShowAutoDeleteButtonInMessageField,
		.icon = { &st::messageFieldTTLIcon },
	});
	luxury.addSettingToggle({
		.id = u"luxury/showEmojiButtonInMessageField"_q,
		.title = tr::luxury_MessageFieldElementEmoji(),
		.getter = &LuxurySettings::showEmojiButtonInMessageField,
		.setter = &LuxurySettings::setShowEmojiButtonInMessageField,
		.icon = { &st::messageFieldEmojiIcon },
	});
	luxury.addSettingToggle({
		.id = u"luxury/showMicrophoneButtonInMessageField"_q,
		.title = tr::luxury_MessageFieldElementVoice(),
		.getter = &LuxurySettings::showMicrophoneButtonInMessageField,
		.setter = &LuxurySettings::setShowMicrophoneButtonInMessageField,
		.icon = { &st::messageFieldVoiceIcon },
	});
	luxury.addSettingToggle({
		.id = u"luxury/showGiftButtonInMessageField"_q,
		.title = tr::lng_profile_action_short_gift(),
		.getter = &LuxurySettings::showGiftButtonInMessageField,
		.setter = &LuxurySettings::setShowGiftButtonInMessageField,
		.icon = { &st::settingsButtonIconGift },
	});
	luxury.addSettingToggle({
		.id = u"luxury/showAiEditorButtonInMessageField"_q,
		.title = tr::lng_ai_compose_title(),
		.getter = &LuxurySettings::showAiEditorButtonInMessageField,
		.setter = &LuxurySettings::setShowAiEditorButtonInMessageField,
		.icon = { &st::messageFieldCocoonAiIcon },
	});

	luxury.addSectionDivider();
}

void BuildMessageFieldPopups(SectionBuilder &builder, LuxurySectionBuilder &luxury) {
	builder.addSubsectionTitle(tr::luxury_MessageFieldPopupsHeader());

	luxury.addSettingToggle({
		.id = u"luxury/showAttachPopup"_q,
		.title = tr::luxury_MessageFieldElementAttach(),
		.getter = &LuxurySettings::showAttachPopup,
		.setter = &LuxurySettings::setShowAttachPopup,
		.icon = { &st::messageFieldAttachIcon },
	});
	luxury.addSettingToggle({
		.id = u"luxury/showEmojiPopup"_q,
		.title = tr::luxury_MessageFieldElementEmoji(),
		.getter = &LuxurySettings::showEmojiPopup,
		.setter = &LuxurySettings::setShowEmojiPopup,
		.icon = { &st::messageFieldEmojiIcon },
	});
}

const auto kMeta = BuildHelper({
	.id = LuxuryChats::Id(),
	.parentId = LuxuryMain::Id(),
	.title = &tr::luxury_CategoryChats,
	.icon = &st::menuIconChatBubble,
}, [](SectionBuilder &builder) {
	auto luxury = LuxurySectionBuilder(builder);
	const auto previewState = std::make_shared<PreviewState>();

	builder.addSkip();
	BuildStickersAndEmoji(builder, luxury);
	BuildGroupsAndChannels(builder, luxury);
	BuildMarks(builder, luxury, previewState);
	BuildWideMessagesMultiplier(builder, luxury, previewState);
	BuildContextMenuElements(builder, luxury);
	BuildMessageFieldElements(builder, luxury);
	BuildMessageFieldPopups(builder, luxury);
	builder.addSkip();
});

} // namespace

rpl::producer<QString> LuxuryChats::title() {
	return tr::luxury_CategoryChats();
}

LuxuryChats::LuxuryChats(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

void LuxuryChats::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kMeta.build);
	Ui::ResizeFitChild(this, content);
}

Type LuxuryChatsId() {
	return LuxuryChats::Id();
}

} // namespace Settings
