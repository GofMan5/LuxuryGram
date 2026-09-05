// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "luxury/ui/settings/settings_general.h"

#include "lang_auto.h"
#include "luxury/luxury_settings.h"
#include "luxury/ui/settings/luxury_builder.h"
#include "luxury/ui/settings/settings_luxury_utils.h"
#include "luxury/ui/settings/settings_main.h"
#include "base/platform/base_platform_info.h"
#include "boxes/translate_box.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "lang/lang_text_entity.h"
#include "platform/platform_translate_provider.h"
#include "settings/settings_builder.h"
#include "settings/settings_common.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "ui/boxes/single_choice_box.h"
#include "ui/boxes/choose_language_box.h"
#include "ui/toast/toast.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"

namespace Settings {

using namespace Builder;
using namespace LuxuryBuilder;

namespace {

void BuildTranslator(SectionBuilder &builder, LuxurySectionBuilder &luxury) {
	builder.addSubsectionTitle(tr::lng_translate_settings_subtitle());

	auto *settings = &LuxurySettings::getInstance();

	const auto options = std::vector{
		std::pair(TranslationProvider::Telegram, QString("Telegram")),
		std::pair(TranslationProvider::Google, QString("Google")),
		std::pair(TranslationProvider::Yandex, QString("Yandex")),
	};
	const auto nativeAvailable = Platform::IsTranslateProviderAvailable();
	auto availableOptions = options;
	if (nativeAvailable) {
		availableOptions.push_back(std::pair(
			TranslationProvider::Native,
			[] {
				if constexpr (Platform::IsMac()) {
					return QString("macOS");
				} else if constexpr (Platform::IsWindows()) {
					return QString("Windows");
				} else {
					return QString("Linux");
				}
			}()));
	}
	auto optionLabels = std::vector<QString>();
	optionLabels.reserve(availableOptions.size());
	for (const auto &option : availableOptions) {
		optionLabels.push_back(option.second);
	}

	const auto getIndex = [=](TranslationProvider val) {
		const auto i = ranges::find(
			availableOptions,
			val,
			&std::pair<TranslationProvider, QString>::first);
		return (i != end(availableOptions))
			? int(i - begin(availableOptions))
			: 0;
	};

	auto currentVal = LuxurySettings::getInstance().translationProviderValue()
		| rpl::map(getIndex)
		| rpl::map([=](int val) { return availableOptions[val].second; });

	const auto button = builder.addButton({
		.id = u"luxury/translationProvider"_q,
		.title = tr::luxury_TranslationProvider(),
		.st = &st::settingsButtonNoIcon,
		.label = std::move(currentVal),
		.onClick = [=] {
			if (const auto controller = Core::App().activeWindow()->sessionController()) {
				controller->show(Box(
						[=](not_null<Ui::GenericBox*> box) {
							const auto save = [=](int index) {
								const auto option = availableOptions[index].first;
								LuxurySettings::getInstance().setTranslationProvider(option);

								if constexpr (Platform::IsMac()) {
									if (option == TranslationProvider::Native) {
										controller->showToast(Ui::Toast::Config{
											.text = tr::lng_translate_settings_use_platform_mac_about(tr::now, tr::rich),
											.duration = 6 * crl::time(1000)
										});
									}
								}
							};
							SingleChoiceBox(box, {
								.title = tr::luxury_TranslationProvider(),
								.options = optionLabels,
								.initialSelection = getIndex(settings->translationProvider()),
								.callback = save,
							});
						}));
			}
		},
	});
	if (button) {
		luxury.addBetaBadge(button);
	}
	builder.addButton({
		.id = u"luxury/translationLanguage"_q,
		.title = tr::lng_translate_menu_to(),
		.st = &st::settingsButtonNoIcon,
		.label = Core::App().settings().translateToValue()
			| rpl::map([](LanguageId id) {
				return Ui::LanguageName(id);
			}),
		.onClick = [] {
			if (const auto controller = Core::App().activeWindow()->sessionController()) {
				controller->show(Ui::ChooseTranslateToBox(
					Core::App().settings().translateTo(),
					[](LanguageId) {}));
			}
		},
	});
}

void BuildShowPeerId(SectionBuilder &builder) {
	auto *settings = &LuxurySettings::getInstance();

	const auto options = std::vector{
		QString(tr::luxury_SettingsShowID_Hide(tr::now)),
		QString("Telegram API"),
		QString("Bot API")
	};

	auto currentVal = LuxurySettings::getInstance().showPeerIdValue()
		| rpl::map([=](PeerIdDisplay val) {
			return options[static_cast<int>(val)];
		});

	const auto controller = builder.controller();
	builder.addButton({
		.id = u"luxury/showPeerId"_q,
		.altIds = { u"luxury/showIdAndDc"_q },
		.title = tr::luxury_SettingsShowID(),
		.st = &st::settingsButtonNoIcon,
		.label = std::move(currentVal),
		.onClick = [=] {
			controller->show(Box(
				[=](not_null<Ui::GenericBox*> box) {
					const auto save = [=](int index) {
						LuxurySettings::getInstance().setShowPeerId(
							static_cast<PeerIdDisplay>(index));
					};
					SingleChoiceBox(box, {
						.title = tr::luxury_SettingsShowID(),
						.options = options,
						.initialSelection = static_cast<int>(settings->showPeerId()),
						.callback = save,
					});
				}));
		},
	});
}

void BuildQoLToggles(SectionBuilder &builder, LuxurySectionBuilder &luxury) {
	auto *settings = &LuxurySettings::getInstance();

	BuildTranslator(builder, luxury);
	luxury.addSectionDivider();

	builder.addSubsectionTitle(tr::luxury_CategoryGeneral());

	const auto controller = builder.controller();
	luxury.addToggle({
		.id = u"luxury/disableStories"_q,
		.altIds = { u"luxury/hideStories"_q },
		.title = tr::luxury_DisableStories(),
		.getter = [=] { return settings->disableStories(); },
		.setter = [=](bool enabled) {
			LuxurySettings::getInstance().setDisableStories(enabled);
			ShowRestartPrompt(controller);
		},
	});

	luxury.addSettingToggle({
		.id = u"luxury/disableOpenLinkWarning"_q,
		.title = tr::luxury_DisableOpenLinkWarning(),
		.getter = &LuxurySettings::disableOpenLinkWarning,
		.setter = &LuxurySettings::setDisableOpenLinkWarning,
	});

	luxury.addCollapsibleToggle({
		.id = u"luxury/similarChannels"_q,
		.title = tr::luxury_DisableSimilarChannels(),
		.checkboxes = {
			NestedEntry{
				tr::luxury_CollapseSimilarChannels(tr::now),
				[] { return LuxurySettings::getInstance().collapseSimilarChannels(); },
				[](bool v) { LuxurySettings::getInstance().setCollapseSimilarChannels(v); }
			},
			NestedEntry{
				tr::luxury_HideSimilarChannelsTab(tr::now),
				[] { return LuxurySettings::getInstance().hideSimilarChannels(); },
				[](bool v) { LuxurySettings::getInstance().setHideSimilarChannels(v); }
			}
		},
		.toggledWhenAll = true,
	});

	luxury.addSettingToggle({
		.id = u"luxury/disableNotificationsDelay"_q,
		.title = tr::luxury_DisableNotificationsDelay(),
		.getter = &LuxurySettings::disableNotificationsDelay,
		.setter = &LuxurySettings::setDisableNotificationsDelay,
	});

	luxury.addSectionDivider();

	const auto zalgoButton = builder.addButton({
		.id = u"luxury/filterZalgo"_q,
		.title = tr::luxury_FilterZalgo(),
		.st = &st::settingsButtonNoIcon,
		.toggled = rpl::single(settings->filterZalgo()),
	});
	if (zalgoButton) {
		zalgoButton->toggledValue(
		) | rpl::filter(
			[=](bool enabled) {
				return (enabled != settings->filterZalgo());
			}
		) | on_next(
			[=](bool enabled) {
				LuxurySettings::getInstance().setFilterZalgo(enabled);
				ShowRestartPrompt(controller);
			},
			zalgoButton->lifetime());
		luxury.addBetaBadge(zalgoButton);
	}

	luxury.addSettingToggle({
		.id = u"luxury/improveLinkPreviews"_q,
		.title = tr::luxury_ImproveLinkPreviews(),
		.getter = &LuxurySettings::improveLinkPreviews,
		.setter = &LuxurySettings::setImproveLinkPreviews,
	});
	luxury.addCollapsibleToggle({
		.id = u"luxury/confirmations"_q,
		.title = tr::luxury_ConfirmationsTitle(),
		.checkboxes = {
			NestedEntry{
				tr::luxury_StickerConfirmation(tr::now),
				[] { return LuxurySettings::getInstance().stickerConfirmation(); },
				[](bool v) { LuxurySettings::getInstance().setStickerConfirmation(v); }
			},
			NestedEntry{
				tr::luxury_GIFConfirmation(tr::now),
				[] { return LuxurySettings::getInstance().gifConfirmation(); },
				[](bool v) { LuxurySettings::getInstance().setGifConfirmation(v); }
			},
			NestedEntry{
				tr::luxury_VoiceConfirmation(tr::now),
				[] { return LuxurySettings::getInstance().voiceConfirmation(); },
				[](bool v) { LuxurySettings::getInstance().setVoiceConfirmation(v); }
			},
			NestedEntry{
				tr::luxury_RoundConfirmation(tr::now),
				[] { return LuxurySettings::getInstance().roundConfirmation(); },
				[](bool v) { LuxurySettings::getInstance().setRoundConfirmation(v); }
			}
		},
		.toggledWhenAll = false,
	});
	luxury.addSettingToggle({
		.id = u"luxury/showMessageSeconds"_q,
		.altIds = { u"luxury/formatTimeWithSeconds"_q },
		.title = tr::luxury_SettingsShowMessageSeconds(),
		.getter = &LuxurySettings::showMessageSeconds,
		.setter = &LuxurySettings::setShowMessageSeconds,
	});
	luxury.addSettingToggle({
		.id = u"luxury/showLastSeenSeconds"_q,
		.title = tr::luxury_SettingsShowLastSeenSeconds(),
		.getter = &LuxurySettings::showLastSeenSeconds,
		.setter = &LuxurySettings::setShowLastSeenSeconds,
	});

	BuildShowPeerId(builder);

	luxury.addSectionDivider();

	builder.addSubsectionTitle(rpl::single(QString("Webview")));

	luxury.addSettingToggle({
		.id = u"luxury/spoofWebviewAsAndroid"_q,
		.title = tr::luxury_SettingsSpoofWebviewAsAndroid(),
		.getter = &LuxurySettings::spoofWebviewAsAndroid,
		.setter = &LuxurySettings::setSpoofWebviewAsAndroid,
	});

	luxury.addCollapsibleToggle({
		.id = u"luxury/biggerWindow"_q,
		.title = tr::luxury_SettingsBiggerWindow(),
		.checkboxes = {
			NestedEntry{
				tr::luxury_SettingsIncreaseWebviewHeight(tr::now),
				[] { return LuxurySettings::getInstance().increaseWebviewHeight(); },
				[](bool v) { LuxurySettings::getInstance().setIncreaseWebviewHeight(v); }
			},
			NestedEntry{
				tr::luxury_SettingsIncreaseWebviewWidth(tr::now),
				[] { return LuxurySettings::getInstance().increaseWebviewWidth(); },
				[](bool v) { LuxurySettings::getInstance().setIncreaseWebviewWidth(v); }
			}
		},
		.toggledWhenAll = false,
	});
}

const auto kMeta = BuildHelper({
	.id = LuxuryGeneral::Id(),
	.parentId = LuxuryMain::Id(),
	.title = &tr::luxury_CategoryGeneral,
	.icon = &st::menuIconShowAll,
}, [](SectionBuilder &builder) {
	auto luxury = LuxurySectionBuilder(builder);

	builder.addSkip();
	BuildQoLToggles(builder, luxury);
	builder.addSkip();
});

} // namespace

rpl::producer<QString> LuxuryGeneral::title() {
	return tr::luxury_CategoryGeneral();
}

LuxuryGeneral::LuxuryGeneral(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

void LuxuryGeneral::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kMeta.build);
	Ui::ResizeFitChild(this, content);
}

} // namespace Settings
