// This is the source code of LuxuryGram for Desktop.
// See LEGAL for license and copyright information.
#include "luxury/ui/boxes/plugin_consent_box.h"

#include "lang_auto.h"
#include "luxury/plugins/plugin_manager.h"
#include "luxury/ui/boxes/plugin_consent_logic.h"
#include "rpl/event_stream.h"
#include "ui/layers/generic_box.h"
#include "ui/layers/show.h"
#include "ui/text/text_utilities.h"
#include "ui/vertical_list.h"
#include "ui/widgets/labels.h"

#include "styles/style_layers.h"

namespace Luxury::Plugins {
namespace {

struct PendingState {
	std::vector<PendingInstall> list;
	rpl::event_stream<> changes;
};

[[nodiscard]] PendingState &Pending() {
	static auto result = PendingState();
	return result;
}

// The last entry of the id: a commit refused as Busy right away is the one
// added last. Returns whether it was cancelled.
bool RemovePending(const QString &id) {
	auto &state = Pending();
	const auto i = std::find_if(
		state.list.rbegin(),
		state.list.rend(),
		[&](const PendingInstall &entry) { return entry.id == id; });
	if (i == state.list.rend()) {
		return false;
	}
	const auto cancelled = i->cancelled;
	state.list.erase(std::next(i).base());
	state.changes.fire({});
	return cancelled;
}

void CommitInstall(std::shared_ptr<Ui::Show> show, InstallPlan plan) {
	const auto manager = Manager::Maybe();
	if (!manager) {
		show->showToast(tr::lng_luxury_plugins_ui_unavailable(tr::now));
		return;
	}
	const auto id = QString::fromStdString(plan.manifest.id);
	const auto name = QString::fromStdString(plan.manifest.name);
	const auto update = plan.update;
	Pending().list.push_back({ .id = id, .name = name });
	Pending().changes.fire({});
	manager->commitInstall(std::move(plan), [=](QString error, ErrorCode code) {
		const auto cancelled = RemovePending(id);
		show->showToast(!error.isEmpty()
			? (cancelled
				? tr::lng_luxury_plugins_ui_install_cancelled(
					tr::now,
					lt_name,
					name)
				: tr::lng_luxury_plugins_ui_install_failed(
					tr::now,
					lt_error,
					ErrorText(code)))
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

	const auto manager = Manager::Maybe();
	const auto installed = (plan.update && manager)
		? manager->plugin(QString::fromStdString(manifest.id))
		: std::optional<PluginView>();
	const auto version = QString::fromStdString(manifest.version);
	AddLabel(
		box,
		Ui::Text::Bold(QString::fromStdString(manifest.name)).append(
			u" "_q + (installed
				? (installed->version + QString::fromUtf8(" \xe2\x86\x92 "))
				: QString()) + version),
		st::boxLabel);
	if (!manifest.author.empty()) {
		AddLabel(
			box,
			{ QString::fromStdString(manifest.author) },
			st::boxDividerLabel);
	}
	if (!manifest.description.empty()) {
		AddLabel(
			box,
			{ QString::fromStdString(manifest.description) },
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
		// On an update the hosts the installed version could not reach are
		// bold; all of them are new without a Network grant.
		auto old = std::vector<std::string>();
		if (installed && installed->granted.has(Permission::Network)) {
			for (const auto &host : installed->network) {
				old.push_back(host.toStdString());
			}
		}
		const auto fresh = installed
			? NewHosts(old, manifest.network)
			: std::vector<std::string>();
		auto hosts = TextWithEntities();
		for (const auto &host : manifest.network) {
			if (!hosts.empty()) {
				hosts.append(u", "_q);
			}
			const auto text = (host == "*")
				? tr::lng_luxury_plugins_ui_any_website(tr::now)
				: QString::fromStdString(host);
			if (ranges::contains(fresh, host)) {
				hosts.append(Ui::Text::Bold(text));
			} else {
				hosts.append(text);
			}
		}
		box->addSkip(st::boxLittleSkip);
		AddLabel(
			box,
			{ tr::lng_luxury_plugins_ui_consent_hosts(tr::now) },
			st::boxLabel);
		AddLabel(box, std::move(hosts), st::boxDividerLabel);
	}
	for (const auto combined : CombinedDangers(asked, manifest.permissions)) {
		box->addSkip(st::boxLittleSkip);
		AddLabel(box, { (combined == CombinedDanger::MessagesOut)
			? tr::lng_luxury_plugins_ui_combined_messages(tr::now)
			: (combined == CombinedDanger::AccountOut)
			? tr::lng_luxury_plugins_ui_combined_account(tr::now)
			: tr::lng_luxury_plugins_ui_combined_clipboard(tr::now) },
			st::boxLabel,
			attention);
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

	box->addButton(plan.update
		? tr::lng_luxury_plugins_ui_consent_allow_update()
		: tr::lng_luxury_plugins_ui_consent_allow(), [=] {
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

const std::vector<PendingInstall> &PendingInstalls() {
	return Pending().list;
}

rpl::producer<> PendingInstallsChanges() {
	return Pending().changes.events();
}

void CancelInstall(const QString &id) {
	auto &state = Pending();
	auto changed = false;
	for (auto &entry : state.list) {
		if (entry.id == id && !entry.cancelled) {
			entry.cancelled = changed = true;
		}
	}
	if (!changed) {
		return;
	} else if (const auto manager = Manager::Maybe()) {
		manager->cancelInstall(id);
	}
	state.changes.fire({});
}

QString ErrorText(ErrorCode code) {
	switch (code) {
	case ErrorCode::None:
		return QString();
	case ErrorCode::Link:
		return tr::lng_luxury_plugins_ui_error_link(tr::now);
	case ErrorCode::Network:
		return tr::lng_luxury_plugins_ui_error_network(tr::now);
	case ErrorCode::RateLimited:
		return tr::lng_luxury_plugins_ui_error_rate_limited(tr::now);
	case ErrorCode::NotFound:
		return tr::lng_luxury_plugins_ui_error_not_found(tr::now);
	case ErrorCode::Github:
		return tr::lng_luxury_plugins_ui_error_github(tr::now);
	case ErrorCode::TooLarge:
		return tr::lng_luxury_plugins_ui_error_too_large(tr::now);
	case ErrorCode::Manifest:
		return tr::lng_luxury_plugins_ui_error_manifest(tr::now);
	case ErrorCode::Incompatible:
		return tr::lng_luxury_plugins_ui_error_incompatible(tr::now);
	case ErrorCode::Conflict:
		return tr::lng_luxury_plugins_ui_error_conflict(tr::now);
	case ErrorCode::Downgrade:
		return tr::lng_luxury_plugins_downgrade(tr::now);
	case ErrorCode::Pinned:
		return tr::lng_luxury_plugins_pinned(tr::now);
	case ErrorCode::Busy:
		return tr::lng_luxury_plugins_ui_error_busy(tr::now);
	case ErrorCode::Files:
		return tr::lng_luxury_plugins_ui_error_files(tr::now);
	case ErrorCode::UpToDate:
		return tr::lng_luxury_plugins_up_to_date(tr::now);
	case ErrorCode::Consent:
		return tr::lng_luxury_plugins_ui_error_consent(tr::now);
	}
	return QString();
}

QString LastErrorText(const QString &error) {
	// The literals plugin_host_api.cpp and plugin_manager.cpp store.
	if (error == u"The plugin used too much CPU time."_q) {
		return tr::lng_luxury_plugins_ui_error_cpu(tr::now);
	} else if (error == u"code/plugin.json is missing or broken."_q) {
		return tr::lng_luxury_plugins_ui_error_missing_manifest(tr::now);
	}
	return error;
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
