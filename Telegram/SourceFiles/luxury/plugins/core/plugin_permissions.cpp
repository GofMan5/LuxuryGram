// This is the source code of LuxuryGram for Desktop.
// See LEGAL for license and copyright information.
#include "luxury/plugins/core/plugin_permissions.h"

#include <algorithm>
#include <array>

namespace Luxury::Plugins {
namespace {

struct PermissionInfo {
	Permission permission = Permission::MessagesRead;
	std::string_view id;
	bool dangerous = false;
};

constexpr auto kPermissions = std::array<PermissionInfo, kPermissionCount>{{
	{ Permission::MessagesRead, "messages.read", false },
	{ Permission::MessagesSend, "messages.send", true },
	{ Permission::MessagesModify, "messages.modify", true },
	{ Permission::Ui, "ui", false },
	{ Permission::Network, "network", false },
	{ Permission::Storage, "storage", false },
	{ Permission::Clipboard, "clipboard", true },
	{ Permission::TelegramApi, "telegram.api", true },
	{ Permission::TelegramUpdates, "telegram.updates", true },
	{ Permission::MessagesManage, "messages.manage", true },
}};

// Whole namespaces: login, the TON wallet, paid SMS jobs, and every
// payments.* method that is not a read (see IsMethodForbidden).
constexpr auto kForbiddenPrefixes = std::array<std::string_view, 7>{
	"auth.",
	"account.getpassword",
	"invoke",
	"initconnection",
	"payments.",
	"wallet.",
	"smsjobs.",
};

// payments.get* reads stay allowed, except these.
constexpr auto kAllowedPrefix = std::string_view("payments.get");

// Sessions, takeout, password and recovery, login email and phone,
// Telegram Passport, URL and web app logins, passkeys, push devices,
// withdrawals and anything that spends Stars or hands out a credential.
constexpr auto kForbiddenMethods = std::array<std::string_view, 60>{
	"account.deleteaccount",
	"account.resetauthorization",
	"account.resetwebauthorization",
	"account.resetwebauthorizations",
	"account.getauthorizations",
	"account.getwebauthorizations",
	"account.changeauthorizationsettings",
	"account.setauthorizationttl",
	"account.invalidatesignincodes",
	"account.inittakeoutsession",
	"account.finishtakeoutsession",
	"account.updatepasswordsettings",
	"account.gettmppassword",
	"account.resetpassword",
	"account.declinepasswordreset",
	"account.confirmpasswordemail",
	"account.resendpasswordemail",
	"account.cancelpasswordemail",
	"account.sendverifyemailcode",
	"account.verifyemail",
	"account.changephone",
	"account.sendchangephonecode",
	"account.sendconfirmphonecode",
	"account.confirmphone",
	"account.sendverifyphonecode",
	"account.verifyphone",
	"account.getallsecurevalues",
	"account.getsecurevalue",
	"account.savesecurevalue",
	"account.deletesecurevalue",
	"account.getauthorizationform",
	"account.acceptauthorization",
	"messages.requestencryption",
	"messages.requesturlauth",
	"messages.accepturlauth",
	"messages.sendpaidreaction",
	"bots.exportbottoken",
	// A push token of the plugin's own would get message previews.
	"account.registerdevice",
	"account.unregisterdevice",
	// Passkeys are a login credential.
	"account.getpasskeys",
	"account.initpasskeyregistration",
	"account.registerpasskey",
	"account.deletepasskey",
	"messages.checkurlauthmatchcode",
	"messages.declineurlauth",
	// A web app URL carries initData signed for this account: a login.
	"messages.requestwebview",
	"messages.requestsimplewebview",
	"messages.requestmainwebview",
	"messages.requestappwebview",
	"messages.requestchatjoinwebview",
	"messages.prolongwebview",
	"messages.sendwebviewdata",
	"bots.invokewebviewcustommethod",
	// Grants that let a bot read or act for the account: business bot
	// connections, attach menu write access, messages and emoji status.
	"account.updateconnectedbot",
	"account.toggleconnectedbotpaused",
	"account.disablepeerconnectedbot",
	"account.getbotbusinessconnection",
	"bots.allowsendmessage",
	"bots.toggleuseremojistatuspermission",
	"messages.togglebotinattachmenu",
};

// Reads under payments.get* that still hand out money or personal data.
constexpr auto kForbiddenReads = std::array<std::string_view, 7>{
	"payments.getsavedinfo",
	"payments.getbankcarddata",
	"payments.getstargiftwithdrawalurl",
	"payments.getstarsrevenuewithdrawalurl",
	"payments.getstarsrevenueadsaccounturl",
	// Both return the saved name, phone, email and shipping address.
	"payments.getpaymentform",
	"payments.getpaymentreceipt",
};

[[nodiscard]] char Lower(char c) {
	return (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c;
}

[[nodiscard]] std::string Lowercase(std::string_view text) {
	auto result = std::string(text);
	std::transform(result.begin(), result.end(), result.begin(), Lower);
	return result;
}

[[nodiscard]] bool EndsWith(std::string_view text, std::string_view suffix) {
	return text.size() >= suffix.size()
		&& text.substr(text.size() - suffix.size()) == suffix;
}

[[nodiscard]] bool IsBadHostChar(char c) {
	const auto byte = static_cast<unsigned char>(c);
	return byte <= 0x20
		|| byte == 0x7F
		|| c == '@'
		|| c == ':'
		|| c == '/'
		|| c == '\\';
}

} // namespace

std::string_view PermissionId(Permission permission) {
	return kPermissions[static_cast<std::size_t>(permission)].id;
}

std::optional<Permission> PermissionFromId(std::string_view id) {
	for (const auto &info : kPermissions) {
		if (info.id == id) {
			return info.permission;
		}
	}
	return std::nullopt;
}

bool IsDangerous(Permission permission) {
	return kPermissions[static_cast<std::size_t>(permission)].dangerous;
}

bool PermissionSet::anyDangerous() const {
	return std::any_of(kPermissions.begin(), kPermissions.end(), [&](
			const PermissionInfo &info) {
		return info.dangerous && has(info.permission);
	});
}

std::vector<std::string> PermissionSet::ids() const {
	auto result = std::vector<std::string>();
	for (const auto &info : kPermissions) {
		if (has(info.permission)) {
			result.emplace_back(info.id);
		}
	}
	return result;
}

bool IsMethodForbidden(std::string_view tlMethodName) {
	const auto name = Lowercase(tlMethodName);
	const auto listed = [&](const auto &list) {
		return std::find(list.begin(), list.end(), name) != list.end();
	};
	if (name.starts_with(kAllowedPrefix)) {
		return listed(kForbiddenReads);
	}
	const auto prefixed = std::any_of(
		kForbiddenPrefixes.begin(),
		kForbiddenPrefixes.end(),
		[&](std::string_view prefix) { return name.starts_with(prefix); });
	return prefixed || listed(kForbiddenMethods);
}

bool HostMatches(
		const std::vector<std::string> &allowed,
		std::string_view host) {
	if (host.ends_with('.')) {
		host.remove_suffix(1);
	}
	if (host.empty()
		|| host.starts_with('.')
		|| host.find("..") != std::string_view::npos
		|| std::any_of(host.begin(), host.end(), IsBadHostChar)) {
		return false;
	}
	const auto lower = Lowercase(host);
	for (const auto &entry : allowed) {
		const auto pattern = Lowercase(entry);
		if (pattern == "*" || pattern == lower) {
			return true;
		} else if (pattern.starts_with("*.")
			&& pattern.size() > 2
			&& EndsWith(lower, std::string_view(pattern).substr(1))) {
			return true;
		}
	}
	return false;
}

} // namespace Luxury::Plugins
