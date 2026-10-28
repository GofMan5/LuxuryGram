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
#include "base/timer_rpl.h"
#include "boxes/abstract_box.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "core/crash_reports.h"
#include "lang/lang_text_entity.h"
#include "main/main_account.h"
#include "main/main_session.h"
#include "mtproto/mtp_instance.h"
#include "mtproto/details/mtproto_websocket_logic.h"
#include "settings/settings_builder.h"
#include "settings/settings_common.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "ui/vertical_list.h"
#include "ui/boxes/confirm_box.h"
#include "ui/boxes/single_choice_box.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "window/themes/window_theme.h"

#include <QtGui/QClipboard>
#include <QtGui/QGuiApplication>

namespace Settings {

using namespace Builder;
using namespace LuxuryBuilder;

namespace {

void BuildCrashReporting(SectionBuilder &builder, LuxurySectionBuilder &luxury) {
#ifndef TDESKTOP_DISABLE_AUTOUPDATE
	if (!*CrashReports::kReportEndpoint) {
		return;
	}
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

constexpr auto kWsStatusRefresh = crl::time(2000);

[[nodiscard]] QString WebSocketTransportName(WebSocketTransport mode) {
	switch (mode) {
	case WebSocketTransport::Off: return tr::luxury_WebSocketTransportOff(tr::now);
	case WebSocketTransport::Always: return tr::luxury_WebSocketTransportAlways(tr::now);
	case WebSocketTransport::Auto: break;
	}
	return tr::luxury_WebSocketTransportAuto(tr::now);
}

[[nodiscard]] QString WsStatusText(Window::SessionController *controller) {
	if (Core::App().settings().proxy().isEnabled()) {
		return tr::luxury_WebSocketStatusProxy(tr::now);
	} else if (LuxurySettings::getInstance().webSocketTransport()
		== WebSocketTransport::Off) {
		return tr::luxury_WebSocketStatusOff(tr::now);
	}
	const auto account = controller
		? &controller->session().account()
		: &Core::App().activeAccount();
	if (!account->sessionExists()) {
		return tr::luxury_WebSocketStatusConnecting(tr::now);
	}
	const auto transport = account->mtp().dctransport();
	if (transport.startsWith(u"WebSocket"_q)) {
		return tr::luxury_WebSocketStatusActive(tr::now);
	} else if (!transport.isEmpty()) {
		return tr::luxury_WebSocketStatusDirect(
			tr::now,
			lt_transport,
			transport);
	}
	return tr::luxury_WebSocketStatusConnecting(tr::now);
}

[[nodiscard]] QString WsLogText() {
	auto result = QStringList();
	for (const auto &line : MTP::details::WsLog::Instance().lines()) {
		result.push_back(QString::fromStdString(line));
	}
	return result.join('\n');
}

void WsLogBox(not_null<Ui::GenericBox*> box) {
	box->setTitle(tr::lng_luxury_plugins_ui_logs_title());
	box->setWidth(st::boxWideWidth);
	const auto text = WsLogText();
	const auto label = box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		rpl::single(text.isEmpty()
			? TextWithEntities{ tr::lng_luxury_plugins_ui_logs_empty(tr::now) }
			: Ui::Text::Code(text)),
		st::boxDividerLabel));
	label->setSelectable(true);
	box->addButton(tr::lng_luxury_plugins_ui_logs_copy(), [=] {
		QGuiApplication::clipboard()->setText(text);
		box->showToast(tr::lng_text_copied(tr::now));
	});
	box->addButton(tr::lng_luxury_plugins_ui_logs_clear(), [=] {
		MTP::details::WsLog::Instance().clear();
		box->closeBox();
	});
	box->addButton(tr::lng_close(), [=] {
		box->closeBox();
	});
}

void BuildNetwork(SectionBuilder &builder) {
	const auto controller = builder.controller();
	const auto options = std::vector{
		WebSocketTransport::Off,
		WebSocketTransport::Auto,
		WebSocketTransport::Always,
	};

	builder.addSkip();
	builder.addSubsectionTitle(tr::luxury_NetworkHeader());
	builder.addButton({
		.id = u"luxury/webSocketTransport"_q,
		.title = tr::luxury_WebSocketTransport(),
		.icon = { &st::menuIconNetwork },
		.label = LuxurySettings::getInstance().webSocketTransportValue()
			| rpl::map(WebSocketTransportName),
		.onClick = [=] {
			if (!controller) {
				return;
			} else if (Core::App().settings().proxy().isEnabled()) {
				controller->showToast(
					tr::luxury_WebSocketTransportProxyActive(tr::now));
				return;
			}
			controller->show(Box([=](not_null<Ui::GenericBox*> box) {
				auto labels = std::vector<QString>();
				for (const auto option : options) {
					labels.push_back(WebSocketTransportName(option));
				}
				const auto current
					= LuxurySettings::getInstance().webSocketTransport();
				SingleChoiceBox(box, {
					.title = tr::luxury_WebSocketTransport(),
					.options = labels,
					.initialSelection = int(ranges::find(options, current)
						- begin(options)),
					.callback = [=](int index) {
						LuxurySettings::getInstance().setWebSocketTransport(
							options[index]);
					},
				});
			}));
		},
		.keywords = { u"websocket"_q, u"network"_q, u"connection"_q, u"blocked"_q },
	});
	// The factory runs only when widgets are built, not in the search pass.
	builder.addControl({
		.factory = [=](not_null<Ui::VerticalLayout*> container) {
			return object_ptr<Ui::FlatLabel>(
				container,
				rpl::merge(
					// Mode changes notify connectionType as well.
					Core::App().settings().proxy().connectionTypeValue(),
					base::timer_each(kWsStatusRefresh)
				) | rpl::map([=] { return WsStatusText(controller); }),
				st::boxDividerLabel);
		},
		.margin = st::defaultBoxDividerLabelPadding,
	});
	builder.addDividerText(tr::luxury_WebSocketTransportDescription());
	builder.addButton({
		.id = u"luxury/webSocketLog"_q,
		.title = tr::luxury_WebSocketLog(),
		.icon = { &st::menuIconReport },
		.onClick = [=] {
			if (controller) {
				controller->show(Box(WsLogBox));
			}
		},
	});
	builder.addSkip();
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
					const auto needsRestart = LuxurySettings::reset();
					if (needsRestart) {
						ShowRestartPrompt(controller);
					} else {
						controller->showToast(tr::lng_box_done(tr::now));
					}
					close();
				},
				.confirmText = tr::luxury_BoxActionReset(),
				.confirmStyle = &st::attentionBoxButton,
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

	BuildCrashReporting(builder, luxury);
	BuildNetwork(builder);
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

} // namespace Settings
