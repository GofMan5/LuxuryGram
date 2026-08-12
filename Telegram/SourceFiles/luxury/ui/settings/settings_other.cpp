// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "luxury/ui/settings/settings_other.h"

#include "lang_auto.h"
#include "luxury/luxury_settings.h"
#include "luxury/ui/settings/luxury_builder.h"
#include "luxury/ui/settings/settings_luxury_utils.h"
#include "luxury/ui/settings/settings_main.h"
#include "boxes/abstract_box.h"
#include "core/application.h"
#include "lang/lang_text_entity.h"
#include "settings/settings_builder.h"
#include "settings/settings_common.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "ui/vertical_list.h"
#include "ui/boxes/confirm_box.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "window/themes/window_theme.h"


namespace Settings {

using namespace Builder;
using namespace LuxuryBuilder;

namespace {

void BuildCrashReporting(SectionBuilder &builder, LuxurySectionBuilder &luxury) {
#ifndef TDESKTOP_DISABLE_AUTOUPDATE
	builder.addSkip();
	builder.addSubsectionTitle(tr::luxury_CategoryOther());

	luxury.addSettingToggle({
		.id = u"luxury/crashReporting"_q,
		.altIds = { u"luxury/crashlytics"_q },
		.title = tr::luxury_CrashReporting(),
		.getter = &LuxurySettings::crashReporting,
		.setter = &LuxurySettings::setCrashReporting,
		.icon = { &st::menuIconReport },
	});
	builder.addSkip();
	builder.addDividerText(tr::luxury_CrashReportingDescription());
#endif
}

void BuildOtherThings(SectionBuilder &builder) {
	const auto controller = builder.controller();

	builder.addSkip();
	builder.addButton({
		.id = u"luxury/registerUrlScheme"_q,
		.title = tr::luxury_RegisterURLScheme(),
		.icon = { &st::menuIconLink },
		.onClick = [=] {
			Core::Application::RegisterUrlScheme();
			controller->showToast(tr::lng_box_done(tr::now));
		},
	});
	builder.addButton({
		.id = u"luxury/resetSettings"_q,
		.title = tr::luxury_ResetSettings(),
		.icon = { &st::menuIconRestore },
		.onClick = [=] {
			controller->show(Ui::MakeConfirmBox({
				.text = tr::luxury_ResetSettingsConfirmation(tr::rich),
				.confirmed = [=](Fn<void()> &&close) {
					LuxurySettings::reset();
					controller->showToast(tr::lng_box_done(tr::now));
					close();
				},
				.confirmText = tr::lng_box_yes(),
			}));
		},
	});
	builder.addSkip();
}

const auto kMeta = BuildHelper({
	.id = LuxuryOther::Id(),
	.parentId = LuxuryMain::Id(),
	.title = &tr::luxury_CategoryOther,
	.icon = &st::menuIconFave,
}, [](SectionBuilder &builder) {
	auto luxury = LuxurySectionBuilder(builder);

	builder.addSkip();
	BuildCrashReporting(builder, luxury);
	BuildOtherThings(builder);
});

} // namespace

rpl::producer<QString> LuxuryOther::title() {
	return tr::luxury_CategoryOther();
}

LuxuryOther::LuxuryOther(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

void LuxuryOther::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kMeta.build);
	Ui::ResizeFitChild(this, content);
}

Type LuxuryOtherId() {
	return LuxuryOther::Id();
}

} // namespace Settings
