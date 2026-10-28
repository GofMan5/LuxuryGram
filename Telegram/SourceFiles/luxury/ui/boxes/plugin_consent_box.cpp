// This is the source code of LuxuryGram for Desktop.
// See LEGAL for license and copyright information.
#include "luxury/ui/boxes/plugin_consent_box.h"

#include "lang_auto.h"
#include "luxury/plugins/plugin_manager.h"
#include "ui/layers/generic_box.h"
#include "ui/layers/show.h"
#include "ui/text/text_utilities.h"
#include "ui/vertical_list.h"
#include "ui/widgets/labels.h"

#include "styles/style_layers.h"

namespace Luxury::Plugins {
namespace {

void CommitInstall(std::shared_ptr<Ui::Show> show, InstallPlan plan) {
	const auto manager = Manager::Maybe();
	if (!manager) {
		show->showToast(tr::lng_luxury_plugins_ui_unavailable(tr::now));
		return;
	}
	const auto name = QString::fromStdString(plan.manifest.name);
	const auto update = plan.update;
	manager->commitInstall(std::move(plan), [=](QString error) {
		show->showToast(!error.isEmpty()
			? tr::lng_luxury_plugins_ui_install_failed(
				tr::now,
				lt_error,
				error)
			: update
			? tr::lng_luxury_plugins_ui_updated(tr::now, lt_name, name)
			: tr::lng_luxury_plugins_ui_installed(tr::now, lt_name, name));
	});
}

// newPermissions compares permission bits only, so an update that keeps
// Network but reaches more hosts must be shown for consent as well.
[[nodiscard]] bool HostsWidened(const InstallPlan &plan) {
	if (!plan.update || !plan.manifest.permissions.has(Permission::Network)) {
		return false;
	}
	const auto manager = Manager::Maybe();
	const auto installed = manager
		? manager->plugin(QString::fromStdString(plan.manifest.id))
		: std::optional<PluginView>();
	if (!installed || !installed->granted.has(Permission::Network)) {
		return true;
	}
	const auto &old = installed->network;
	if (std::find(old.begin(), old.end(), u"*"_q) != old.end()) {
		return false;
	}
	for (const auto &host : plan.manifest.network) {
		const auto value = QString::fromStdString(host);
		if (std::find(old.begin(), old.end(), value) == old.end()) {
			return true;
		}
	}
	return false;
}

[[nodiscard]] QString SourceText(const InstallPlan &plan) {
	const auto repo = QString::fromStdString(
		plan.input.owner + '/' + plan.input.repo);
	const auto ref = QString::fromStdString(plan.resolved.ref);
	return (plan.resolved.channel == Channel::Release)
		? (repo + '@' + ref)
		: (repo
			+ '#'
			+ QString::fromStdString(plan.resolved.label)
			+ u" ("_q
			+ ref.left(7)
			+ ')');
}

void AddLabel(
		not_null<Ui::GenericBox*> box,
		TextWithEntities text,
		const style::FlatLabel &st,
		std::optional<QColor> color = std::nullopt) {
	const auto label = box->addRow(
		object_ptr<Ui::FlatLabel>(box, rpl::single(std::move(text)), st));
	label->setTextColorOverride(color);
}

void FillConsentBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Ui::Show> show,
		InstallPlan plan) {
	const auto &manifest = plan.manifest;
	const auto asked = plan.update
		? plan.newPermissions
		: manifest.permissions;
	const auto widened = HostsWidened(plan);
	const auto hostsShown = asked.has(Permission::Network) || widened;
	const auto attention = st::boxTextFgError->c;

	box->setWidth(st::boxWideWidth);
	box->setTitle(plan.update
		? tr::lng_luxury_plugins_ui_consent_update_title()
		: tr::lng_luxury_plugins_ui_consent_title());

	AddLabel(
		box,
		Ui::Text::Bold(QString::fromStdString(manifest.name)).append(
			u" "_q + QString::fromStdString(manifest.version)),
		st::boxLabel);
	if (!manifest.author.empty()) {
		AddLabel(
			box,
			{ QString::fromStdString(manifest.author) },
			st::boxDividerLabel);
	}
	AddLabel(
		box,
		{ tr::lng_luxury_plugins_ui_source(
			tr::now,
			lt_name,
			SourceText(plan)) },
		st::boxDividerLabel);
	box->addSkip(st::boxLittleSkip * 2);

	AddLabel(box, { (asked.empty() && widened)
		? tr::lng_luxury_plugins_ui_consent_new_hosts(tr::now)
		: asked.empty()
		? tr::lng_luxury_plugins_ui_consent_none(tr::now)
		: plan.update
		? tr::lng_luxury_plugins_ui_consent_new(tr::now)
		: tr::lng_luxury_plugins_ui_consent_requests(tr::now) },
		st::boxLabel);
	for (auto i = 0; i != kPermissionCount; ++i) {
		const auto permission = static_cast<Permission>(i);
		if (!asked.has(permission)) {
			continue;
		}
		const auto dangerous = IsDangerous(permission);
		box->addSkip(st::boxLittleSkip);
		AddLabel(
			box,
			Ui::Text::Bold(PermissionTitle(permission)),
			st::boxLabel,
			dangerous ? std::optional<QColor>(attention) : std::nullopt);
		AddLabel(
			box,
			{ PermissionDescription(permission) },
			st::boxDividerLabel);
	}
	if (hostsShown) {
		auto hosts = QStringList();
		for (const auto &host : manifest.network) {
			hosts.push_back((host == "*")
				? tr::lng_luxury_plugins_ui_any_website(tr::now)
				: QString::fromStdString(host));
		}
		box->addSkip(st::boxLittleSkip);
		AddLabel(
			box,
			{ tr::lng_luxury_plugins_ui_consent_hosts(tr::now) },
			st::boxLabel);
		AddLabel(box, { hosts.join(u", "_q) }, st::boxDividerLabel);
	}
	const auto dangerous = asked.anyDangerous();
	if (dangerous) {
		box->addSkip(st::boxLittleSkip * 2);
		AddLabel(
			box,
			{ tr::lng_luxury_plugins_ui_consent_danger(tr::now) },
			st::boxLabel,
			attention);
	}

	box->addButton(tr::lng_luxury_plugins_ui_consent_allow(), [=] {
		box->closeBox();
		auto allowed = plan;
		allowed.consented = true;
		allowed.shownPermissions = asked;
		if (hostsShown) {
			allowed.shownHosts = allowed.manifest.network;
		}
		CommitInstall(show, std::move(allowed));
	}, dangerous ? st::attentionBoxButton : st::defaultBoxButton);
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

} // namespace

void RequestInstall(std::shared_ptr<Ui::Show> show, InstallPlan plan) {
	if (plan.update && plan.newPermissions.empty() && !HostsWidened(plan)) {
		CommitInstall(std::move(show), std::move(plan));
	} else {
		show->showBox(Box(FillConsentBox, show, std::move(plan)));
	}
}

QString PermissionTitle(Permission permission) {
	switch (permission) {
	case Permission::MessagesRead:
		return tr::lng_luxury_plugins_ui_perm_messages_read(tr::now);
	case Permission::MessagesSend:
		return tr::lng_luxury_plugins_ui_perm_messages_send(tr::now);
	case Permission::MessagesModify:
		return tr::lng_luxury_plugins_ui_perm_messages_modify(tr::now);
	case Permission::Ui:
		return tr::lng_luxury_plugins_ui_perm_ui(tr::now);
	case Permission::Network:
		return tr::lng_luxury_plugins_ui_perm_network(tr::now);
	case Permission::Storage:
		return tr::lng_luxury_plugins_ui_perm_storage(tr::now);
	case Permission::Clipboard:
		return tr::lng_luxury_plugins_ui_perm_clipboard(tr::now);
	case Permission::TelegramApi:
		return tr::lng_luxury_plugins_ui_perm_telegram_api(tr::now);
	case Permission::TelegramUpdates:
		return tr::lng_luxury_plugins_ui_perm_telegram_updates(tr::now);
	}
	return QString();
}

QString PermissionDescription(Permission permission) {
	switch (permission) {
	case Permission::MessagesRead:
		return tr::lng_luxury_plugins_ui_perm_messages_read_about(tr::now);
	case Permission::MessagesSend:
		return tr::lng_luxury_plugins_ui_perm_messages_send_about(tr::now);
	case Permission::MessagesModify:
		return tr::lng_luxury_plugins_ui_perm_messages_modify_about(tr::now);
	case Permission::Ui:
		return tr::lng_luxury_plugins_ui_perm_ui_about(tr::now);
	case Permission::Network:
		return tr::lng_luxury_plugins_ui_perm_network_about(tr::now);
	case Permission::Storage:
		return tr::lng_luxury_plugins_ui_perm_storage_about(tr::now);
	case Permission::Clipboard:
		return tr::lng_luxury_plugins_ui_perm_clipboard_about(tr::now);
	case Permission::TelegramApi:
		return tr::lng_luxury_plugins_ui_perm_telegram_api_about(tr::now);
	case Permission::TelegramUpdates:
		return tr::lng_luxury_plugins_ui_perm_telegram_updates_about(tr::now);
	}
	return QString();
}

} // namespace Luxury::Plugins
