"""Plugin core model: manifest, permissions, GitHub source, registry, version.

The Qt-free half of docs/plugins/ARCHITECTURE.md. Compiles the real
luxury/plugins/core/*.cpp with -Wall -Wextra -Werror against nlohmann json
and checks every manifest rule, every GitHub link form (good and hostile:
traversal, backslash, drive letters, bad owner/repo), host matching, the
always-refused TL methods, version order and a registry round-trip.
"""
from library_regressions import ROOT, cpp, source

CORE = 'Telegram/SourceFiles/luxury/plugins/core/'
UNITS = ('plugin_version', 'plugin_permissions', 'plugin_manifest', 'plugin_source', 'plugin_registry')

MAIN = r'''
#include "luxury/plugins/core/plugin_manifest.h"
#include "luxury/plugins/core/plugin_permissions.h"
#include "luxury/plugins/core/plugin_registry.h"
#include "luxury/plugins/core/plugin_source.h"
#include "luxury/plugins/core/plugin_version.h"

#include <cstdlib>
#include <iostream>
#include <string>

using namespace Luxury::Plugins;

#define EXPECT(condition) do { if (!(condition)) { \
	std::cerr << "line " << __LINE__ << ": " #condition "\n"; std::abort(); } } while (0)

namespace {

std::string Manifest(const std::string &extra, const std::string &permissions = "[]") {
	return R"({"id":"dev.alice.autoreply","name":"Auto reply","version":"1.2.0","api":1,"main":"main.js","permissions":)"
		+ permissions + extra + "}";
}

void Refused(const std::string &json, const std::string &needle) {
	const auto result = ParseManifest(json);
	if (result || result.error().find(needle) == std::string::npos) {
		std::cerr << "expected error with '" << needle << "' for " << json << "\ngot: "
			<< (result ? std::string("ok") : result.error()) << "\n";
		std::abort();
	}
}

const ::Luxury::Plugins::Manifest Accepted(const std::string &json) {
	const auto result = ParseManifest(json);
	if (!result) {
		std::cerr << "unexpected error for " << json << ": " << result.error() << "\n";
		std::abort();
	}
	return result.value();
}

GithubInput Link(std::string_view text) {
	const auto result = ParseGithubInput(text);
	if (!result) {
		std::cerr << "link refused: " << text << ": " << result.error() << "\n";
		std::abort();
	}
	return result.value();
}

void BadLink(std::string_view text) {
	if (ParseGithubInput(text)) {
		std::cerr << "link accepted: " << text << "\n";
		std::abort();
	}
}

int Compare(const char *a, const char *b) {
	const auto x = ParseVersion(a);
	const auto y = ParseVersion(b);
	EXPECT(x && y);
	const auto c = *x <=> *y;
	return (c < 0) ? -1 : (c > 0) ? 1 : 0;
}

void Versions() {
	EXPECT(kPluginApiVersion == 1);
	EXPECT(Compare("1.2.0", "1.2.0") == 0);
	EXPECT(Compare("1.2", "1.2.0") == 0);
	EXPECT(Compare("1.0.22", "1.0.21") == 1);
	EXPECT(Compare("1.0.9", "1.0.10") == -1);
	EXPECT(Compare("2.0.0", "1.99.99") == 1);
	EXPECT(Compare("1.0.0-beta", "1.0.0") == -1);
	EXPECT(Compare("1.0.0", "1.0.0-rc.1") == 1);
	EXPECT(Compare("1.0.0-alpha", "1.0.0-alpha.1") == -1);
	EXPECT(Compare("1.0.0-alpha.2", "1.0.0-alpha.10") == -1);
	EXPECT(Compare("1.0.0-2", "1.0.0-alpha") == -1);
	EXPECT(Compare("1.0.0+build.5", "1.0.0") == 0);
	EXPECT(Compare("1.0.0-rc.1", "0.9.9") == 1);
	for (const auto bad : { "", "1.", ".1", "1..2", "1.2.3.4", "v1.2", "01.2", "1.2.x",
		"1.0.0-", "1.0.0-a..b", "1.0.0+", "-1.0", "1.0.0-01", "1234567890.0" }) {
		if (ParseVersion(bad)) {
			std::cerr << "version accepted: " << bad << "\n";
			std::abort();
		}
	}
}

void Permissions() {
	const char *ids[] = { "messages.read", "messages.send", "messages.modify", "ui", "network",
		"storage", "clipboard", "telegram.api", "telegram.updates" };
	auto all = PermissionSet();
	for (const auto id : ids) {
		const auto permission = PermissionFromId(id);
		EXPECT(permission && PermissionId(*permission) == id);
		all.add(*permission);
	}
	EXPECT(all.ids().size() == 9);
	EXPECT(!PermissionFromId("Messages.read") && !PermissionFromId("") && !PermissionFromId("admin"));
	EXPECT(IsDangerous(Permission::MessagesSend) && IsDangerous(Permission::MessagesModify));
	EXPECT(IsDangerous(Permission::TelegramApi) && IsDangerous(Permission::TelegramUpdates));
	// Copied passwords and login codes pass through the clipboard.
	EXPECT(IsDangerous(Permission::Clipboard));
	EXPECT(!IsDangerous(Permission::MessagesRead) && !IsDangerous(Permission::Ui)
		&& !IsDangerous(Permission::Network) && !IsDangerous(Permission::Storage));

	auto set = PermissionSet();
	EXPECT(set.empty() && !set.anyDangerous());
	set.add(Permission::Ui);
	set.add(Permission::Storage);
	EXPECT(set.has(Permission::Ui) && !set.has(Permission::Network) && !set.anyDangerous());
	EXPECT((set.ids() == std::vector<std::string>{ "ui", "storage" }));
	set.add(Permission::TelegramApi);
	EXPECT(set.anyDangerous());
	set.remove(Permission::TelegramApi);
	EXPECT(!set.anyDangerous() && PermissionSet(set.bits()) == set);
	EXPECT(PermissionSet(0xFFFFFFFFU) == all);

	auto granted = PermissionSet();
	granted.add(Permission::Ui);
	granted.add(Permission::Storage);
	auto requested = granted;
	EXPECT(!NeedsNewConsent(granted, requested));
	requested.remove(Permission::Storage);
	EXPECT(!NeedsNewConsent(granted, requested));
	requested.add(Permission::Network);
	EXPECT(NeedsNewConsent(granted, requested));
	EXPECT(!NeedsNewConsent(granted, PermissionSet()));
}

void Methods() {
	for (const auto name : { "auth.sendCode", "auth.exportAuthorization", "auth.importBotAuthorization",
		"AUTH.signIn", "account.deleteAccount", "account.resetAuthorization",
		"account.resetWebAuthorization", "account.resetWebAuthorizations", "account.getPassword",
		"account.getPasswordSettings", "account.updatePasswordSettings", "account.getTmpPassword",
		"account.confirmPasswordEmail", "account.changePhone", "account.sendChangePhoneCode",
		"payments.sendPaymentForm", "payments.sendStarsForm", "payments.sendStarGift",
		"messages.requestEncryption", "invokeWithLayer", "invokeWithoutUpdates",
		"invokeWithTakeout", "invokeAfterMsg", "invokeAfterMsgs", "initConnection",
		// Sessions, takeout, password, Passport, URL logins.
		"account.getAuthorizations", "account.getWebAuthorizations", "account.initTakeoutSession",
		"account.resetPassword", "account.declinePasswordReset", "account.getAllSecureValues",
		"account.getSecureValue", "account.saveSecureValue", "account.deleteSecureValue",
		"account.getAuthorizationForm", "account.acceptAuthorization", "messages.acceptUrlAuth",
		"messages.requestUrlAuth", "account.verifyEmail", "account.setAuthorizationTTL",
		"bots.exportBotToken", "messages.sendPaidReaction",
		// payments.* except reads, and the reads that hand out money.
		"payments.transferStarGift", "payments.convertStarGift", "payments.upgradeStarGift",
		"payments.updateStarGiftPrice", "payments.refundStarsCharge", "payments.applyGiftCode",
		"PAYMENTS.saveStarGift", "payments.getSavedInfo", "payments.getBankCardData",
		"payments.getStarsRevenueWithdrawalUrl", "payments.getStarGiftWithdrawalUrl",
		"wallet.sendTransfer", "smsjobs.join",
		// Payment details, push devices, passkeys, web app logins.
		"payments.getPaymentForm", "payments.getPaymentReceipt", "account.registerDevice",
		"account.unregisterDevice", "account.getPasskeys", "account.registerPasskey",
		"account.initPasskeyRegistration", "account.deletePasskey", "messages.declineUrlAuth",
		"messages.checkUrlAuthMatchCode", "messages.requestWebView", "messages.requestSimpleWebView",
		"messages.requestMainWebView", "messages.requestAppWebView", "messages.requestChatJoinWebView",
		"messages.prolongWebView", "messages.sendWebViewData", "bots.invokeWebViewCustomMethod",
		// Bot grants: business connections, attach menu, messages, emoji status.
		"account.updateConnectedBot", "account.toggleConnectedBotPaused",
		"account.disablePeerConnectedBot", "account.getBotBusinessConnection",
		"bots.allowSendMessage", "bots.toggleUserEmojiStatusPermission",
		"messages.toggleBotInAttachMenu" }) {
		if (!IsMethodForbidden(name)) {
			std::cerr << "method allowed: " << name << "\n";
			std::abort();
		}
	}
	for (const auto name : { "messages.sendMessage", "messages.getHistory", "account.getContentSettings",
		"payments.getSavedStarGifts", "Payments.GetStarsTransactions",
		"account.updateProfile", "payments.getStarGifts", "payments.getStarsStatus",
		"users.getFullUser", "messages.acceptEncryption", "help.getConfig", "authx.foo", "" }) {
		if (IsMethodForbidden(name)) {
			std::cerr << "method refused: " << name << "\n";
			std::abort();
		}
	}
}

void Hosts() {
	const auto list = std::vector<std::string>{ "api.example.com", "*.example.org", "Mixed.Case.net" };
	EXPECT(HostMatches(list, "api.example.com"));
	EXPECT(HostMatches(list, "API.Example.COM"));
	EXPECT(HostMatches(list, "api.example.com."));
	EXPECT(HostMatches(list, "mixed.case.net"));
	EXPECT(!HostMatches(list, "example.com"));
	EXPECT(!HostMatches(list, "x.api.example.com"));
	EXPECT(!HostMatches(list, "evilapi.example.com"));
	EXPECT(HostMatches(list, "a.example.org"));
	EXPECT(HostMatches(list, "a.b.EXAMPLE.org"));
	EXPECT(!HostMatches(list, "example.org"));
	EXPECT(!HostMatches(list, "badexample.org"));
	EXPECT(!HostMatches(list, "example.org.evil.com"));
	EXPECT(!HostMatches(list, "user@api.example.com"));
	EXPECT(!HostMatches(list, "api.example.com:443"));
	EXPECT(!HostMatches(list, "a.example.org/x"));
	EXPECT(!HostMatches(list, "a.example.org\\x"));
	EXPECT(!HostMatches(list, " api.example.com"));
	EXPECT(!HostMatches(list, ".example.org"));
	EXPECT(!HostMatches(list, "a..example.org"));
	EXPECT(!HostMatches(list, ""));
	EXPECT(!HostMatches({}, "api.example.com"));
	EXPECT(HostMatches({ "example.org", "*.example.org" }, "example.org"));
	EXPECT(HostMatches({ "*" }, "anything.at.all"));
	EXPECT(!HostMatches({ "*" }, "a@b"));
	EXPECT(!HostMatches({ "*." }, "a."));
}

void Paths() {
	for (const auto good : { "main.js", "lib/text.js", "a/b/c.d.js", ".hidden", "x..y.js", "con_tent.js" }) {
		EXPECT(IsSafeRelativePath(good));
	}
	for (const auto bad : { "", "/main.js", "../main.js", "lib/../../x", "..", ".", "./main.js",
		"lib//x.js", "lib/", "lib\\x.js", "C:main.js", "C:/x.js", "c:\\x.js", "\\\\server\\x",
		"main.js:stream", "a/ b/x.", "lib/x.", "lib/x ", "con", "CON.js", "lib/nul.txt",
		"com1", "lpt9.js", "a*b", "a?b", "a<b", "a|b", "a\"b", "tab\tname" }) {
		if (IsSafeRelativePath(bad)) {
			std::cerr << "path accepted: " << bad << "\n";
			std::abort();
		}
	}
	EXPECT(!IsSafeRelativePath(std::string_view("a\0b", 3)));
}

void Manifests() {
	const auto full = Accepted(R"({
		"id": "dev.alice.autoreply", "name": "Auto reply", "version": "1.2.0",
		"description": "Answers while you are away", "author": "alice", "api": 1,
		"runtime": "js", "main": "main.js", "files": ["main.js", "lib/text.js"],
		"permissions": ["messages.read", "messages.send", "ui", "storage", "network"],
		"network": ["API.example.com", "*.example.org"],
		"settings": [
			{"key": "text", "type": "string", "title": "Reply text", "default": "Away"},
			{"key": "onlyPrivate", "type": "bool", "title": "Private chats only", "default": true},
			{"key": "delay", "type": "int", "title": "Delay, s", "default": 5, "min": 0, "max": 600},
			{"key": "mode", "type": "choice", "title": "Mode", "default": "contacts", "options": ["all", "contacts"]}
		],
		"minClient": "1.0.22", "homepage": "https://github.com/alice/autoreply",
		"futureKey": {"nested": [1, 2]}
	})");
	EXPECT(full.id == "dev.alice.autoreply" && full.name == "Auto reply" && full.version == "1.2.0");
	EXPECT(full.description == "Answers while you are away" && full.author == "alice");
	EXPECT(full.api == 1 && full.runtime == Runtime::Js && full.main == "main.js");
	EXPECT((full.files == std::vector<std::string>{ "main.js", "lib/text.js" }));
	EXPECT(full.permissions.has(Permission::MessagesSend) && full.permissions.has(Permission::Network));
	EXPECT(!full.permissions.has(Permission::TelegramApi));
	EXPECT((full.network == std::vector<std::string>{ "api.example.com", "*.example.org" }));
	EXPECT(full.minClient == "1.0.22" && full.homepage == "https://github.com/alice/autoreply");
	EXPECT(full.settings.size() == 4);
	EXPECT(full.settings[0].type == SettingType::String && full.settings[0].defaultString == "Away");
	EXPECT(full.settings[1].type == SettingType::Bool && full.settings[1].defaultBool);
	EXPECT(full.settings[2].type == SettingType::Int && full.settings[2].defaultInt == 5);
	EXPECT(full.settings[2].min == 0 && full.settings[2].max == 600);
	EXPECT(full.settings[3].type == SettingType::Choice && full.settings[3].defaultString == "contacts");
	EXPECT(full.settings[3].options.size() == 2 && full.settings[3].title == "Mode");

	const auto minimal = Accepted(Manifest(""));
	EXPECT((minimal.files == std::vector<std::string>{ "main.js" }));
	EXPECT(minimal.permissions.empty() && minimal.network.empty() && minimal.settings.empty());
	EXPECT(Accepted(Manifest(R"(,"settings":[{"key":"m","type":"choice","title":"M","options":["a","b"]}])")).settings[0].defaultString == "a");
	EXPECT(Accepted(Manifest(R"(,"settings":[{"key":"n","type":"int","title":"N","min":3}])")).settings[0].defaultInt == 3);
	EXPECT(!Accepted(Manifest(R"(,"settings":[{"key":"b","type":"bool","title":"B"}])")).settings[0].defaultBool);
	EXPECT(Accepted(Manifest(R"(,"network":["*"])", R"(["network"])")).network[0] == "*");
	Accepted(Manifest(R"(,"runtime":"js")"));
	Accepted(R"({"id":"abc","name":"n","version":"0.1","api":1,"main":"m.js"})");

	// Document.
	Refused("", "plugin.json");
	Refused("{", "not valid JSON");
	Refused("[]", "must be an object");
	Refused(std::string(300 * 1024, ' '), "256 KiB");

	// id.
	Refused(R"({"name":"n","version":"1","api":1,"main":"m.js"})", "id: is required");
	Refused(R"({"id":5,"name":"n","version":"1","api":1,"main":"m.js"})", "id: must be a string");
	for (const auto id : { "ab", "Dev.alice", ".abc", "-abc", "_abc", "a b c", "abc/def",
		// Windows drops a trailing dot (victim.id. opens victim.id) and
		// device names open the device.
		"victim.id.", "abc-", "abc_", "a..b", "nul", "con", "con.x", "aux.js", "com1", "lpt9.x",
		"ab\u00e9c",
		"a2345678901234567890123456789012345678901234567890123456789012345" }) {
		Refused(R"({"id":")" + std::string(id) + R"(","name":"n","version":"1","api":1,"main":"m.js"})", "id: must match");
	}
	EXPECT(IsValidPluginId("a234567890123456789012345678901234567890123456789012345678901234"));
	EXPECT(IsValidPluginId("0a-b_c.d") && IsValidPluginId("abc"));

	// api.
	Refused(R"({"id":"abc","name":"n","version":"1","main":"m.js"})", "api: is required");
	Refused(R"({"id":"abc","name":"n","version":"1","api":"1","main":"m.js"})", "api: must be an integer");
	Refused(R"({"id":"abc","name":"n","version":"1","api":1.5,"main":"m.js"})", "api: must be an integer");
	Refused(R"({"id":"abc","name":"n","version":"1","api":0,"main":"m.js"})", "api: must be at least 1");
	Refused(R"({"id":"abc","name":"n","version":"1","api":-3,"main":"m.js"})", "api: must be at least 1");
	Refused(R"({"id":"abc","name":"n","version":"1","api":2,"main":"m.js"})", "api: plugin needs API 2, this LuxuryGram supports up to 1; update LuxuryGram");
	Refused(R"({"id":"abc","name":"n","version":"1","api":18446744073709551615,"main":"m.js"})", "update LuxuryGram");
	// A newer API is reported even when other fields are unknown to this client.
	Refused(R"({"id":"abc","api":2,"main":7})", "update LuxuryGram");

	// runtime.
	Refused(Manifest(R"(,"runtime":"python")"), "runtime: \"python\" needs a newer LuxuryGram");
	Refused(Manifest(R"(,"runtime":"python-native")"), "runtime: \"python-native\" needs a newer LuxuryGram");
	Refused(Manifest(R"(,"runtime":"lua")"), "runtime: unknown runtime");
	Refused(Manifest(R"(,"runtime":1)"), "runtime: must be a string");

	// Texts and versions.
	Refused(R"({"id":"abc","version":"1","api":1,"main":"m.js"})", "name: is required");
	Refused(R"({"id":"abc","name":"","version":"1","api":1,"main":"m.js"})", "name: must not be empty");
	Refused(R"({"id":"abc","name":"n","api":1,"main":"m.js"})", "version: is required");
	Refused(R"({"id":"abc","name":"n","version":"one","api":1,"main":"m.js"})", "version: must look like");
	Refused(R"({"id":"abc","name":"n","version":1,"api":1,"main":"m.js"})", "version: must be a string");
	Refused(Manifest(R"(,"minClient":"latest")"), "minClient: must look like");
	Refused(Manifest(R"(,"minClient":22)"), "minClient: must be a string");
	Refused(Manifest(R"(,"description":["x"])"), "description: must be a string");
	Refused(Manifest(R"(,"author":null)"), "author: must be a string");
	Refused(Manifest(R"x(,"homepage":"javascript:alert(1)")x"),"homepage: must be an http(s) link");
	Refused(Manifest(R"(,"homepage":false)"), "homepage: must be a string");

	// main and files.
	Refused(R"({"id":"abc","name":"n","version":"1","api":1})", "main: is required");
	Refused(R"({"id":"abc","name":"n","version":"1","api":1,"main":"../m.js"})", "main: must be a safe relative path");
	Refused(R"({"id":"abc","name":"n","version":"1","api":1,"main":"C:\\m.js"})", "main: must be a safe relative path");
	Refused(Manifest(R"(,"files":"main.js")"), "files: must be an array of strings");
	Refused(Manifest(R"(,"files":["main.js",3])"), "files[1]: must be a string");
	Refused(Manifest(R"(,"files":["main.js","../secret"])"), "files[1]: must be a safe relative path");
	Refused(Manifest(R"(,"files":["main.js","lib\\x.js"])"), "files[1]: must be a safe relative path");
	Refused(Manifest(R"(,"files":["main.js","C:/x.js"])"), "files[1]: must be a safe relative path");
	Refused(Manifest(R"(,"files":["main.js","/etc/passwd"])"), "files[1]: must be a safe relative path");
	Refused(Manifest(R"(,"files":["main.js","a\u0000b"])"), "files[1]: must be a safe relative path");
	Refused(Manifest(R"(,"files":["main.js","main.js"])"), "files[1]: duplicate");
	Refused(Manifest(R"(,"files":["lib.js"])"), "files: must include main");
	Refused(Manifest(R"(,"files":[])"), "files: must include main");
	auto files = std::string(R"(,"files":["main.js")");
	for (auto i = 1; i != 200; ++i) {
		files += ",\"f" + std::to_string(i) + ".js\"";
	}
	EXPECT(Accepted(Manifest(files + "]")).files.size() == 200);
	Refused(Manifest(files + R"(,"f200.js"])"), "files: more than 200 files");

	// permissions and network.
	Refused(Manifest("", R"("ui")"), "permissions: must be an array of strings");
	Refused(Manifest("", R"(["ui",1])"), "permissions[1]: must be a string");
	Refused(Manifest("", R"(["ui","root"])"), "permissions[1]: unknown permission \"root\"");
	Refused(Manifest("", R"(["network"])"), "network: is required with the network permission");
	Refused(Manifest(R"(,"network":[])", R"(["network"])"), "network: is required with the network permission");
	Refused(Manifest(R"(,"network":"a.com")", R"(["network"])"), "network: must be an array of strings");
	Refused(Manifest(R"(,"network":["a.com",5])", R"(["network"])"), "network[1]: must be a string");
	for (const auto host : { "", "*.", "**.a.com", "a.*.com", "user@a.com", "a..com",
		".a.com", "a.com.", "https://a.com", "a.com/x", "a com", "*a.com",
		"a.com:", "a.com:0", "a.com:080", "a.com:65536", "a.com:44x", "a.com:1:2", "*:443", ":443" }) {
		Refused(Manifest(R"(,"network":[")" + std::string(host) + R"("])", R"(["network"])"), "network[0]: must be a host");
	}
	// An explicit port lets http.fetch reach it; the bare host means 443.
	EXPECT((Accepted(Manifest(R"(,"network":["A.com:8443","*.b.org:1","c.net:65535"])", R"(["network"])")).network
		== std::vector<std::string>{ "a.com:8443", "*.b.org:1", "c.net:65535" }));
	// The host list is kept but grants nothing without the permission.
	EXPECT(!Accepted(Manifest(R"(,"network":["a.com"])")).permissions.has(Permission::Network));

	// settings.
	Refused(Manifest(R"(,"settings":{})"), "settings: must be an array");
	Refused(Manifest(R"(,"settings":[5])"), "settings[0]: must be an object");
	Refused(Manifest(R"(,"settings":[{"type":"bool","title":"T"}])"), "settings[0].key: is required");
	Refused(Manifest(R"(,"settings":[{"key":"a b","type":"bool","title":"T"}])"), "settings[0].key: must be");
	Refused(Manifest(R"(,"settings":[{"key":"k","type":"bool"}])"), "settings[0].title: is required");
	Refused(Manifest(R"(,"settings":[{"key":"k","title":"T"}])"), "settings[0].type: is required");
	Refused(Manifest(R"(,"settings":[{"key":"k","type":"color","title":"T"}])"), "settings[0].type: unknown type \"color\"");
	Refused(Manifest(R"(,"settings":[{"key":"k","type":"string","title":"T","default":1}])"), "settings[0].default: must be a string");
	Refused(Manifest(R"(,"settings":[{"key":"k","type":"bool","title":"T","default":"yes"}])"), "settings[0].default: must be a boolean");
	Refused(Manifest(R"(,"settings":[{"key":"k","type":"int","title":"T","default":"5"}])"), "settings[0].default: must be an integer");
	Refused(Manifest(R"(,"settings":[{"key":"k","type":"int","title":"T","default":1.5}])"), "settings[0].default: must be an integer");
	Refused(Manifest(R"(,"settings":[{"key":"k","type":"int","title":"T","min":"0"}])"), "settings[0].min: must be an integer");
	Refused(Manifest(R"(,"settings":[{"key":"k","type":"int","title":"T","max":true}])"), "settings[0].max: must be an integer");
	Refused(Manifest(R"(,"settings":[{"key":"k","type":"int","title":"T","min":5,"max":1}])"), "settings[0].min: must not exceed max");
	Refused(Manifest(R"(,"settings":[{"key":"k","type":"int","title":"T","default":700,"max":600}])"), "settings[0].default: must be within min and max");
	Refused(Manifest(R"(,"settings":[{"key":"k","type":"int","title":"T","default":-1,"min":0}])"), "settings[0].default: must be within min and max");
	Refused(Manifest(R"(,"settings":[{"key":"k","type":"int","title":"T","default":18446744073709551615}])"), "settings[0].default: is out of range");
	Refused(Manifest(R"(,"settings":[{"key":"k","type":"choice","title":"T"}])"), "settings[0].options: is required");
	Refused(Manifest(R"(,"settings":[{"key":"k","type":"choice","title":"T","options":[]}])"), "settings[0].options: must not be empty");
	Refused(Manifest(R"(,"settings":[{"key":"k","type":"choice","title":"T","options":"a"}])"), "settings[0].options: must be an array");
	Refused(Manifest(R"(,"settings":[{"key":"k","type":"choice","title":"T","options":["a",1]}])"), "settings[0].options[1]: must be a string");
	Refused(Manifest(R"(,"settings":[{"key":"k","type":"choice","title":"T","options":["a","a"]}])"), "settings[0].options: duplicate");
	Refused(Manifest(R"(,"settings":[{"key":"k","type":"choice","title":"T","default":"c","options":["a","b"]}])"), "settings[0].default: must be one of options");
	Refused(Manifest(R"(,"settings":[{"key":"k","type":"choice","title":"T","default":1,"options":["a"]}])"), "settings[0].default: must be a string");
	Refused(Manifest(R"(,"settings":[{"key":"k","type":"bool","title":"T"},{"key":"k","type":"int","title":"U"}])"), "settings[1].key: duplicate");
}

void Github() {
	for (const auto text : { "https://github.com/alice/autoreply", "http://github.com/alice/autoreply",
		"github.com/alice/autoreply", "alice/autoreply", "https://www.github.com/alice/autoreply",
		"HTTPS://GitHub.com/alice/autoreply", "https://github.com/alice/autoreply/",
		"https://github.com/alice/autoreply.git", "alice/autoreply.git/", "  alice/autoreply \n",
		"https://github.com/alice/autoreply?tab=readme#install", "alice/autoreply#x" }) {
		const auto link = Link(text);
		EXPECT(link.owner == "alice" && link.repo == "autoreply");
		EXPECT(link.kind == GithubKind::Auto && link.ref.empty() && link.subdir.empty());
	}
	const auto dotted = Link("my-org_1/Repo.name-2");
	EXPECT(dotted.owner == "my-org_1" && dotted.repo == "Repo.name-2");

	const auto tree = Link("https://github.com/alice/plugins/tree/main");
	EXPECT(tree.kind == GithubKind::Ref && tree.ref == "main" && tree.subdir.empty());
	const auto sub = Link("https://github.com/alice/plugins/tree/v1.2.0/packages/autoreply/?x=1");
	EXPECT(sub.kind == GithubKind::Ref && sub.ref == "v1.2.0" && sub.subdir == "packages/autoreply");
	const auto slashed = Link("github.com/alice/plugins/tree/feature%2Fx/src");
	EXPECT(slashed.ref == "feature/x" && slashed.subdir == "src");
	const auto spaced = Link("github.com/alice/plugins/tree/main/my%20plugin");
	EXPECT(spaced.subdir == "my plugin");
	const auto tag = Link("https://github.com/alice/autoreply/releases/tag/v1.2.0");
	EXPECT(tag.kind == GithubKind::ReleaseTag && tag.ref == "v1.2.0" && tag.subdir.empty());
	EXPECT(Link("alice/autoreply/releases/tag/release/2024").ref == "release/2024");
	EXPECT(Link("alice/autoreply/releases/tag/v1.0+build.git/").ref == "v1.0+build.git");
	const auto blob = Link("https://github.com/alice/plugins/blob/main/packages/auto/main.js");
	EXPECT(blob.kind == GithubKind::Ref && blob.ref == "main" && blob.subdir == "packages/auto");
	const auto rootBlob = Link("alice/plugins/blob/v1/plugin.json");
	EXPECT(rootBlob.kind == GithubKind::Ref && rootBlob.ref == "v1" && rootBlob.subdir.empty());
	const auto commitLink = Link("https://github.com/alice/plugins/commit/0123abc");
	EXPECT(commitLink.kind == GithubKind::Ref && commitLink.ref == "0123abc");
	EXPECT(Link("alice/plugins/commit/" + std::string(40, 'f')).ref == std::string(40, 'f'));
	const auto latest = Link("https://github.com/alice/plugins/releases/latest");
	EXPECT(latest.kind == GithubKind::Auto && latest.ref.empty());
	const auto ssh = Link("git@github.com:Alice/Plugins.git");
	EXPECT(ssh.owner == "Alice" && ssh.repo == "Plugins" && ssh.kind == GithubKind::Auto);
	EXPECT(IsCommitSha(std::string(40, 'a')) && !IsCommitSha(std::string(40, 'A')) && !IsCommitSha("abc"));

	for (const auto text : { "", "alice", "alice/", "/alice/autoreply", "https://gitlab.com/alice/autoreply",
		"https://github.com.evil.com/alice/autoreply", "https://evil.com/github.com/alice/autoreply",
		"https://user@github.com/alice/autoreply", "github.com", "https://github.com/",
		"ftp://github.com/alice/autoreply", "file:///alice/autoreply", "alice/auto reply",
		"al ice/autoreply", "alice/auto%20reply", "../autoreply", "alice/..", "alice/.",
		"./autoreply", "alice/.git", "a\\b/c", "alice/auto;rm", "ali$ce/x", "alice/autoreply/blob/main",
		"alice/autoreply/tree", "alice/autoreply/tree/", "alice/autoreply/releases",
		"alice/autoreply/releases/tag", "alice/autoreply/releases/tag/", "alice/autoreply/releases/latest/x",
		"alice/autoreply/commit/abc",
		"alice/autoreply/commit/zzzzzzz", "https://git@github.com:alice/autoreply",
		"alice/autoreply/issues/1",
		"alice/autoreply/tree/main/../secret", "alice/autoreply/tree/main/a/../../b",
		"alice/autoreply/tree/main/%2e%2e/x", "alice/autoreply/tree/main/a%2F..%2Fb",
		"alice/autoreply/tree/main/a%5Cb", "alice/autoreply/tree/main/C:", "alice/autoreply/tree/main/C%3A/x",
		"alice/autoreply/tree/main/a//b", "alice/autoreply/tree/main/./a", "alice/autoreply/tree/main/x%00",
		"alice/autoreply/tree/main/%zz", "alice/autoreply/tree/main/%4", "alice/autoreply/tree/..",
		"alice/autoreply/tree/-rf", "alice/autoreply/tree/a..b", "alice/autoreply/tree/x.lock",
		"alice/autoreply/tree/%2Fx", "alice/autoreply/tree/a%40%7Bb", "alice/autoreply/tree/a%20b",
		"alice/autoreply/releases/tag/..", "alice/autoreply/releases/tag/a%5Cb" }) {
		BadLink(text);
	}
	BadLink(std::string(101, 'a') + "/repo");
	BadLink("alice/autoreply/commit/" + std::string(41, 'a'));

	EXPECT(LatestReleaseUrl("alice", "autoreply") == "https://api.github.com/repos/alice/autoreply/releases/latest");
	EXPECT(RepoUrl("alice", "autoreply") == "https://api.github.com/repos/alice/autoreply");
	EXPECT(CommitUrl("alice", "autoreply", "feature/x") == "https://api.github.com/repos/alice/autoreply/commits/feature%2Fx");
	EXPECT(ReleaseByTagUrl("alice", "autoreply", "v1.0+b") == "https://api.github.com/repos/alice/autoreply/releases/tags/v1.0%2Bb");
	EXPECT(RawFileUrl("alice", "autoreply", "v1.2.0", "", "main.js") == "https://raw.githubusercontent.com/alice/autoreply/v1.2.0/main.js");
	EXPECT(RawFileUrl("alice", "r", "feature/x", "my plugin/src", "lib/a b#%.js")
		== "https://raw.githubusercontent.com/alice/r/feature/x/my%20plugin/src/lib/a%20b%23%25.js");
	EXPECT(RawFileUrl("alice", "r", "main", "", "a?.js").empty());
	EXPECT(RawFileUrl("alice", "r", "main", "", "\xd1\x8f.js") == "https://raw.githubusercontent.com/alice/r/main/%D1%8F.js");
	EXPECT(RawFileUrl("alice", "r", "main", "", "../x.js").empty());
	EXPECT(RawFileUrl("alice", "r", "main", "../up", "x.js").empty());
	EXPECT(RawFileUrl("alice", "r", "main", "a\\b", "x.js").empty());
	EXPECT(RawFileUrl("alice", "r", "../main", "", "x.js").empty());
	EXPECT(RawFileUrl("al/ice", "r", "main", "", "x.js").empty());
	EXPECT(RawFileUrl("alice", "r", "main", "", "C:/x.js").empty());

	const auto release = ParseReleaseTag(R"({"tag_name":"v1.2.0","name":"x","assets":[]})");
	EXPECT(release && release.value() == "v1.2.0");
	const auto limited = ParseReleaseTag(R"({"message":"API rate limit exceeded for 1.2.3.4."})");
	EXPECT(!limited && limited.error() == "GitHub: API rate limit exceeded for 1.2.3.4.");
	EXPECT(!ParseReleaseTag("<html>") && !ParseReleaseTag("[]") && !ParseReleaseTag(R"({"tag_name":5})"));
	EXPECT(!ParseReleaseTag(R"({"tag_name":"../x"})"));
	const auto branch = ParseDefaultBranch(R"({"id":1,"default_branch":"main"})");
	EXPECT(branch && branch.value() == "main");
	EXPECT(!ParseDefaultBranch(R"({"default_branch":""})") && !ParseDefaultBranch("{}"));
	const auto sha = ParseCommitSha(R"({"sha":"0123456789ABCDEFabcdef0123456789abcdef01","commit":{}})");
	EXPECT(sha && sha.value() == "0123456789abcdefabcdef0123456789abcdef01");
	EXPECT(!ParseCommitSha(R"({"sha":"0123456789abcdef0123456789abcdef0123456"})"));
	EXPECT(!ParseCommitSha(R"({"sha":"0123456789abcdef0123456789abcdef012345678"})"));
	EXPECT(!ParseCommitSha(R"({"sha":"g123456789abcdef0123456789abcdef01234567"})"));
	EXPECT(!ParseCommitSha(R"({"sha":"../../../../../../../../../../../../../.."})"));

	const auto stored = ResolvedRef{ Channel::Release, "v1.0.0", "" };
	EXPECT(DecideUpdate(stored, { Channel::Release, "v1.0.0", "" }) == UpdateDecision::UpToDate);
	EXPECT(DecideUpdate(stored, { Channel::Release, "v1.1.0", "" }) == UpdateDecision::Available);
	EXPECT(DecideUpdate(stored, { Channel::Release, "", "" }) == UpdateDecision::UpToDate);
	EXPECT(DecideUpdate(stored, { Channel::Branch, "v1.0.0", "main" }) == UpdateDecision::Available);
	const auto commit = ResolvedRef{ Channel::Branch, std::string(40, 'a'), "main" };
	EXPECT(DecideUpdate(commit, { Channel::Branch, std::string(40, 'a'), "main" }) == UpdateDecision::UpToDate);
	EXPECT(DecideUpdate(commit, { Channel::Branch, std::string(40, 'b'), "main" }) == UpdateDecision::Available);
	EXPECT(DecideUpdate(stored, { Channel::Release, "v0.9.0", "" }) == UpdateDecision::Downgrade);
	EXPECT(DecideUpdate(stored, { Channel::Release, "0.9.9", "" }) == UpdateDecision::Downgrade);
	EXPECT(DecideUpdate(stored, { Channel::Release, "nightly", "" }) == UpdateDecision::Available);
	EXPECT(DecideUpdate(stored, { Channel::Branch, std::string(40, 'a'), "main" }) == UpdateDecision::Available);
	EXPECT(DecideUpdate(commit, { Channel::Release, "v0.1.0", "" }) == UpdateDecision::Available);
}

void Registries() {
	auto registry = Registry();
	auto plugin = InstalledPlugin();
	plugin.id = "dev.alice.autoreply";
	plugin.name = "Auto reply";
	plugin.version = "1.2.0";
	plugin.description = "Answers";
	plugin.author = "alice";
	plugin.homepage = "https://github.com/alice/autoreply";
	plugin.requested.add(Permission::Ui);
	plugin.requested.add(Permission::Network);
	plugin.network = { "api.example.com" };
	plugin.source = { "alice", "plugins", "packages/autoreply", Channel::Branch, std::string(40, 'c'), "main",
		std::string(40, 'c'), 4815162342 };
	plugin.pinned = true;
	plugin.previousVersion = "1.1.0";
	plugin.previousSource = { "alice", "plugins", "packages/autoreply", Channel::Branch, std::string(40, 'd'), "main",
		"", 4815162342 };
	plugin.granted.add(Permission::Ui);
	plugin.enabled = true;
	plugin.installedAt = 1761523200;
	plugin.lastError = "TypeError: x is \"undefined\"";
	registry.plugins.push_back(plugin);
	auto second = InstalledPlugin();
	second.id = "abc";
	registry.plugins.push_back(second);
	auto broken = InstalledPlugin();
	broken.id = "../evil";
	registry.plugins.push_back(broken);

	const auto json = SerializeRegistry(registry);
	EXPECT(json.find("../evil") == std::string::npos);
	const auto parsed = ParseRegistry(json);
	EXPECT(parsed && parsed.value().skipped.empty() && parsed.value().plugins.size() == 2);
	const auto &back = parsed.value().plugins[0];
	EXPECT(back.id == plugin.id && back.name == plugin.name && back.version == plugin.version);
	EXPECT(back.description == plugin.description && back.author == plugin.author && back.homepage == plugin.homepage);
	EXPECT(back.requested == plugin.requested && back.granted == plugin.granted && back.network == plugin.network);
	EXPECT(back.source.owner == "alice" && back.source.repo == "plugins" && back.source.subdir == "packages/autoreply");
	EXPECT(back.source.channel == Channel::Branch && back.source.ref == std::string(40, 'c') && back.source.label == "main");
	EXPECT(back.enabled && back.installedAt == 1761523200 && back.lastError == plugin.lastError);
	EXPECT(back.source.commit == std::string(40, 'c') && back.source.repoId == 4815162342);
	EXPECT(back.pinned && back.previousVersion == "1.1.0");
	EXPECT(back.previousSource.ref == std::string(40, 'd') && back.previousSource.repoId == 4815162342);
	EXPECT(back.previousSource.commit.empty() && back.loadReason.empty());
	EXPECT(NeedsNewConsent(back.granted, back.requested));
	const auto &empty = parsed.value().plugins[1];
	EXPECT(empty.id == "abc" && !empty.enabled && empty.granted.empty() && empty.source.channel == Channel::Release);
	EXPECT(!empty.pinned && empty.previousVersion.empty() && empty.source.repoId == 0 && empty.source.commit.empty());
	EXPECT(SerializeRegistry(parsed.value()) == json);

	const auto tolerant = ParseRegistry(R"({"format":7,"extra":{"x":1},"plugins":[
		{"id":"good.one","enabled":"yes","installedAt":"soon","granted":["ui","future.perm",3],"source":"nope","newKey":1},
		{"id":"Bad Id"}, 5, {"name":"no id"}, {"id":"good.one"},
		{"id":"evil.src","source":{"owner":"a","repo":"b","subdir":"../.."}},
		{"id":"evil.ref","source":{"owner":"a","repo":"b","ref":"../x"}},
		{"id":"ok.release","source":{"owner":"a","repo":"b","channel":"weird","ref":"v1"}}
	]})");
	EXPECT(tolerant);
	const auto &list = tolerant.value().plugins;
	EXPECT(list.size() == 2 && list[0].id == "good.one" && list[1].id == "ok.release");
	EXPECT(!list[0].enabled && list[0].installedAt == 0 && list[0].source.owner.empty());
	EXPECT((list[0].granted.ids() == std::vector<std::string>{ "ui" }));
	EXPECT(list[1].source.channel == Channel::Release && list[1].source.ref == "v1");
	EXPECT(tolerant.value().skipped.size() == 6);
	EXPECT(tolerant.value().skipped[0] == "plugins[1]: bad id");
	EXPECT(tolerant.value().skipped[3] == "plugins[4]: duplicate id good.one");

	EXPECT(ParseRegistry("{}") && ParseRegistry("{}").value().plugins.empty());
	EXPECT(!ParseRegistry("") && !ParseRegistry("[]") && !ParseRegistry("null"));
	EXPECT(!ParseRegistry(R"({"plugins":{}})"));

	auto damaged = Registry();
	damaged.plugins.push_back(second);
	damaged.plugins[0].lastError = "\xff\xfe";
	EXPECT(ParseRegistry(SerializeRegistry(damaged)));
}

} // namespace

int main() {
	Versions();
	Permissions();
	Methods();
	Hosts();
	Paths();
	Manifests();
	Github();
	Registries();
	std::cout << "plugin core fixture ok";
}
'''


