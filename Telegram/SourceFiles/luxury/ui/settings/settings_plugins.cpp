// This is the source code of LuxuryGram for Desktop.
// See LEGAL for license and copyright information.
#include "luxury/ui/settings/settings_plugins.h"

#include "lang_auto.h"
#include "luxury/plugins/plugin_manager.h"
#include "luxury/ui/boxes/plugin_consent_box.h"
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

[[nodiscard]] QString StateText(const Plugins::PluginView &plugin) {
	return !plugin.lastError.isEmpty()
		? Plugins::LastErrorText(plugin.lastError)
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
}

// The plugin as the manager has it now and after every change; the row
// updates in place while it exists.
[[nodiscard]] rpl::producer<Plugins::PluginView> PluginValue(
		not_null<Plugins::Manager*> manager,
		const QString &id) {
	return rpl::single(rpl::empty) | rpl::then(
		manager->changes()
	) | rpl::map([=] {
		return manager->plugin(id);
	}) | rpl::filter([](const std::optional<Plugins::PluginView> &plugin) {
		return plugin.has_value();
	}) | rpl::map([](const std::optional<Plugins::PluginView> &plugin) {
		return *plugin;
	});
}

void AddStateLabel(
		not_null<Ui::VerticalLayout*> list,
		not_null<Ui::SettingsButton*> toggle,
		rpl::producer<Plugins::PluginView> plugin) {
	const auto &padding = st::settingsButtonNoIcon.padding;
	const auto margin = style::margins(padding.left(), 0, padding.right(), 0);
	// A long error is elided to three lines, the details box has it all.
	const auto labelSt = list->lifetime().make_state<style::FlatLabel>(
		st::boxDividerLabel);
	labelSt->maxHeight = st::boxDividerLabel.style.font->height * 3;
	const auto label = list->add(
		object_ptr<Ui::FlatLabel>(list, QString(), *labelSt),
		margin);
	std::move(plugin) | rpl::on_next([=](const Plugins::PluginView &view) {
		const auto text = StateText(view);
		label->setText(text);
		label->setTextColorOverride(!view.lastError.isEmpty()
			? std::make_optional(st::boxTextFgError->c)
			: view.updateAvailable
			? std::make_optional(st::windowActiveTextFg->c)
			: std::nullopt);
		toggle->setAccessibleDescription(text);
		toggle->accessibilityDescriptionChanged();
	}, label->lifetime());
}

// "Installing {name}…" with Cancel for every commitInstall in flight.
void FillPending(not_null<Ui::VerticalLayout*> pending) {
	const auto &padding = st::settingsButtonNoIcon.padding;
	const auto margin = style::margins(padding.left(), 0, padding.right(), 0);
	for (const auto &entry : Plugins::PendingInstalls()) {
		const auto id = entry.id;
		Ui::AddSkip(pending);
		pending->add(
			object_ptr<Ui::FlatLabel>(
				pending,
				(entry.cancelled
					? tr::lng_luxury_plugins_ui_install_cancelling(
						tr::now,
						lt_name,
						entry.name)
					: tr::lng_luxury_plugins_ui_installing(
						tr::now,
						lt_name,
						entry.name)),
				st::boxLabel),
			margin);
		if (!entry.cancelled) {
			pending->add(object_ptr<Ui::SettingsButton>(
				pending,
				tr::lng_luxury_plugins_ui_install_cancel(),
				st::settingsAttentionButton
			))->setClickedCallback([=] {
				Plugins::CancelInstall(id);
			});
		}
		Ui::AddSkip(pending);
		Ui::AddDivider(pending);
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
		const auto value = [=] { return PluginValue(manager, id); };
		Ui::AddSkip(list);
		const auto toggle = list->add(object_ptr<Ui::SettingsButton>(
			list,
			value() | rpl::map(&Plugins::PluginView::name),
			st::settingsButtonNoIcon));
		toggle->toggleOn(value() | rpl::map(&Plugins::PluginView::enabled));
		toggle->toggledChanges() | rpl::on_next([=](bool enabled) {
			if (const auto manager = Plugins::Manager::Maybe()) {
				manager->setEnabled(id, enabled);
			}
		}, toggle->lifetime());
		list->add(
			object_ptr<Ui::FlatLabel>(
				list,
				value() | rpl::map(MetaText),
				st::boxDividerLabel),
			margin);
		AddStateLabel(list, toggle, value());
		const auto details = list->add(object_ptr<Ui::SettingsButton>(
			list,
			tr::lng_luxury_plugins_ui_details(),
			st::settingsButtonNoIcon));
		details->setAccessibleDescription(plugin.name);
		details->setClickedCallback([=] {
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
			manager->checkUpdatesDetailed([=](
					Plugins::Manager::UpdateCheck result) {
				auto text = result.available
					? tr::lng_luxury_plugins_ui_updates_found(
						tr::now,
						lt_number,
						QString::number(result.available))
					: result.failed
					? QString()
					: tr::lng_luxury_plugins_ui_updates_none(tr::now);
				if (result.failed) {
					const auto failed = tr::lng_luxury_plugins_ui_check_failed(
						tr::now,
						lt_number,
						QString::number(result.failed),
						lt_error,
						Plugins::ErrorText(
							result.firstCode,
							result.firstError));
					text = text.isEmpty() ? failed : (text + '\n' + failed);
				}
				show->showToast(text);
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
		const auto result = owned.data();
		const auto pending = result->add(
			object_ptr<Ui::VerticalLayout>(result));
		const auto list = result->add(object_ptr<Ui::VerticalLayout>(result));
		const auto show = std::shared_ptr<Ui::Show>(ctx.controller->uiShow());
		const auto ids = [] {
			auto found = std::vector<QString>();
			if (const auto manager = Plugins::Manager::Maybe()) {
				for (const auto &plugin : manager->plugins()) {
					found.push_back(plugin.id);
				}
			}
			return found;
		};
		const auto shown = std::make_shared<std::vector<QString>>(ids());
		const auto rebuild = [=] {
			list->clear();
			FillList(list, show);
			list->resizeToWidth(list->width());
		};
		rebuild();
		const auto refillPending = [=] {
			pending->clear();
			FillPending(pending);
			pending->resizeToWidth(pending->width());
		};
		refillPending();
		// Cancel fires the change from its own click: rebuild on the next
		// event loop turn, not under the pressed button.
		const auto pendingScheduled = std::make_shared<bool>(false);
		Plugins::PendingInstallsChanges(
		) | rpl::on_next([=] {
			if (!std::exchange(*pendingScheduled, true)) {
				crl::on_main(pending, [=] {
					*pendingScheduled = false;
					refillPending();
				});
			}
		}, pending->lifetime());
		if (const auto manager = Plugins::Manager::Maybe()) {
			// Rows follow their plugin in place; the list is rebuilt only
			// when plugins come or go, on the next event loop turn as a
			// click in a row may have fired the change.
			const auto scheduled = std::make_shared<bool>(false);
			manager->changes() | rpl::on_next([=] {
				if (!std::exchange(*scheduled, true)) {
					crl::on_main(list, [=] {
						*scheduled = false;
						auto now = ids();
						if (now != *shown) {
							*shown = std::move(now);
							rebuild();
						}
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
