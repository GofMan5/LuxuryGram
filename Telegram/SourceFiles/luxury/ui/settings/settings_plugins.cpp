// This is the source code of LuxuryGram for Desktop.
// See LEGAL for license and copyright information.
#include "luxury/ui/settings/settings_plugins.h"

#include "lang_auto.h"
#include "luxury/plugins/plugin_manager.h"
#include "luxury/ui/boxes/plugin_details_box.h"
#include "luxury/ui/boxes/plugin_install_box.h"
#include "luxury/ui/settings/settings_main.h"
#include "settings/settings_builder.h"
#include "settings/settings_common.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "ui/layers/generic_box.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"

namespace Settings {

using namespace Builder;
namespace Plugins = Luxury::Plugins;

namespace {

[[nodiscard]] QString MetaText(const Plugins::PluginView &plugin) {
	auto parts = QStringList{ plugin.version };
	if (!plugin.author.isEmpty()) {
		parts.push_back(plugin.author);
	}
	parts.push_back(plugin.sourceLabel);
	return parts.join(u" · "_q);
}

void AddStateLabel(
		not_null<Ui::VerticalLayout*> list,
		const Plugins::PluginView &plugin) {
	const auto &padding = st::settingsButtonNoIcon.padding;
	const auto margin = style::margins(padding.left(), 0, padding.right(), 0);
	const auto text = !plugin.lastError.isEmpty()
		? plugin.lastError
		: plugin.updateAvailable
		? tr::lng_luxury_plugins_ui_state_update(
			tr::now,
			lt_version,
			plugin.availableLabel)
		: !plugin.enabled
		? tr::lng_luxury_plugins_ui_state_disabled(tr::now)
		: plugin.running
		? tr::lng_luxury_plugins_ui_state_running(tr::now)
		: tr::lng_luxury_plugins_ui_state_stopped(tr::now);
	const auto label = list->add(
		object_ptr<Ui::FlatLabel>(list, text, st::boxDividerLabel),
		margin);
	if (!plugin.lastError.isEmpty()) {
		label->setTextColorOverride(st::boxTextFgError->c);
	} else if (plugin.updateAvailable) {
		label->setTextColorOverride(st::windowActiveTextFg->c);
	}
}

void FillList(
		not_null<Ui::VerticalLayout*> list,
		std::shared_ptr<Ui::Show> show) {
	const auto &padding = st::settingsButtonNoIcon.padding;
	const auto margin = style::margins(padding.left(), 0, padding.right(), 0);
	const auto manager = Plugins::Manager::Maybe();
	const auto plugins = manager
		? manager->plugins()
		: std::vector<Plugins::PluginView>();
	if (plugins.empty()) {
		Ui::AddSkip(list);
		list->add(
			object_ptr<Ui::FlatLabel>(
				list,
				(manager
					? tr::lng_luxury_plugins_ui_empty(tr::now)
					: tr::lng_luxury_plugins_ui_unavailable(tr::now)),
				st::boxDividerLabel),
			margin);
		Ui::AddSkip(list);
		return;
	}
	for (const auto &plugin : plugins) {
		const auto id = plugin.id;
		Ui::AddSkip(list);
		const auto toggle = list->add(object_ptr<Ui::SettingsButton>(
			list,
			rpl::single(plugin.name),
			st::settingsButtonNoIcon));
		toggle->toggleOn(rpl::single(plugin.enabled));
		toggle->toggledChanges() | rpl::on_next([=](bool enabled) {
			if (const auto manager = Plugins::Manager::Maybe()) {
				manager->setEnabled(id, enabled);
			}
		}, toggle->lifetime());
		list->add(
			object_ptr<Ui::FlatLabel>(
				list,
				MetaText(plugin),
				st::boxDividerLabel),
			margin);
		AddStateLabel(list, plugin);
		list->add(object_ptr<Ui::SettingsButton>(
			list,
			tr::lng_luxury_plugins_ui_details(),
			st::settingsButtonNoIcon
		))->setClickedCallback([=] {
			show->showBox(Box(Plugins::FillPluginDetailsBox, show, id));
		});
		Ui::AddSkip(list);
		Ui::AddDivider(list);
	}
}

void BuildActions(SectionBuilder &builder) {
	const auto controller = builder.controller();

	builder.addSkip();
	builder.addButton({
		.id = u"luxury/plugins/install"_q,
		.title = tr::lng_luxury_plugins_ui_install_github(),
		.icon = { &st::menuIconDownload },
		.onClick = [=] {
			const auto show = controller->uiShow();
			show->showBox(Box(Plugins::FillPluginInstallBox, show));
		},
	});
	builder.addButton({
		.id = u"luxury/plugins/checkUpdates"_q,
		.title = tr::lng_luxury_plugins_ui_check_updates(),
		.icon = { &st::menuIconRestore },
		.onClick = [=] {
			const auto show = controller->uiShow();
			const auto manager = Plugins::Manager::Maybe();
			if (!manager) {
				show->showToast(tr::lng_luxury_plugins_ui_unavailable(tr::now));
				return;
			}
			show->showToast(tr::lng_luxury_plugins_ui_checking(tr::now));
			manager->checkUpdates([=](int available) {
				show->showToast(available
					? tr::lng_luxury_plugins_ui_updates_found(
						tr::now,
						lt_number,
						QString::number(available))
					: tr::lng_luxury_plugins_ui_updates_none(tr::now));
			});
		},
	});
	builder.addSkip();
	builder.addDividerText(tr::lng_luxury_plugins_ui_about());
}

void BuildList(SectionBuilder &builder) {
	builder.addSkip();
	builder.addSubsectionTitle(tr::lng_luxury_plugins_ui_installed_title());
	builder.add([](const WidgetContext &ctx) -> SectionBuilder::WidgetToAdd {
		auto owned = object_ptr<Ui::VerticalLayout>(ctx.container);
		const auto list = owned.data();
		const auto show = std::shared_ptr<Ui::Show>(ctx.controller->uiShow());
		const auto rebuild = [=] {
			list->clear();
			FillList(list, show);
			list->resizeToWidth(list->width());
		};
		rebuild();
		if (const auto manager = Plugins::Manager::Maybe()) {
			// A toggle fires changes() from its own click, so the rows it
			// lives in are rebuilt on the next event loop turn.
			const auto scheduled = std::make_shared<bool>(false);
			manager->changes() | rpl::on_next([=] {
				if (!std::exchange(*scheduled, true)) {
					crl::on_main(list, [=] {
						*scheduled = false;
						rebuild();
					});
				}
			}, list->lifetime());
		}
		return { .widget = std::move(owned) };
	});
}

const auto kMeta = BuildHelper({
	.id = LuxuryPlugins::Id(),
	.parentId = LuxuryMain::Id(),
	.title = &tr::lng_luxury_plugins_ui_title,
	.icon = &st::menuIconCustomize,
}, [](SectionBuilder &builder) {
	BuildActions(builder);
	BuildList(builder);
});

} // namespace

rpl::producer<QString> LuxuryPlugins::title() {
	return tr::lng_luxury_plugins_ui_title();
}

LuxuryPlugins::LuxuryPlugins(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

void LuxuryPlugins::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kMeta.build);
	Ui::ResizeFitChild(this, content);
}

} // namespace Settings