# Each mutant of a core unit must fail the fixture.
MUTANTS = (
    ('plugin_manifest', '\t\t&& IsLowerAlnum(id.back())\n', ''),
    ('plugin_manifest', '\t\t&& (id.find("..") == std::string_view::npos)\n', ''),
    ('plugin_manifest', '\t\t&& !IsWindowsDevice(id);', ';'),
    ('plugin_manifest', 'return value <= 65535;', 'return true;'),
    ('plugin_manifest', "if (port.empty() || port.size() > 5 || port.front() == '0') {", 'if (port.empty()) {'),
    ('plugin_permissions', '\t"payments.",\n', ''),
    ('plugin_permissions', 'return listed(kForbiddenReads);', 'return false;'),
    ('plugin_permissions', 'if (name.starts_with(kAllowedPrefix)) {', 'if (false) {'),
    ('plugin_permissions', '\t"account.getauthorizations",\n', ''),
    ('plugin_permissions', '\t"account.inittakeoutsession",\n', ''),
    ('plugin_permissions', '\t"messages.accepturlauth",\n', ''),
    ('plugin_permissions', '\t"account.getsecurevalue",\n', ''),
    ('plugin_permissions', '\t"payments.getpaymentform",\n', ''),
    ('plugin_permissions', '\t"account.registerdevice",\n', ''),
    ('plugin_permissions', '\t"account.registerpasskey",\n', ''),
    ('plugin_permissions', '\t"messages.requestwebview",\n', ''),
    ('plugin_permissions', '\t"account.updateconnectedbot",\n', ''),
    ('plugin_permissions', '\t"bots.allowsendmessage",\n', ''),
    ('plugin_permissions', '\t"messages.togglebotinattachmenu",\n', ''),
)


def check_listed_methods(read=source):
    """Every refused name is a real api.tl function: a typo refuses nothing."""
    import re
    text = read(CORE + 'plugin_permissions.cpp')
    tl = read('Telegram/SourceFiles/mtproto/scheme/api.tl')
    functions = tl[tl.index('---functions---'):]
    known = {name.lower() for name in re.findall(r'^([a-zA-Z]+\.[a-zA-Z0-9]+)#', functions, re.M)}
    for block in ('kForbiddenMethods', 'kForbiddenReads'):
        body = re.search(block + r' = std::array<std::string_view, \d+>\{(.*?)\n\};', text, re.S).group(1)
        names = re.findall(r'^\t"([^"]+)"', body, re.M)
        assert names, block
        for name in names:
            assert name in known, f'{block} lists {name}, which api.tl has no function for'


def check(root, read=source):
    check_listed_methods(read)
    core = root / 'plugin_core' / 'luxury' / 'plugins' / 'core'
    core.mkdir(parents=True, exist_ok=True)
    units = []
    for unit in UNITS:
        for ext in ('.h', '.cpp'):
            text = read(CORE + unit + ext)
            assert '#include <Q' not in text and '"base/' not in text, unit + ext
            (core / (unit + ext)).write_text(text, encoding='utf-8', newline='\n')
        units.append(str(core / (unit + '.cpp')))
    flags = ['-Wall', '-Wextra', '-Werror', '-I', str(root / 'plugin_core'), '-I', str(ROOT / 'Telegram/SourceFiles')]
    cpp(root, 'plugin_core', MAIN, [*flags, *units])
    for index, (unit, before, after) in enumerate(MUTANTS):
        target = core / (unit + '.cpp')
        text = target.read_text(encoding='utf-8')
        assert text.count(before) == 1, f'mutant anchor moved: {before!r}'
        target.write_text(text.replace(before, after), encoding='utf-8', newline='\n')
        try:
            cpp(root, f'plugin_core_m{index}', MAIN, [*flags, *units])
        except AssertionError:
            pass
        else:
            raise AssertionError(f'the core fixture accepts the mutant {after!r}')
        finally:
            target.write_text(text, encoding='utf-8', newline='\n')
    print('Plugin core PASS: manifest rules, permissions, refused TL methods, host lists, GitHub links and URLs, update decision, registry round-trip')


if __name__ == '__main__':
    import tempfile
    from pathlib import Path
    with tempfile.TemporaryDirectory() as d:
        check(Path(d))
