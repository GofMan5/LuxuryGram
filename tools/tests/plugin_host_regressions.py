"""Plugin host: the Qt-free decisions and the client hook contract.

docs/plugins/ARCHITECTURE.md, host half. The client is not built here, so:
- plugin_host_logic.h is compiled with -Wall -Wextra -Werror against the
  real core units and checked: beforeSend folding, local commands, the !X
  query refusal, network widening and fetch ports, commit consent and the
  grant against what the consent box showed, timer clamps, the CPU budget,
  reserved hotkeys, the in-flight cap, log truncation, client
  compatibility, settings, labels and the directory swap with rollback;
- the Qt half is checked statically: every hook returns early unless a
  plugin runs, every engine entry goes through the CPU budget, api.call
  refuses forbidden and query-wrapping methods before encoding, requests
  and watchers are capped, commitInstall compares the reviewed plugin.json
  bytes and grants only what was shown, every Manager method of the header
  is defined, api.tl is embedded where ApiSchema reads it, and each client
  call site sits where the contract puts it.
Every check is also run against a mutant it must reject.

Bounded source check; not a native acceptance.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile
from library_regressions import CXX, ROOT, STATIC, function, run, source, standalone

SRC = 'Telegram/SourceFiles/'
PLUGINS = SRC + 'luxury/plugins/'
LOGIC = PLUGINS + 'plugin_host_logic.h'
HOOKS = PLUGINS + 'plugin_hooks.cpp'
HOOKS_H = PLUGINS + 'plugin_hooks.h'
HOST = PLUGINS + 'plugin_host_api.cpp'
MANAGER = PLUGINS + 'plugin_manager.cpp'
HEADER = PLUGINS + 'plugin_manager.h'
INSTALLER = PLUGINS + 'plugin_installer.cpp'
QRC = 'Telegram/Resources/qrc/luxury/luxury.qrc'
APIWRAP = SRC + 'apiwrap.cpp'
UPDATES = SRC + 'api/api_updates.cpp'
SESSION = SRC + 'data/data_session.cpp'
WIDGET = SRC + 'history/history_widget.cpp'
CHAT = SRC + 'history/view/history_view_chat_section.cpp'
APP = SRC + 'core/application.cpp'
INNER = SRC + 'history/history_inner_widget.cpp'
VIEW_MENU = SRC + 'history/view/history_view_context_menu.cpp'
SYNC = SRC + 'luxury/features/forward/luxury_sync.cpp'
SCHEDULED = SRC + 'history/view/history_view_scheduled_section.cpp'
SHORTCUTS = SRC + 'settings/business/settings_shortcut_messages.cpp'
STORIES = SRC + 'media/stories/media_stories_reply.cpp'
SHARE = SRC + 'boxes/share_box.cpp'
DOCS = 'docs/plugins/ARCHITECTURE.md'
CORE = ('plugin_version', 'plugin_permissions', 'plugin_manifest', 'plugin_source', 'tl_schema')

MAIN = r'''
#include "luxury/plugins/plugin_host_logic.h"

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>

using namespace Luxury::Plugins;
using json = nlohmann::json;
namespace fs = std::filesystem;

#define EXPECT(condition) do { if (!(condition)) { \
	std::cerr << "line " << __LINE__ << ": " #condition "\n"; std::abort(); } } while (0)

namespace {

PermissionSet Set(std::initializer_list<Permission> list) {
	auto result = PermissionSet();
	for (const auto p : list) {
		result.add(p);
	}
	return result;
}

void Folding() {
	auto fold = SendFold{ "hi" };
	FoldBeforeSend(fold, json());
	FoldBeforeSend(fold, json(true));
	FoldBeforeSend(fold, json(42));
	FoldBeforeSend(fold, json("hi"));
	EXPECT(!fold.changed && !fold.cancelled && fold.text == "hi");
	FoldBeforeSend(fold, json("HI"));
	EXPECT(fold.changed && fold.text == "HI");
	FoldBeforeSend(fold, json("HI!"));
	EXPECT(fold.text == "HI!"); // The next plugin sees the new text.
	FoldBeforeSend(fold, json(false));
	EXPECT(fold.cancelled);
	FoldBeforeSend(fold, json("late"));
	EXPECT(fold.text == "HI!"); // Nothing after a cancel counts.
}

void Commands() {
	const auto a = ParseLocalCommand("/echo  hello world \n");
	EXPECT(a && a->name == "echo" && a->args == "hello world");
	const auto b = ParseLocalCommand("/ping");
	EXPECT(b && b->name == "ping" && b->args.empty());
	EXPECT(!ParseLocalCommand("echo"));
	EXPECT(!ParseLocalCommand("/"));
	EXPECT(!ParseLocalCommand("/Echo"));
	EXPECT(!ParseLocalCommand("/ echo"));
	EXPECT(!ParseLocalCommand("/echo@bot x"));
	EXPECT(!ParseLocalCommand("/" + std::string(33, 'a')));
	EXPECT(ParseLocalCommand("/" + std::string(32, 'a')));
}

void Queries() {
	const auto schema = Tl::Schema::Parse(
		"boolTrue#997275b5 = Bool;\n"
		"---functions---\n"
		"invokeWithLayer#da9b0d0d {X:Type} layer:int query:!X = X;\n"
		"wrap#11111111 {X:Type} list:Vector<Vector<!X>> = X;\n"
		"help.getConfig#c4f9186b = Bool;\n");
	EXPECT(schema);
	EXPECT(HasQueryParam(*schema.value->method("invokeWithLayer")));
	EXPECT(HasQueryParam(*schema.value->method("wrap"))); // !X two levels down.
	EXPECT(!HasQueryParam(*schema.value->method("help.getConfig")));

	// Stars: a paid flag is refused wherever the params nest it.
	for (const auto name : kPaidParams) {
		EXPECT(HasPaidParam(json{ { std::string(name), 1 } }));
	}
	EXPECT(HasPaidParam(json::parse(R"({"peer":{},"allow_paid_stars":"5"})")));
	EXPECT(HasPaidParam(json::parse(R"({"multi_media":[{"x":{"suggested_post":{}}}]})")));
	EXPECT(HasPaidParam(json::parse(R"([[{"refund_charged":true}]])")));
	EXPECT(!HasPaidParam(json::parse(R"({"message":"allow_paid_stars","peer":{}})")));
	EXPECT(!HasPaidParam(json::parse(R"({"multi_media":[{"x":{"y":["suggested_post"]}}]})")));
	EXPECT(!HasPaidParam(json()));
	EXPECT(std::size(kPaidParams) == 6);

	// The service chat 777000: login codes never reach a plugin.
	EXPECT(MentionsServiceUser(json::parse(R"({"peer":{"_":"inputPeerUser","user_id":"777000"}})")));
	EXPECT(MentionsServiceUser(json::parse(R"({"id":[{"user_id":777000}]})")));
	EXPECT(MentionsServiceUser(json::parse(R"({"user_id":777000.0})")));
	EXPECT(MentionsServiceUser(json::parse(R"([{"_":"updateServiceNotification","message":"code"}])")));
	EXPECT(!MentionsServiceUser(json::parse(R"({"user_id":"7770001","id":"777000"})")));
	// Every form the encoder reads as the long 777000, under every key.
	for (const auto id : { R"("0777000")", R"("+777000")", R"(" 777000")",
			R"("777000 ")", R"("\t+000777000\n")",
			"777000.0", "777000", "7.77e5" }) {
		for (const auto key : { "user_id", "peer", "from_id", "peer_id" }) {
			EXPECT(MentionsServiceUser(json::parse(
				std::string("{\"") + key + "\":" + id + "}")));
		}
		EXPECT(MentionsServiceUser(json::parse(
			std::string(R"({"_":"user","id":)") + id + "}")));
		EXPECT(!MentionsServiceUser(json::parse(
			std::string(R"({"_":"message","id":)") + id + "}")));
	}
	for (const auto id : { R"("7770000")", R"("-777000")", R"("++777000")",
			R"("777 000")", R"("777000x")", R"("")", R"("  ")", R"("0x777000")",
			R"("777000.0")", R"("7.77e5")",
			"777000.5", "-777000", "77700", "true", "null" }) {
		EXPECT(!MentionsServiceUser(json::parse(
			std::string(R"({"user_id":)") + id + "}")));
	}
	EXPECT(MentionsServiceUser(json{ { "user_id", std::int64_t(777000) } }));
	EXPECT(MentionsServiceUser(json{ { "user_id", std::uint64_t(777000) } }));
	EXPECT(MentionsServiceUser(json::parse(R"({"peer":{"user_id":"777000"}})")));
	EXPECT(MentionsServiceUser(json::parse(R"({"_":"peerUser","user_id":"+777000"})")));
	EXPECT(MentionsServiceUser(json::parse(R"({"q":[{"from_id":{"_":"peerUser","user_id":" 0777000"}}]})")));
	EXPECT(MentionsServiceUser(json::parse(R"({"_":"userFull","id":777000.0})")));
	EXPECT(MentionsServiceUser(json::parse(R"({"_":"userEmpty","id":"777000"})")));
	EXPECT(!MentionsServiceUser(json::parse(R"({"message":"777000","_":"updateNewMessage"})")));
	auto history = json::parse(R"({"messages":[
		{"id":1,"peer_id":{"user_id":"777000"}},
		{"id":2,"peer_id":{"user_id":"42"}},
		{"id":3,"fwd_from":{"from_id":{"user_id":"777000"}}}],
		"other_updates":[{"_":"updateServiceNotification"},{"_":"updateUserName"}]})");
	EXPECT(StripServiceUser(history));
	EXPECT(history["messages"].size() == 1 && history["messages"][0]["id"] == 2);
	EXPECT(history["other_updates"].size() == 1);
	auto shortMessage = json::parse(R"({"_":"updateShortMessage","user_id":"777000"})");
	EXPECT(!StripServiceUser(shortMessage));
	auto clean = json::parse(R"({"users":[{"id":"777000"}]})");
	EXPECT(StripServiceUser(clean) && clean["users"].size() == 1);
	// The service user object itself leaves users[], its neighbours stay.
	auto dialogs = json::parse(R"({"users":[{"_":"user","id":"777000","first_name":"Telegram"},
		{"_":"user","id":"42"}],"chats":[]})");
	EXPECT(StripServiceUser(dialogs) && dialogs["users"].size() == 1
		&& dialogs["users"][0]["id"] == "42");
	auto padded = json::parse(R"({"_":"updateShortMessage","user_id":"0777000"})");
	EXPECT(!StripServiceUser(padded));
	auto users = json::parse(R"([{"_":"user","self":true,"phone":"79990001122","id":"1"},
		{"_":"user","phone":"15550000000","id":"2"},
		{"users":[{"_":"user","self":true,"phone":"79990001122"}]},
		{"_":"updateUserPhone","user_id":"1","phone":"79990001122"}])");
	HideOwnPhone(users);
	EXPECT(!users[0].contains("phone") && users[0]["id"] == "1");
	EXPECT(users[1]["phone"] == "15550000000");
	EXPECT(!users[2]["users"][0].contains("phone"));
	EXPECT(!users[3].contains("phone") && users[3]["user_id"] == "1");
	// An id over 48 bits would mask into another peer: 2^48 + 777000 is
	// the service chat again.
	constexpr auto kAlias = (std::int64_t(1) << 48);
	EXPECT(!IsBareDialogId(kAlias + 777000));
	EXPECT(!IsBareDialogId(-(kAlias + 777000)));
	EXPECT(!IsBareDialogId(kAlias));
	EXPECT(!IsBareDialogId(-kAlias));
	EXPECT(!IsBareDialogId(0));
	EXPECT(!IsBareDialogId(std::numeric_limits<std::int64_t>::min()));
	EXPECT(IsBareDialogId(777000));
	EXPECT(IsBareDialogId(-1001234567890));
	EXPECT(IsBareDialogId(kAlias - 1));
	EXPECT(IsBareDialogId(-(kAlias - 1)));
}

void Network() {
	const auto requested = Set({ Permission::Network });
	EXPECT(!NetworkWidened(true, { "a.com" }, PermissionSet(), { "b.com" }));
	EXPECT(NetworkWidened(false, {}, requested, { "a.com" }));
	EXPECT(!NetworkWidened(true, { "a.com", "b.com" }, requested, { "b.com" }));
	EXPECT(NetworkWidened(true, { "a.com" }, requested, { "a.com", "b.com" }));
	EXPECT(!NetworkWidened(true, { "*" }, requested, { "b.com" }));
	EXPECT(FetchAllowed("https", "a.com", -1, { "a.com" }));
	EXPECT(FetchAllowed("https", "a.com", 443, { "a.com" }));
	EXPECT(!FetchAllowed("http", "a.com", -1, { "a.com" }));
	EXPECT(!FetchAllowed("https", "b.com", -1, { "a.com" }));
	// Any port but 443 must be named in the manifest.
	EXPECT(!FetchAllowed("https", "a.com", 8443, { "a.com" }));
	EXPECT(FetchAllowed("https", "a.com", 8443, { "a.com:8443" }));
	EXPECT(!FetchAllowed("https", "a.com", 8080, { "a.com:8443" }));
	EXPECT(!FetchAllowed("https", "a.com", -1, { "a.com:8443" }));
}

void Grants() {
	const auto none = PermissionSet();
	const auto ui = Set({ Permission::Ui });
	const auto net = Set({ Permission::Network });
	const auto uiNet = Set({ Permission::Ui, Permission::Network });
	const auto noHosts = std::vector<std::string>();
	EXPECT(MissingPermissions(uiNet, ui) == net);
	EXPECT(StoredGrant(ui, uiNet) == ui); // Never more than requested.
	// A fresh install always asks; an update only for something new.
	EXPECT(NeedsConsent(false, none, false, false));
	EXPECT(!NeedsConsent(false, ui, false, true));
	EXPECT(!NeedsConsent(true, none, false, false));
	EXPECT(NeedsConsent(true, ui, false, false));
	EXPECT(NeedsConsent(true, none, true, false));
	// What was shown is granted, within what plugin.json asks now.
	EXPECT(CommitGrant(uiNet, none, uiNet, false, { "a.com" }, { "a.com" }) == uiNet);
	EXPECT(CommitGrant(ui, none, uiNet, false, noHosts, noHosts) == ui);
	// A silent update keeps the earlier grant, cut to the new request.
	EXPECT(CommitGrant(ui, uiNet, none, false, noHosts, noHosts) == ui);
	EXPECT(CommitGrant(uiNet, ui, net, false, noHosts, noHosts) == uiNet);
	// More asked at commit than the box showed: review again.
	EXPECT(!CommitGrant(uiNet, none, ui, false, noHosts, noHosts));
	EXPECT(!CommitGrant(uiNet, ui, none, false, noHosts, noHosts));
	// Wider hosts must all have been shown.
	EXPECT(!CommitGrant(net, net, none, true, { "a.com", "b.com" }, { "a.com" }));
	EXPECT(CommitGrant(net, net, none, true, { "a.com", "b.com" }, { "a.com", "b.com" }) == net);
	EXPECT(CommitGrant(net, net, none, false, { "b.com" }, noHosts) == net);
}

void Timers() {
	EXPECT(TimerDelay(json(0), true) == 50);
	EXPECT(TimerDelay(json(5), true) == 50);
	EXPECT(TimerDelay(json(49.9), true) == 50);
	EXPECT(TimerDelay(json(std::nan("")), true) == 50);
	EXPECT(TimerDelay(json("x"), true) == 50);
	EXPECT(kMaxTimers == 64);
	EXPECT(TimerDelay(json(-5), false) == 4);
	EXPECT(TimerDelay(json(2), false) == 4);
	EXPECT(TimerDelay(json(std::nan("")), false) == 4);
	EXPECT(TimerDelay(json(7), false) == 7);
	EXPECT(TimerDelay(json(20), true) == 50);
	EXPECT(TimerDelay(json(60), true) == 60);
	EXPECT(TimerDelay(json(1e300), false) == (1 << 30));
}

void Cpu() {
	constexpr auto ms = std::int64_t(1000); // The budget counts µs.
	{ // Three overruns within 10 s stop it.
		auto b = CpuBudget();
		EXPECT(!b.record(0, 0, true));
		EXPECT(!b.record(4000 * ms, 0, true));
		EXPECT(b.record(9000 * ms, 0, true));
	}
	{ // Spread wider they do not.
		auto b = CpuBudget();
		EXPECT(!b.record(0, 0, true));
		EXPECT(!b.record(6000 * ms, 0, true));
		EXPECT(!b.record(12000 * ms, 0, true));
		EXPECT(b.record(15000 * ms, 0, true));
	}
	{ // More than 500 ms within 5 s stops it; exactly 500 does not.
		auto b = CpuBudget();
		for (auto i = 0; i != 5; ++i) {
			EXPECT(!b.record(i * 1000 * ms, 100 * ms, false));
		}
		EXPECT(b.record(4500 * ms, 1, false));
	}
	{ // Steady 100 ms a second stays within budget forever.
		auto b = CpuBudget();
		for (auto i = 0; i != 60; ++i) {
			EXPECT(!b.record(i * 1000 * ms, 100 * ms, false));
		}
		EXPECT(b.slices.size() == 5 && b.used == 500 * ms);
	}
	{ // Busy 4 s, idle 1 s, again: pausing does not reset the sum.
		auto b = CpuBudget();
		auto stopped = std::int64_t(0);
		for (auto now = std::int64_t(0); now < 30'000 && !stopped; now += 250) {
			if ((now / 1000) % 5 != 4 && b.record(now * ms, 33 * ms, false)) {
				stopped = now;
			}
		}
		EXPECT(stopped == 3750); // 16 slices: 528 ms by the 4th busy second.
	}
	{ // The same rate with an idle second is 4/5 of it: 105 ms a second stays.
		auto b = CpuBudget();
		for (auto now = std::int64_t(0); now < 60'000; now += 1000) {
			if ((now / 1000) % 5 != 4) {
				EXPECT(!b.record(now * ms, 105 * ms, false));
			}
		}
	}
	{ // One slice over the whole budget.
		auto b = CpuBudget();
		EXPECT(b.record(0, 501 * ms, false));
	}
	{ // Slices older than 5 s no longer count.
		auto b = CpuBudget();
		EXPECT(!b.record(0, 500 * ms, false));
		EXPECT(!b.record(5000 * ms, 500 * ms, false));
		EXPECT(b.record(9999 * ms, 1, false));
	}
	{ // A negative elapsed time never pays back.
		auto b = CpuBudget();
		EXPECT(!b.record(0, -1000 * ms, false));
		EXPECT(b.record(1, 500 * ms, false));
	}
	{ // 10000 trivial entries a tenth of a millisecond apart: each costs
		// the minimum charge, so a flood of 1 µs entries is stopped too.
		auto b = CpuBudget();
		auto stopped = 0;
		for (auto i = 0; i != 10000 && !stopped; ++i) {
			if (b.record(std::int64_t(i) * 100, 1, false)) {
				stopped = i + 1;
			}
		}
		EXPECT(stopped == 5001); // 5001 * 100 µs > 500 ms.
	}
	{ // The same entries one per 10 ms stay far within budget.
		auto b = CpuBudget();
		for (auto i = 0; i != 10000; ++i) {
			EXPECT(!b.record(std::int64_t(i) * 10 * ms, 1, false));
		}
		EXPECT(b.slices.size() == 500 && b.used == 500 * kMinChargeUs);
	}
	{ // A drain of queued events pays the minimum once, not per event:
		// a full 256-event drain of 1 us handlers costs 0.1 ms + 255 us.
		auto b = CpuBudget();
		for (auto i = 0; i != 256; ++i) {
			EXPECT(!b.record(0, 1, false, i == 0));
		}
		EXPECT(b.used == kMinChargeUs + 255);
		EXPECT(!b.record(0, -5, false, false) && b.used == kMinChargeUs + 255);
	}
}

void Gestures() {
	EXPECT(GestureActive(999, 1000));
	EXPECT(!GestureActive(1000, 1000));
	EXPECT(!GestureActive(5, 0)); // No gesture yet.
	EXPECT(kGestureMs == 1000);
	EXPECT(ToastAllowed(5, 0));
	EXPECT(!ToastAllowed(1999, 1000));
	EXPECT(ToastAllowed(2000, 1000));
}

void Events() {
	auto q = EventQueue();
	EXPECT(q.push("message", json(1))); // First: schedule a drain.
	EXPECT(!q.push("update", json(2)));
	auto taken = q.take();
	EXPECT(taken.size() == 2 && taken[0].first == "message" && taken[1].second == 2);
	EXPECT(q.events.empty());
	for (auto i = 0; i != int(kMaxQueuedEvents) + 10; ++i) {
		EXPECT(q.push("update", json(i)) == (i == 0));
	}
	EXPECT(q.events.size() == kMaxQueuedEvents && q.dropped == 10);
	EXPECT(q.events.front().second == 10); // The oldest went first.
	EXPECT(q.events.back().second == int(kMaxQueuedEvents) + 9);
	EXPECT(q.take().size() == kMaxQueuedEvents && q.dropped == 0);
	EXPECT(q.push("update", json(0)));
	q.clear();
	EXPECT(q.events.empty() && q.dropped == 0 && q.push("x", json()));
	for (const auto name : { "message", "messageOut", "beforeSend", "update", "unload" }) {
		EXPECT(KnownEvent(name));
	}
	for (const auto name : { "", "Message", "updates", "ping", "beforesend" }) {
		EXPECT(!KnownEvent(name));
	}
}

std::array<std::uint8_t, 16> V4(int a, int b, int c, int d) {
	return { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF,
		std::uint8_t(a), std::uint8_t(b), std::uint8_t(c), std::uint8_t(d) };
}

std::array<std::uint8_t, 16> V6(
		std::initializer_list<int> head,
		std::initializer_list<int> tail = {}) {
	auto result = std::array<std::uint8_t, 16>();
	auto i = std::size_t(0);
	for (const auto byte : head) {
		result[i++] = std::uint8_t(byte);
	}
	i = 16 - tail.size();
	for (const auto byte : tail) {
		result[i++] = std::uint8_t(byte);
	}
	return result;
}

void Addresses() {
	// Every first octet: only the public unicast ranges pass.
	for (auto a = 0; a != 256; ++a) {
		const auto expected = (a != 0 && a != 10 && a != 127 && a < 224);
		if (a != 100 && a != 169 && a != 172 && a != 192) {
			EXPECT(IsPublicAddress(V4(a, 1, 2, 3)) == expected);
		}
	}
	for (auto b = 0; b != 256; ++b) {
		EXPECT(IsPublicAddress(V4(100, b, 0, 1)) == (b < 64 || b >= 128));
		EXPECT(IsPublicAddress(V4(169, b, 0, 1)) == (b != 254));
		EXPECT(IsPublicAddress(V4(172, b, 0, 1)) == (b < 16 || b >= 32));
		EXPECT(IsPublicAddress(V4(192, b, 0, 1)) == (b != 168));
	}
	EXPECT(IsPublicAddress(V4(8, 8, 8, 8)));
	EXPECT(IsPublicAddress(V4(149, 154, 167, 99)));
	EXPECT(!IsPublicAddress(V4(255, 255, 255, 255)));
	EXPECT(!IsPublicAddress(V6({}))); // 0.0.0.0 and :: both.
	EXPECT(!IsPublicAddress(V6({}, { 1 }))); // ::1.
	// Deprecated IPv4-compatible and NAT64 forms carry the IPv4 verdict.
	EXPECT(!IsPublicAddress(V6({}, { 127, 0, 0, 1 })));
	EXPECT(!IsPublicAddress(V6({}, { 10, 0, 0, 1 })));
	EXPECT(IsPublicAddress(V6({}, { 8, 8, 8, 8 })));
	EXPECT(!IsPublicAddress(V6({ 0, 0x64, 0xFF, 0x9B }, { 192, 168, 1, 1 })));
	EXPECT(IsPublicAddress(V6({ 0, 0x64, 0xFF, 0x9B }, { 8, 8, 8, 8 })));
	// Local-use NAT64 64:ff9b:1::/48 is refused whatever it embeds.
	EXPECT(!IsPublicAddress(V6({ 0, 0x64, 0xFF, 0x9B, 0, 1 }, { 10, 0, 0, 1 })));
	EXPECT(!IsPublicAddress(V6({ 0, 0x64, 0xFF, 0x9B, 0, 1 }, { 8, 8, 8, 8 })));
	EXPECT(IsPublicAddress(V6({ 0, 0x64, 0xFF, 0x9B, 0, 2 }, { 8, 8, 8, 8 })));
	// Unique local, link-local, site-local and multicast IPv6.
	for (auto first = 1; first != 256; ++first) {
		EXPECT(IsPublicAddress(V6({ first, 0x80 }, { 1 }))
			== (first != 0xFC && first != 0xFD && first != 0xFE && first != 0xFF));
	}
	EXPECT(!IsPublicAddress(V6({ 0xFE, 0x80 }, { 1 })));
	EXPECT(!IsPublicAddress(V6({ 0xFE, 0xBF }, { 1 })));
	EXPECT(!IsPublicAddress(V6({ 0xFE, 0xC0 }, { 1 })));
	EXPECT(IsPublicAddress(V6({ 0xFE, 0x7F }, { 1 })));
	EXPECT(IsPublicAddress(V6({ 0x20, 0x01, 0x0D, 0xB8 }, { 1 })));
	EXPECT(IsPublicAddress(V6({ 0x2A, 0x00, 0x14, 0x50 }, { 0x0E })));
	EXPECT(IsPublicAddress(V6({ 0, 0, 0, 1 }, { 1 }))); // Not the ::/96 block.
	// Cross-origin redirects drop credentials, case-insensitively.
	for (const auto name : { "Authorization", "cookie", "COOKIE", "Host",
			"Proxy-Authorization", "proxy-connection" }) {
		EXPECT(StripOnCrossOrigin(name));
	}
	for (const auto name : { "Accept", "Content-Type", "X-Cookie", "Hostname",
			"Authorization2", "proxy" }) {
		EXPECT(!StripOnCrossOrigin(name));
	}
}

void Hotkeys() {
	const auto ctrl = kControlModifier;
	const auto shift = kShiftModifier;
	const auto alt = 0x08000000; // Qt::AltModifier.
	for (const auto key : { 'C', 'V', 'X', 'A', 'Z', 'Y' }) {
		EXPECT(HotkeyReserved(ctrl | key));
		EXPECT(HotkeyReserved(ctrl | shift | key));
		EXPECT(!HotkeyReserved(ctrl | alt | key));
		EXPECT(!HotkeyReserved(alt | key));
		EXPECT(!HotkeyReserved(key));
	}
	for (const auto key : { kKeyReturn, kKeyEnter, kKeyEscape }) {
		EXPECT(HotkeyReserved(key));
		EXPECT(HotkeyReserved(ctrl | key));
		EXPECT(HotkeyReserved(shift | alt | key));
	}
	EXPECT(!HotkeyReserved(ctrl | 'K'));
	EXPECT(!HotkeyReserved(ctrl | shift | 'K'));
	static_assert(kMaxInFlight == 16 && kMaxSettingWatchers == 64);
	EXPECT(InFlightAllowed(0) && InFlightAllowed(15));
	EXPECT(!InFlightAllowed(16) && !InFlightAllowed(17));
}

void Truncate() {
	static_assert(kMaxLogText == 4096);
	EXPECT(TruncateText("short") == "short");
	EXPECT(TruncateText(std::string(4096, 'a')) == std::string(4096, 'a'));
	const auto ascii = TruncateText(std::string(10000, 'a'));
	EXPECT(ascii.size() == 4096 && ascii.ends_with("a..."));
	// The cut never splits a 2- or 4-byte character.
	for (const auto &unit : { std::string("\xD1\x8F"), std::string("\xF0\x9F\x98\x80") }) {
		auto text = std::string();
		while (text.size() < 10000) {
			text += unit;
		}
		const auto cut = TruncateText(text);
		EXPECT(cut.ends_with("...") && cut.size() <= 4096);
		const auto kept = cut.size() - 3;
		EXPECT(kept % unit.size() == 0 && kept + unit.size() > 4093);
	}
}

void Compatibility() {
	auto manifest = Manifest();
	manifest.api = kPluginApiVersion;
	EXPECT(CompatibilityError(manifest, "1.0.21").empty());
	manifest.minClient = "1.0.21";
	EXPECT(CompatibilityError(manifest, "1.0.21").empty());
	manifest.minClient = "1.0.22";
	EXPECT(!CompatibilityError(manifest, "1.0.21").empty());
	manifest.minClient = "1.1.0";
	EXPECT(!CompatibilityError(manifest, "1.0.21").empty());
	manifest.minClient = {};
	manifest.api = kPluginApiVersion + 1;
	EXPECT(!CompatibilityError(manifest, "1.0.21").empty());
}

void Settings() {
	auto s = SettingDescriptor();
	s.type = SettingType::Int;
	s.defaultInt = 5;
	s.min = 1;
	s.max = 10;
	EXPECT(SettingDefault(s) == json(5));
	EXPECT(SettingAccepts(s, json(1)) && SettingAccepts(s, json(10)));
	EXPECT(!SettingAccepts(s, json(0)) && !SettingAccepts(s, json(11)));
	EXPECT(!SettingAccepts(s, json("5")) && !SettingAccepts(s, json(5.5)));
	s.type = SettingType::Choice;
	s.options = { "a", "b" };
	s.defaultString = "a";
	EXPECT(SettingDefault(s) == json("a"));
	EXPECT(SettingAccepts(s, json("b")) && !SettingAccepts(s, json("c")));
	s.type = SettingType::Bool;
	EXPECT(SettingAccepts(s, json(false)) && !SettingAccepts(s, json(0)));
	EXPECT(StorageFits(json::object()));
	EXPECT(!StorageFits(json{ { "k", std::string(kStorageQuota, 'x') } }));
}

void Labels() {
	EXPECT(SourceLabel("o", "r", Channel::Release, "v1.0", "") == "o/r@v1.0");
	EXPECT(SourceLabel("o", "r", Channel::Branch, "abcdef0123", "main")
		== "o/r#main (abcdef0)");
	EXPECT(AvailableLabel({ Channel::Release, "v2", "" }) == "v2");
	EXPECT(AvailableLabel({ Channel::Branch, "abcdef0123", "main" }) == "abcdef0");
}

void Write(const fs::path &path, const char *text) {
	fs::create_directories(path.parent_path());
	std::ofstream(path) << text;
}

std::string Read(const fs::path &path) {
	auto in = std::ifstream(path);
	return std::string(std::istreambuf_iterator<char>(in), {});
}

void Swap(const fs::path &root) {
	const auto target = root / "swap" / "id";
	const auto staged = root / "swap" / ".staging" / "1";
	Write(target / "code" / "main.js", "old");
	Write(target / "data" / "storage.json", "{}");
	Write(staged / "main.js", "new");
	// The manager swaps the code directory only: data/ stays.
	EXPECT(SwapDirectory(staged, target / "code").empty());
	EXPECT(Read(target / "code" / "main.js") == "new");
	EXPECT(Read(target / "data" / "storage.json") == "{}");
	EXPECT(!fs::exists(staged));
	EXPECT(!fs::exists(target / "code.old"));
	// A missing staged directory fails and puts the old code back.
	EXPECT(!SwapDirectory(root / "swap" / "missing", target / "code").empty());
	EXPECT(Read(target / "code" / "main.js") == "new");
	// A first install has nothing to restore.
	Write(root / "swap" / ".staging" / "2" / "main.js", "fresh");
	EXPECT(SwapDirectory(root / "swap" / ".staging" / "2", root / "swap" / "b" / "code").empty());
	EXPECT(Read(root / "swap" / "b" / "code" / "main.js") == "fresh");
}

} // namespace

int main(int argc, char **argv) {
	EXPECT(argc == 2);
	Folding();
	Commands();
	Queries();
	Network();
	Grants();
	Timers();
	Cpu();
	Gestures();
	Events();
	Addresses();
	Hotkeys();
	Truncate();
	Compatibility();
	Settings();
	Labels();
	Swap(argv[1]);
	std::cout << "plugin host fixture ok";
}
'''

# Compile-time mutants of plugin_host_logic.h the fixture must reject.
LOGIC_MUTANTS = (
    ('} else if (answer.is_boolean() && !answer.get<bool>()) {', '} else if (answer.is_boolean()) {'),
    ('if (fold.cancelled) {\n\t\treturn;', 'if (false) {\n\t\treturn;'),
    ('return HasQuery(a);', 'return a.kind == Tl::TypeRef::Kind::Query;'),
    ("return !has(\"*\") && !std::all_of(", "return !std::all_of("),
    ('return (!update || !newPermissions.empty() || widened) && !consented;',
     'return (!update || !newPermissions.empty()) && !consented;'),
    ('return PermissionSet(requested.bits() & granted.bits());', 'return PermissionSet(requested.bits() | granted.bits());'),
    ('return !needed || !current || (*needed > *current);', 'return !needed || !current || (*needed >= *current);'),
    ('\t\tif (hadOld) {\n\t\t\tfs::rename(old, target, ec);', '\t\tif (false) {\n\t\t\tfs::rename(old, target, ec);'),
    ('&& (name.size() <= 32)', '&& (name.size() <= 33)'),
    # Paid params.
    ('\t\t\t\t|| HasPaidParam(inner)) {', '\t\t\t\t) {'),
    ('\t\t\treturn HasPaidParam(v);', '\t\t\treturn false;'),
    ('\t"suggested_post",\n', ''),
    ('\t"allow_paid_floodskip",\n', ''),
    ('key) != std::end(kPaidParams)', 'key) == std::end(kPaidParams)'),
    # Service chat.
    ('if (typeName == "updateServiceNotification") {', 'if (false) {'),
    ('return value.get<std::int64_t>() == kServiceUserId;', 'return false;'),
    ('return value.get<std::uint64_t>() == std::uint64_t(kServiceUserId);', 'return false;'),
    ('return value.get<double>() == double(kServiceUserId);', 'return false;'),
    ('text = text.substr(first, text.find_last_not_of(space) - first + 1);', 'text = text.substr(first);'),
    ("if (text.front() == '+') {", "if (false) {"),
    ("if (text.front() == '+') {", "while (!text.empty() && text.front() == '+') {"),
    ('\t\t&& integer.ptr == end\n', ''),
    ('\t\t&& number == kServiceUserId;', '\t\t&& false;'),
    # Dialog ids over 48 bits.
    ('return magnitude && (magnitude <= kBareIdMask);', 'return magnitude != 0;'),
    ('return magnitude && (magnitude <= kBareIdMask);', 'return magnitude && (magnitude <= kBareIdMask + 1);'),
    ('? (std::uint64_t(0) - std::uint64_t(dialogId))', '? std::uint64_t(dialogId)'),
    ('\t\t\t|| (key == "peer")\n', ''),
    ('\t\t\t|| (key == "from_id")\n', ''),
    ('\t\t\t|| (key == "peer_id")\n', ''),
    ('|| (user && key == "id");', '|| (key == "id");'),
    ('|| (user && key == "id");', ';'),
    ('\t\t\t|| (typeName == "userEmpty")\n', ''),
    ('\t\t\t|| (typeName == "userFull");', ';'),
    ('\t\t\treturn MentionsServiceUser(element);', '\t\t\treturn false;'),
    ('\treturn !MentionsServiceUser(value);\n}', '\treturn true;\n}'),
    ('\t\t\tvalue.erase("phone");', '\t\t\t(void)0;'),
    ('|| (name != value.end() && *name == "updateUserPhone")) {', ') {'),
    ('\t\tfor (auto &inner : value) {\n\t\t\tHideOwnPhone(inner);', '\t\tfor (auto &inner : value) {\n\t\t\t(void)inner;'),
    # Fetch ports.
    ('return ((port < 0 || port == 443) && HostMatches(bare, host))', 'return (HostMatches(bare, host))'),
    ('} else if (port > 0 && std::string_view(entry).substr(colon) == suffix) {', '} else {'),
    # Commit grants.
    ('if (!shown.contains(MissingPermissions(requested, previously))', 'if (false'),
    ('|| (widened && !std::all_of(hosts.begin(), hosts.end(), seen))) {', ') {'),
    ('PermissionSet(previously.bits() | shown.bits()));', 'PermissionSet(shown.bits()));'),
    ('return StoredGrant(\n\t\trequested,', 'return StoredGrant(\n\t\tPermissionSet(requested.bits() | shown.bits()),'),
    # Timers.
    ('const auto low = repeat ? kMinInterval : kMinTimeout;', 'const auto low = kMinTimeout;'),
    ('return (ms >= low) // False for NaN as well.', 'return !(ms < low)'),
    # CPU budget.
    ('while (now - slices.front().first >= kCpuSpanUs) {', 'while (false) {'),
    ('while (now - slices.front().first >= kCpuSpanUs) {', 'while (now - slices.front().first >= 1\'000\'000) {'),
    ('|| (used > kCpuSpanBudgetUs);', '|| (used >= kCpuSpanBudgetUs);'),
    ('used += charge;', 'used = charge;'),
    ('used -= slices.front().second;', '(void)0;'),
    # Without the minimum charge a flood of trivial entries runs free.
    ('minimum ? kMinChargeUs : std::int64_t(0));', 'std::int64_t(0));'),
    ('minimum ? kMinChargeUs : std::int64_t(0));', 'kMinChargeUs);'),
    ('minimum ? kMinChargeUs : std::int64_t(0));', 'minimum ? kMinChargeUs : elapsed);'),
    ('while (!overruns.empty() && now - overruns.front() >= kOverrunSpanUs) {', 'while (false) {'),
    ('kMaxTimers = 64;', 'kMaxTimers = 1000;'),
    ('kMinInterval = 50.;', 'kMinInterval = 10.;'),
    # Gestures, toasts, the event queue.
    ('return now < until;', 'return now <= until;'),
    ('return !last || now - last >= kToastIntervalMs;', 'return now - last >= kToastIntervalMs;'),
    ('return !last || now - last >= kToastIntervalMs;', 'return !last || now - last > kToastIntervalMs;'),
    ('\t\t\tevents.pop_front();\n', ''),
    ('\t\t\t++dropped;\n', ''),
    ('\t\treturn first;', '\t\treturn true;'),
    ('\t\tdropped = 0;\n\t\treturn std::exchange', '\t\treturn std::exchange'),
    ('\t\t|| event == "messageOut"\n', ''),
    ('\t\t|| event == "unload";', ';'),
    # Fetch addresses.
    ('return (a != 0)', 'return true'),
    ('&& (a != 10)', '&& true'),
    ('&& !(a == 100 && (b & 0xC0) == 64)', '&& !(a == 100 && (b & 0xC0) == 0)'),
    ('&& (a != 127)', '&& true'),
    ('&& !(a == 169 && b == 254)', '&& true'),
    ('&& !(a == 172 && (b & 0xF0) == 16)', '&& !(a == 172 && b == 16)'),
    ('&& !(a == 192 && b == 168)', '&& true'),
    ('&& (a < 224);', '&& (a < 255);'),
    ('return embedded(); // IPv4-mapped.', 'return true;'),
    ('fail as 0.0.0.0/8.\n\t\treturn embedded();', '\n\t\treturn true;'),
    ('return embedded(); // NAT64 64:ff9b::/96.', 'return true;'),
    ('\t\t&& ipv6[4] == 0x00 && ipv6[5] == 0x01) {', '\t\t&& ipv6[4] == 0x00 && ipv6[5] == 0x02) {'),
    ('return ((ipv6[0] & 0xFE) != 0xFC)', 'return (ipv6[0] != 0xFC)'),
    ('&& !(ipv6[0] == 0xFE && (ipv6[1] & 0xC0) == 0x80)', '&& true'),
    ('&& !(ipv6[0] == 0xFE && (ipv6[1] & 0xC0) == 0xC0)', '&& true'),
    ('&& (ipv6[0] != 0xFF); // Multicast.', ';'),
    ('return (c >= \'A\' && c <= \'Z\') ? char(c - \'A\' + \'a\') : c;', 'return c;'),
    ('|| (lower == "host")', ''),
    ('|| lower.starts_with("proxy-");', ';'),
    # Hotkeys.
    ('if (key == kKeyEscape || key == kKeyReturn || key == kKeyEnter) {', 'if (key == kKeyEscape || key == kKeyReturn) {'),
    ('((modifiers & ~std::uint32_t(kShiftModifier))\n\t\t\t== std::uint32_t(kControlModifier));',
     '(modifiers & std::uint32_t(kControlModifier));'),
    ('&& std::string_view("CVXAZY")', '&& std::string_view("CVXA")'),
    # In flight.
    ('return inFlight < kMaxInFlight;', 'return inFlight <= kMaxInFlight;'),
    # Truncation.
    ('\t\t--size; // text[size] continues a character: cut before its lead.', '\t\tbreak;'),
    ('auto size = kMaxLogText - kMark.size();', 'auto size = kMaxLogText;'),
)


def build_core(root, read):
    tree = root / 'plugin_host'
    objects = []
    for unit in CORE:
        for ext in ('.h', '.cpp'):
            path = tree / 'luxury/plugins/core' / (unit + ext)
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(read(PLUGINS + 'core/' + unit + ext), encoding='utf-8', newline='\n')
        obj = tree / (unit + '.o')
        run([CXX, '-std=c++20', '-O0', '-c', *flags(tree), str(tree / 'luxury/plugins/core' / (unit + '.cpp')), '-o', str(obj)], root)
        objects.append(str(obj))
    return tree, objects


def flags(tree):
    # The copied tree comes first: core files are read through `read`.
    return ['-Wall', '-Wextra', '-Werror', '-I', str(tree), '-I', str(ROOT / SRC)]


def fixture(root, tree, objects, logic, name):
    (tree / 'luxury/plugins/plugin_host_logic.h').write_text(logic, encoding='utf-8', newline='\n')
    main = root / (name + '.cpp')
    main.write_text(MAIN, encoding='utf-8')
    exe = root / (name + ('.exe' if os.name == 'nt' else ''))
    run([CXX, '-std=c++20', '-O0', *flags(tree), str(main), *objects, *STATIC, '-o', str(exe)], root)
    standalone(exe)
    work = root / (name + '_fs')
    work.mkdir()
    result = subprocess.run([str(exe), str(work)], cwd=root, capture_output=True, text=True, errors='replace')
    return result.returncode, result.stdout + result.stderr


# --- Static checks of the Qt half.

def body(text, signature):
    return function(text, signature)


def check_hooks(files):
    text = files[HOOKS]
    active = body(text, '[[nodiscard]] Manager *Active() {')
    assert 'Manager::Maybe()' in active and 'manager->active()' in active, 'Active() must check Maybe() and active()'
    for signature in (
            'bool BeforeSend(Api::MessageToSend &message) {',
            'bool HasCommands() {',
            'bool LocalCommand(not_null<History*> history, const QString &text) {',
            'void EmitMessage(not_null<HistoryItem*> item, const std::string &event) {',
            'void EmitUpdate(not_null<Main::Session*> session, const Object &updates) {',
            'void FillMessageMenu(not_null<Ui::PopupMenu*> menu, HistoryItem *item) {',
            'bool HandleHotkey(not_null<QObject*> object, not_null<QKeyEvent*> event) {'):
        first = body(text, signature).split('\n')[1].strip()
        assert first == 'const auto manager = Active();', f'{signature} must start with the Active() guard'
    # The service chat with the login codes never reaches a plugin.
    for signature in ('void EmitMessage(not_null<HistoryItem*> item, const std::string &event) {',
                      'void FillMessageMenu(not_null<Ui::PopupMenu*> menu, HistoryItem *item) {'):
        assert '|| (item->history()->peer->id == PeerData::kServiceNotificationsId)' in body(text, signature), \
            f'{signature} must skip the service chat'
    emit = body(text, 'void EmitUpdate(not_null<Main::Session*> session, const Object &updates) {')
    assert emit.index('if (!StripServiceUser(update)) {') < emit.index('emitLater'), 'updates must drop the service chat'
    assert emit.index('HideOwnPhone(update);') < emit.index('emitLater'), 'updates must hide the own phone'
    # Every UpdatesReceived overload is the guarded EmitUpdate and nothing else.
    overloads = re.findall(r'\nvoid UpdatesReceived\(\n(?:\t\t.*\n)*?\t\t.*\) \{\n(.*?)\n\}', text)
    assert len(overloads) == 3, 'UpdatesReceived must cover updates, difference and channel difference'
    for inner in overloads:
        assert re.fullmatch(r'\tEmitUpdate\(session, \w+\);', inner), 'UpdatesReceived must only call EmitUpdate'
    updates = body(text, 'void EmitUpdate(not_null<Main::Session*> session, const Object &updates) {')
    # Serialization only when a plugin subscribed to 'update'.
    assert updates.index('wanting.empty()') < updates.index('updates.write(buffer)'), 'updates serialized without a subscriber'
    send = body(text, 'bool BeforeSend(Api::MessageToSend &message) {')
    assert 'granted.has(Permission::MessagesModify)' in send, 'beforeSend without messages.modify'
    assert 'if (fold.cancelled) {\n\t\treturn false;' in send, 'a cancel must stop the send'
    assert 'TextWithTags{\n\t\t\tQString::fromStdString(fold.text),\n\t\t}' in send, 'a rewrite must drop the old entities'
    command = body(text, 'bool LocalCommand(not_null<History*> history, const QString &text) {')
    # Only a /name some plugin registered is consumed; bots get the rest.
    assert 'if (command.title == name) {' in command and command.rstrip('}').rstrip().endswith('return false;'), \
        'an unregistered /command must reach the chat'
    menu = body(text, 'void FillMessageMenu(not_null<Ui::PopupMenu*> menu, HistoryItem *item) {')
    assert 'Permission::MessagesRead' in menu and '? MessageJson(message)' in menu, 'menu message without messages.read gate'
    assert 'strong->data().message(itemId)' in menu, 'menu handler must look the message up again'
    hotkey = body(text, 'bool HandleHotkey(not_null<QObject*> object, not_null<QKeyEvent*> event) {')
    assert 'object.get() != receiver' in hotkey, 'hotkey must be decided once, for the receiver'
    # No plugin key while a text field has focus, and never a reserved one.
    assert 'if (typing) {\n\t\treturn false;\n\t}' in hotkey, 'hotkeys must not fire while typing'
    assert 'if (HotkeyReserved(keys)) {\n\t\treturn false;' in hotkey, 'hotkeys must skip reserved keys'
    assert hotkey.index('if (typing)') < hotkey.index('for (const auto &instance'), 'typing must be checked first'
    assert 'granted.has(Permission::Ui)' in hotkey, 'hotkeys without ui'
    # The Qt-free key constants of plugin_host_logic.h are Qt's.
    for line in ('kKeyEscape == Qt::Key_Escape', 'kKeyReturn == Qt::Key_Return', 'kKeyEnter == Qt::Key_Enter',
                 'kShiftModifier == int(Qt::ShiftModifier)', 'kControlModifier == int(Qt::ControlModifier)',
                 'kModifierMask == unsigned(Qt::KeyboardModifierMask)'):
        assert f'static_assert({line});' in text, f'{line} is not checked against Qt'
    message = body(text, '[[nodiscard]] json MessageJson(not_null<HistoryItem*> item) {')
    for key in ('"id", std::to_string(item->id.bare)', '"peer"', '"from"', '"date"', '"text"', '"out"', '"replyTo"', '"hasMedia"'):
        assert key in message, f'message JSON lacks {key}'
    peer = body(text, '[[nodiscard]] json PeerJson(not_null<PeerData*> peer) {')
    assert 'std::to_string(getDialogIdFromPeer(peer))' in peer, 'peer id must be the decimal dialog id'


def check_names(files):
    # Finding 1: a type of plugin_host_logic.h named like a hook function of
    # plugin_hooks.h (one namespace) does not compile where both are seen.
    types = set(re.findall(r'^struct (\w+) \{', files[LOGIC], re.M))
    functions = set(re.findall(r'^(?:\[\[nodiscard\]\] )?\w+ (\w+)\(', files[HOOKS_H], re.M))
    assert types and functions, 'no names found'
    clash = types & functions
    assert not clash, f'plugin_host_logic.h types clash with hook functions: {clash}'


ENGINE_ENTRIES = ('loadMain', 'emit', 'callFunction', 'resolve', 'reject', 'fireTimer')


def check_api_call(files):
    host = files[HOST]
    call = body(host, 'Js::HostResult Instance::apiCall(const json &args, std::uint64_t promiseId) {')
    encode = call.index('Tl::EncodeMethod(')
    assert call.index('IsMethodForbidden(method)') < encode, 'forbidden methods must be refused before encoding'
    assert call.index('HasQueryParam(*combinator)') < encode, 'query-wrapping methods must be refused before encoding'
    assert call.index('HasPaidParam(params)') < encode, 'Stars-spending params must be refused before encoding'
    assert call.index('MentionsServiceUser(params)') < encode, 'the service chat must be refused before encoding'
    assert 'if (decoded && !StripServiceUser(result)) {' in call, 'results must drop the service chat'
    assert call.index('HideOwnPhone(result);') < call.index('strong->settleLater(promiseId, true, std::move(result));'), \
        'results must hide the own phone'
    assert 'Gunzip' in call, 'results are not inflated with the capped Gunzip'
    gunzip = body(host, 'std::optional<std::string> Gunzip(std::string_view packed) {')
    assert 'result.size() > kInflateLimit' in gunzip, 'Gunzip must stop at kInflateLimit'
    assert 'invalidate_weak_ptrs(this);' in body(host, 'Instance::~Instance() {'), 'callbacks may outlive the instance'
    # CPU budget: every entry into JS is timed by run().
    timed = body(host, 'Js::Outcome Instance::run(Fn<Js::Outcome()> entry, bool minimum) {')
    assert '\t\t&& _budget.record(\n\t\t\tfinished,\n\t\t\tfinished - started,\n\t\t\toutcome.overrun,\n\t\t\tminimum)) {' in timed \
        and 'instanceStop' in timed, \
        'run() must stop a plugin over its CPU budget'
    for match in re.finditer(r'_engine->(\w+)\(', host):
        if match.group(1) == 'releaseFunction':
            continue
        assert match.group(1) in ENGINE_ENTRIES, f'unknown engine entry {match.group(1)}'
        before = host[max(0, match.start() - 120):match.start()]
        assert re.search(r'run\(\[&\] \{\s*return(?:\s*fulfill\s*\?\s*|\s*fulfill\s*\?\s*_engine->resolve\(promiseId, value\)\s*:\s*|\s*)$', before), \
            f'_engine->{match.group(1)} runs outside run()'
    assert 'constexpr auto kStack = std::size_t(256) * 1024;' in host and 'limits.stack = kStack;' in host, \
        'the JS stack must be 256 KiB'
    timer = body(host, 'Js::HostResult Instance::timerSet(const json &options) {')
    assert 'const auto ms = TimerDelay(delay, repeat);' in timer and 'timer->setInterval(ms);' in timer, \
        'timers must be clamped'
    # At most kMaxInFlight requests, counted until they settle.
    for signature in ('Js::HostResult Instance::fetch(const json &args, std::uint64_t promiseId) {',
                      'Js::HostResult Instance::apiCall(const json &args, std::uint64_t promiseId) {',
                      'Js::HostResult Instance::send(const json &options, std::uint64_t promiseId) {'):
        request = body(host, signature)
        assert request.index('if (!InFlightAllowed(int(_requests.size()))) {') < request.index('_requests.emplace(promiseId);'), \
            f'{signature} must check the in-flight cap first'
    assert '_requests.erase(promiseId);' in body(host, 'void Instance::settleLater('), 'a settled request must free its slot'
    send = body(host, 'Js::HostResult Instance::send(const json &options, std::uint64_t promiseId) {')
    assert 'const auto sent = session->api().sendMessage(std::move(message));' in send \
        and 'strong->settleLater(\n\t\t\t\tpromiseId,\n\t\t\t\tsent,' in send, \
        'messages.send must reject when a plugin cancelled the message'
    step = body(host, 'void Instance::fetchStep(')
    assert 'parsed.port(),' in step, 'fetch must pass the URL port to FetchAllowed'
    calls = body(host, 'Js::HostResult Instance::call(')
    watch = calls[calls.index('"settings.onChange"'):]
    assert watch.index('_settingWatchers.size() >= std::size_t(kMaxSettingWatchers)') < watch.index('_settingWatchers.push_back(*fn);'), \
        'settings.onChange must be capped'
    unload = body(host, 'void Instance::unload() {')
    assert 'for (const auto fn : base::take(_settingWatchers)) {\n\t\t_engine->releaseFunction(fn);' in unload, \
        'unload must release the setting watchers'
    handler = body(host, 'Js::HostResult Instance::addHandler(')
    assert 'if (HotkeyReserved(handler.keys)) {' in handler, 'addHotkey must refuse reserved keys'
    assert 'if (!stored) {\n\t\t\t_engine->releaseFunction(*fn);' in handler, 'a rejected handler must be released'
    assert handler.rstrip('}').rstrip().endswith('stored = true;\n\treturn Result::Value(nullptr);'), \
        'only a stored handler may keep its function'
    assert handler.count('stored = true;') == 1


def check_sandbox(files):
    host, hooks = files[HOST], files[HOOKS]
    # The CPU budget counts microseconds of a monotonic clock.
    timed = body(host, 'Js::Outcome Instance::run(Fn<Js::Outcome()> entry, bool minimum) {')
    assert 'std::chrono::steady_clock::now()' in timed and 'std::chrono::microseconds' in timed, \
        'run() must time entries in steady microseconds'
    # Events: one bounded queue, drained in one pass.
    emit = body(host, 'void Instance::emitLater(std::string event, json payload) {')
    assert 'if (_stopping || !_queue.push(std::move(event), std::move(payload))) {\n\t\treturn;' in emit, \
        'emitLater must queue and schedule one drain'
    assert 'const auto events = _queue.take();' in emit and 'for (const auto &entry : events) {' in emit, \
        'the drain must take the whole queue'
    assert 'auto first = true;' in emit and '}, std::exchange(first, false));' in emit, \
        'a drain must pay the minimum CPU charge once, on its first event'
    # Clipboard: only right after a command, menu item or hotkey.
    later = body(host, 'void Instance::callLater(std::int64_t fn, json args, bool gesture) {')
    assert 'if (gesture) {\n\t\t\t\t_gestureUntil = crl::now() + kGestureMs;' in later, 'callLater must open the gesture window'
    for site in ('\t\t\t\t}, true);\n\t\t\t\treturn true;', '} } }, true);', 'instance->callLater(hotkey.fn, nullptr, true);'):
        assert site in hooks, f'a user action does not pass the gesture: {site}'
    assert hooks.count('callLater(') == 3, 'every callLater site must decide the gesture'
    calls = body(host, 'Js::HostResult Instance::call(')
    gate = calls.index('&& !GestureActive(crl::now(), _gestureUntil)) {')
    assert calls.index('function.substr(0, 10) == "clipboard."') < gate < calls.index('"clipboard.read"'), \
        'the clipboard must be gated by a gesture'
    assert '{ "granted", _plugin.granted.ids() },' in calls and '{ "lang", Lang::LanguageIdOrDefault(' in calls, \
        'plugin.info lacks granted or lang'
    subscribe = body(host, 'Js::HostResult Instance::subscribe(')
    assert 'if (delta > 0 && !KnownEvent(event)) {\n\t\treturn TypeError(' in subscribe, 'unknown events must throw'
    # Dialogs: not over the passcode, one at a time, named, rate-limited toasts.
    ui = body(host, 'Js::HostResult Instance::ui(')
    locked = ui.index('Core::App().passcodeLocked()')
    assert ui.index('"ui.add"') < locked < ui.index('function == "ui.toast"'), 'ui must refuse while locked'
    assert 'if (!ToastAllowed(now, _lastToast)) {' in ui and '_lastToast = now;' in ui, 'toasts must be rate-limited'
    assert '} else if (_dialog) {\n\t\treturn Result::Error(' in ui, 'one dialog per plugin'
    assert ui.count('_dialog = box.data();') == 2 and 'strong->_dialog = nullptr;' in ui, 'dialogs must be tracked'
    assert 'const auto title = custom.isEmpty() ? name : (name + u": "_q + custom);' in ui \
        and '.title = v::text::data(title),' in ui and 'box->setTitle(rpl::single(title));' in ui, \
        'dialog titles must name the plugin'
    for signature in ('void Instance::unload() {', 'Instance::~Instance() {'):
        assert 'if (const auto dialog = _dialog.get()) {\n\t\tdialog->closeBox();' in body(host, signature), \
            f'{signature} must close the dialog'
    # Account switch or logout: pending promises reject, late replies drop.
    assert '.activeSessionChanges(\n\t\t) | rpl::on_next([=](Main::Session *) {\n\t\t\taccountChanged();' \
        in body(host, 'bool Instance::start() {'), 'start() must follow the active account'
    changed = body(host, 'void Instance::accountChanged() {')
    assert '_queue.clear();' in changed and 'for (const auto promiseId : base::take(_requests)) {' in changed, \
        'accountChanged must reject the pending requests'
    send = body(host, 'Js::HostResult Instance::send(const json &options, std::uint64_t promiseId) {')
    assert send.index('if (!alive || alive->_stopping || !alive->_requests.contains(promiseId)) {') \
        < send.index('session->api().sendMessage('), 'a queued send must check the instance first'
    assert send.index('if (IsServiceUserId(options.value("peer", json()))) {') < send.index('_requests.emplace'), \
        'messages.send must refuse the service chat'
    assert send.index('ResolvePeer(session, *dialogId)') < send.index('} else if (!peer) {') \
        < send.index('_requests.emplace'), 'messages.send must refuse a peer it did not resolve'
    # The resolved peer is checked, so no 48-bit alias reaches 777000.
    resolve = body(host, '[[nodiscard]] PeerData *ResolvePeer(')
    assert resolve.index('if (!IsBareDialogId(dialogId)) {\n\t\treturn nullptr;') < resolve.index('getBareDialogId('), \
        'ResolvePeer must refuse ids over 48 bits'
    assert 'return (peer && peer->id == PeerData::kServiceNotificationsId)\n\t\t? nullptr\n\t\t: peer;' in resolve, \
        'ResolvePeer must refuse the service chat after resolving'
    call = body(host, 'Js::HostResult Instance::apiCall(const json &args, std::uint64_t promiseId) {')
    assert 'if (!strong || !strong->_requests.contains(promiseId)) {\n\t\t\treturn true;' in call \
        and 'if (strong && strong->_requests.contains(promiseId)) {' in call, 'api.call replies must check the request'
    # Fetch: public addresses only, credentials stay on their origin.
    fetch = body(host, 'Js::HostResult Instance::fetch(const json &args, std::uint64_t promiseId) {')
    assert '.toLower() == "host") {\n\t\t\t\tcontinue;' in fetch, 'a plugin Host header must be dropped'
    step = body(host, 'void Instance::fetchStep(')
    literal = step.index('if (isLiteral || proxied) {\n\t\tif (isLiteral && !IsPublic(literal)) {\n\t\t\treturn refuse(this);')
    assert literal < step.index('QHostInfo::lookupHost('), 'literal addresses must be checked'
    # Behind an application proxy a local lookup would leak the name.
    assert 'const auto proxied = (QNetworkProxy::applicationProxy().type()\n\t\t!= QNetworkProxy::NoProxy);' in step, \
        'fetch must skip the local lookup behind an application proxy'
    looked = step[step.index('QHostInfo::lookupHost('):]
    assert '|| !strong->_requests.contains(promiseId)) {' in looked, 'a lookup must check the request'
    assert looked.index('for (const auto &address : addresses) {\n\t\t\tif (!IsPublic(address)) {\n\t\t\t\treturn refuse(strong);') \
        < looked.index('strong->fetchSend('), 'every resolved address must be public'
    assert step.count('fetchSend(') == 2 and '_network->' not in step, 'fetchStep must only send through fetchSend'
    sent = body(host, 'void Instance::fetchSend(')
    assert '|| !strong->_requests.contains(promiseId)) {' in sent, 'a reply must check the request'
    assert 'if (!SameOrigin(reply->url(), next)) {' in sent \
        and 'return StripOnCrossOrigin(header.first.toStdString());' in sent and 'std::move(kept),' in sent, \
        'cross-origin redirects must drop credentials'
    assert 'if (binary) {\n\t\t\tresult["base64"] = bytes.toBase64().toStdString();' in sent, 'binary bodies come as base64'
    same = body(host, '[[nodiscard]] bool SameOrigin(const QUrl &a, const QUrl &b) {')
    assert '(a.host().toLower() == b.host().toLower())' in same and '(port(a) == port(b))' in same \
        and '(a.scheme().toLower() == b.scheme().toLower())' in same, 'SameOrigin must compare scheme, host and port'


def check_commit(files):
    manager = files[MANAGER]
    assert 'return Root() + u"plugins/"_q + id;' in body(manager, '[[nodiscard]] QString PluginRoot(const QString &id) {'), \
        'plugin directories must live under plugins/'
    assert 'std::filesystem::path(std::u8string(' in body(manager, '[[nodiscard]] std::filesystem::path FsPath('), \
        'FsPath must convert from UTF-8'
    append = body(manager, 'void Manager::Private::append(')
    assert '.text = Text(TruncateText(text.toStdString())),' in append, 'log lines must be truncated'
    error = body(manager, 'void Manager::Private::setError(const QString &id, const QString &text) {')
    assert 'TruncateText(text.toStdString())' in error and 'fireChangesLater();' in error, 'lastError must be truncated and throttled'
    assert 'save()' not in error and 'changes.fire' not in error, 'lastError must not rewrite the registry'
    later = body(manager, 'void Manager::Private::fireChangesLater() {')
    assert 'if (changesQueued) {\n\t\treturn;' in later and 'base::call_delayed(kChangesDelay' in later, 'changes() must be throttled'
    assert 'plugin->lastError = TruncateText(text.toStdString());' in body(manager, 'void Manager::Private::instanceStop('), \
        'the stop reason must be truncated'
    commit = body(manager, 'void Manager::commitInstall(')
    consent = commit.index('if (NeedsConsent(plan.update, newPermissions, widened, plan.consented)) {')
    assert consent < commit.index('committing.emplace(id);'), 'consent must be checked before anything starts'
    assert commit.index('MissingPermissions(') < consent and commit.index('NetworkWidened(') < consent, \
        'newPermissions must be recomputed from the registry'
    assert 'const auto reviewed = plan.manifestBytes;' in commit, 'the reviewed bytes must come from the plan'
    raw = commit.index('} else if (raw.toStdString() != reviewed) {')
    assert raw < commit.index('installer.download('), 'the downloaded plugin.json must be the reviewed one'
    assert '? plan.shownPermissions\n\t\t: PermissionSet();' in commit and 'const auto shownHosts = plan.shownHosts;' in commit, \
        'the grant must rest on what the consent box showed'
    grant = commit.index('const auto grant = CommitGrant(')
    assert grant < commit.index('if (!grant) {') < commit.index('SwapDirectory('), 'the swap must follow the grant check'
    assert 'manifest.permissions,\n\t\t\t\tpreviously,\n\t\t\t\tshown,' in commit[grant:grant + 200], \
        'CommitGrant must get the plugin.json being installed'
    assert 'entry.granted = *grant;' in commit, 'the stored grant must be the checked one'
    swap = commit[commit.index('const auto swapped = [&]() -> std::string {'):commit.index('if (!swapped.empty()) {')]
    assert 'try {' in swap and 'catch (const std::exception &e) {\n\t\t\t\t\treturn e.what();' in swap, \
        'a filesystem error must become an install error'
    for name in ('PermissionTitle', 'PermissionDescription'):
        for path in (MANAGER, HOST, HOOKS, INSTALLER):
            assert not re.search(r'\b' + name + r'\s*\([^;]*\)\s*\{', files[path]), f'{name} is defined in {path}'
    download = body(files[INSTALLER], 'void Installer::downloadNext(std::shared_ptr<DownloadState> state) {')
    assert 'IsSafeRelativePath(path)' in download, 'plugin file paths must be checked'


# The UI calls these: every one the header declares must be defined.
MANAGER_API = ('Manager', '~Manager', 'Instance', 'Maybe', 'changes', 'plugins', 'plugin', 'logs',
               'clearLogs', 'setEnabled', 'remove', 'reload', 'settingValue', 'setSettingValue',
               'prepareInstall', 'prepareUpdate', 'commitInstall', 'checkUpdates', 'active',
               'messageMenuItems', 'commands', 'instances')


def check_manager_api(files):
    header = files[HEADER]
    cls = header[header.index('class Manager final {'):header.index('private:', header.index('class Manager final {'))]
    for name in MANAGER_API:
        pattern = re.escape(name) + r'\('
        assert re.search(r'(?<![\w~])' + pattern, cls), f'Manager::{name} is not declared'
        assert re.search(r'\bManager::' + pattern, files[MANAGER]), f'Manager::{name} is not defined'


def check_resource(files):
    assert '<file alias="luxury/plugins/api.tl">../../../SourceFiles/mtproto/scheme/api.tl</file>' in files[QRC], 'api.tl is not embedded'
    assert '<qresource prefix="/gui">' in files[QRC]
    assert (ROOT / 'Telegram/Resources/qrc/luxury/../../../SourceFiles/mtproto/scheme/api.tl').is_file()
    assert 'u":/gui/luxury/plugins/api.tl"_q' in files[HOST], 'ApiSchema reads another path'


def check_sites(files):
    send = body(files[APIWRAP], 'bool ApiWrap::sendMessage(\n\t\tMessageToSend &&message,')
    cancel = send.index('if (!Luxury::Plugins::BeforeSend(message)) {\n\t\treturn false;\n\t}')
    assert cancel < send.index('prependPseudoReply(message)'), 'beforeSend must run before anything reads the text'
    # false means only "a plugin cancelled": the composers keep the text then.
    assert send.count('return false;') == 1 and send.rstrip('}').rstrip().endswith('return true;'), \
        'sendMessage returns false only for a plugin cancel'
    updates = files[UPDATES]
    applied = body(updates, 'void Updates::applyUpdates(\n\t\tconst MTPUpdates &updates,')
    assert 'if (!seq || seq == _updatesSeq + 1) {\n\t\tLuxury::Plugins::UpdatesReceived(_session, updates);' in applied, \
        'pushed and RPC updates must reach plugins once per seq'
    assert 'Luxury::Plugins::UpdatesReceived(_session, result);' in body(updates, 'void Updates::differenceDone('), \
        'getDifference results must reach plugins'
    assert 'Luxury::Plugins::UpdatesReceived(_session, difference);' in body(updates, 'void Updates::channelDifferenceDone('), \
        'channel differences must reach plugins'
    assert 'Luxury::Plugins::' not in body(updates, 'void Updates::mtpUpdateReceived(const MTPUpdates &updates) {'), \
        'mtpUpdateReceived misses RPC results and would double them'
    assert updates.count('Luxury::Plugins::UpdatesReceived(') == 3
    assert 'local->setRealId(newId);\n\t\t\t\t\t\tLuxury::Plugins::MessageSent(local);' in updates, \
        'messageOut must fire once the message got its server id'
    assert ('if (type == NewMessageType::Unread) {\n\t\tCheckForSwitchInlineButton(result);\n'
            '\t\tLuxury::Plugins::IncomingMessage(result);\n\t}') in files[SESSION], 'incoming hook outside Unread'
    widget = body(files[WIDGET], 'void HistoryWidget::send(Api::SendOptions options) {')
    command = widget.index('Luxury::Plugins::LocalCommand(')
    assert widget.index('Luxury::Plugins::HasCommands()') < command < widget.index('_field->getTextWithTags().text', command), \
        'the field text must not be read unless a plugin registered a command'
    assert command < widget.index('sendTextWithTags('), 'local commands must be caught before sending'
    assert 'clearFieldText();\n\t\treturn;' in widget[command:command + 300], 'a command must clear the field'
    chat = body(files[CHAT], 'void ChatWidget::send(Api::SendOptions options) {')
    command = chat.index('Luxury::Plugins::LocalCommand(')
    assert chat.index('Luxury::Plugins::HasCommands()') < command < chat.index('sendTextWithTags('), \
        'topic and reply composers must run local commands too'
    assert '_composeControls->clear();\n\t\treturn;' in chat[command:command + 300], 'a command must clear the field'
    for path, signature, clear in (
            (WIDGET, 'void HistoryWidget::sendTextWithTags(\n\t\tTextWithTags textWithTags,', 'clearFieldText();'),
            (CHAT, 'void ChatWidget::sendTextWithTags(\n\t\tTextWithTags textWithTags,', '_composeControls->clear(')):
        composer = body(files[path], signature)
        sent = composer.index('const auto sent = session().api().sendMessage(')
        kept = composer.index('if (!sent) {\n\t\t// A plugin cancelled it: the text stays in the field.\n\t\treturn;')
        assert sent < kept < composer.index(clear), f'{path}: a cancelled send must keep the text'
        # Registered only for a real send: a cancel would leave it pending forever.
        assert kept < composer.index('sendingAnimation().appendSending(') < composer.index(clear), \
            f'{path}: the send animation must start after a successful send'
    for path, signature, guard in (
            (SCHEDULED, 'void ScheduledWidget::send(Api::SendOptions options) {',
             'if (!session().api().sendMessage(std::move(message))) {\n\t\t// A plugin cancelled it: the text stays in the field.\n\t\treturn;'),
            (SHORTCUTS, 'void ShortcutMessages::send(Api::SendOptions options) {',
             'if (!_session->api().sendMessage(std::move(message))) {\n\t\t// A plugin cancelled it: the text stays in the field.\n\t\treturn;'),
            (STORIES, 'bool ReplyArea::send(',
             'if (!session().api().sendMessage(std::move(message))) {\n\t\t// A plugin cancelled it: the text stays in the field.\n\t\treturn false;')):
        composer = body(files[path], signature)
        assert composer.count('sendMessage(') == 1 and guard in composer \
            and composer.index(guard) < composer.index('->clear()'), f'{path}: a cancelled send must keep the text'
    link = files[SHARE]
    assert 'sent |= api.sendMessage(std::move(message));' in link \
        and link.index('sent |= api.sendMessage') < link.index('if (!sent) {', link.index('sent |= api.sendMessage')) \
        < link.index('show->showToast(tr::lng_share_done(tr::now));'), 'the link share must not claim a cancelled send'
    sync = body(files[SYNC], 'bool sendMessageSync(')
    assert 'return strong->api().sendMessage(std::move(message));' in sync, \
        'a cancelled sync send must fail instead of waiting for a local id'
    docs = files[DOCS]
    assert "telegram.updates, raw TL JSON,\n//                                           at-least-once: may repeat" in docs \
        and '| `update` event per update, at-least-once' in docs, 'the update event must be documented as at-least-once'
    assert '`account.updateConnectedBot`' in docs and '(`HasPaidParam`)' in docs, 'the forbidden list is out of date'
    app = files[APP]
    assert 'startDomain();\n\t_private->plugins = std::make_unique<Luxury::Plugins::Manager>();' in app, \
        'the manager starts after accounts load'
    destructor = body(app, 'Application::~Application() {')
    assert destructor.index('_private->plugins = nullptr;') < destructor.index('_domain->finish();'), \
        'plugins must unload before sessions die'
    keys = body(app, 'bool Application::eventFilter(QObject *object, QEvent *e) {')
    assert keys.index('HandleSpacePlayPause(') < keys.index('Luxury::Plugins::HandleHotkey(object, event)'), \
        'plugin hotkeys must only see keys the client did not handle'
    assert 'Luxury::Plugins::FillMessageMenu(_menu, item);' in files[INNER], 'history menu lacks the plugin submenu'
    assert 'Luxury::Plugins::FillMessageMenu(menu, request.item);' in files[VIEW_MENU], 'view menu lacks the plugin submenu'


CANCEL = '\tif (!sent) {\n\t\t// A plugin cancelled it: the text stays in the field.\n\t\treturn;\n\t} else if (animate) {'
CHECKS = (check_hooks, check_names, check_api_call, check_sandbox, check_commit, check_manager_api, check_resource, check_sites)
MUTANTS = (
    (check_hooks, HOOKS, 'return (manager && manager->active()) ? manager : nullptr;', 'return manager;'),
    (check_hooks, HOOKS, 'bool LocalCommand(not_null<History*> history, const QString &text) {\n\tconst auto manager = Active();',
     'bool LocalCommand(not_null<History*> history, const QString &text) {\n\tconst auto manager = Manager::Maybe();'),
    (check_hooks, HOOKS, 'bool HasCommands() {\n\tconst auto manager = Active();', 'bool HasCommands() {\n\tconst auto manager = Manager::Maybe();'),
    (check_hooks, HOOKS, '\tif (wanting.empty()) {\n\t\treturn;\n\t}\n', ''),
    (check_hooks, HOOKS, '\t\t|| item->isService()\n\t\t|| (item->history()->peer->id == PeerData::kServiceNotificationsId)\n',
     '\t\t|| item->isService()\n'),
    (check_hooks, HOOKS, '\t\t|| !item->isRegular()\n\t\t|| (item->history()->peer->id == PeerData::kServiceNotificationsId)\n'
     '\t\t|| !IsActive(&item->history()->session())) {\n\t\treturn;\n\t}\n\tconst auto items',
     '\t\t|| !item->isRegular()\n\t\t|| !IsActive(&item->history()->session())) {\n\t\treturn;\n\t}\n\tconst auto items'),
    (check_hooks, HOOKS, '\tif (!StripServiceUser(update)) {', '\tif (false) {'),
    (check_hooks, HOOKS, '\tHideOwnPhone(update);\n', ''),
    (check_api_call, HOST, '\t\t\tHideOwnPhone(result);\n', ''),
    (check_hooks, HOOKS, '\tEmitUpdate(session, difference);\n}\n\nvoid FillMessageMenu(', '}\n\nvoid FillMessageMenu('),
    (check_hooks, HOOKS, 'if (instance->plugin().granted.has(Permission::MessagesModify)) {', 'if (true) {'),
    (check_hooks, HOOKS, 'if (command.title == name) {', 'if (true) {'),
    (check_hooks, HOOKS, '\tif (typing) {\n\t\treturn false;\n\t}\n', ''),
    (check_hooks, HOOKS, 'if (HotkeyReserved(keys)) {', 'if (false) {'),
    (check_hooks, HOOKS, 'static_assert(kKeyEnter == Qt::Key_Enter);\n', ''),
    (check_hooks, HOOKS, 'const auto full = instance->plugin().granted.has(\n\t\t\t\tPermission::MessagesRead);', 'const auto full = true;'),
    (check_names, LOGIC, 'struct ParsedCommand {', 'struct LocalCommand {'),
    (check_api_call, HOST, 'if (IsMethodForbidden(method)) {', 'if (false) {'),
    (check_api_call, HOST, '} else if (HasQueryParam(*combinator)) {', '} else if (false) {'),
    (check_api_call, HOST, '\tinvalidate_weak_ptrs(this);\n\t_stopping = true;', '\t_stopping = true;'),
    (check_api_call, HOST, '&& _budget.record(\n\t\t\tfinished,', '&& false && _budget.record(\n\t\t\tfinished,'),
    (check_api_call, HOST, '\t\t\toutcome.overrun,\n\t\t\tminimum)) {', '\t\t\toutcome.overrun)) {'),
    (check_api_call, HOST, 'const auto out = run([&] {\n\t\t\treturn _engine->fireTimer(id);',
     'const auto out = ([&] {\n\t\t\treturn _engine->fireTimer(id);'),
    (check_api_call, HOST, 'const auto out = run([&] { return _engine->emit("beforeSend", payload); });',
     'const auto out = _engine->emit("beforeSend", payload);'),
    (check_api_call, HOST, 'constexpr auto kStack = std::size_t(256) * 1024;', 'constexpr auto kStack = std::size_t(1024) * 1024;'),
    (check_api_call, HOST, 'const auto ms = TimerDelay(delay, repeat);', 'const auto ms = delay.is_number() ? delay.get<int>() : 0;'),
    (check_api_call, HOST, '\tif (!InFlightAllowed(int(_requests.size()))) {\n\t\treturn TooManyRequests();\n\t} else if (!url.is_string()) {',
     '\tif (!url.is_string()) {'),
    (check_api_call, HOST, '\tif (!InFlightAllowed(int(_requests.size()))) {\n\t\treturn TooManyRequests();\n\t} else if (!session) {'
     '\n\t\treturn Result::Error("Error", "no active Telegram account");\n\t} else if (!name.is_string()) {',
     '\tif (!session) {\n\t\treturn Result::Error("Error", "no active Telegram account");\n\t} else if (!name.is_string()) {'),
    (check_api_call, HOST, '\tif (!InFlightAllowed(int(_requests.size()))) {\n\t\treturn TooManyRequests();\n\t} else if (!session) {'
     '\n\t\treturn Result::Error("Error", "no active Telegram account");\n\t} else if (!options.is_object()) {',
     '\tif (!session) {\n\t\treturn Result::Error("Error", "no active Telegram account");\n\t} else if (!options.is_object()) {'),
    (check_api_call, HOST, '\t_requests.emplace(promiseId);\n\t// Later: a send runs', '\t// Later: a send runs'),
    (check_api_call, HOST, '\t\t\t\tpromiseId,\n\t\t\t\tsent,', '\t\t\t\tpromiseId,\n\t\t\t\ttrue,'),
    (check_api_call, HOST, '} else if (HasPaidParam(params)) {', '} else if (false) {'),
    (check_api_call, HOST, '} else if (MentionsServiceUser(params)) {', '} else if (false) {'),
    (check_api_call, HOST, 'if (decoded && !StripServiceUser(result)) {', 'if (decoded && false) {'),
    (check_api_call, HOST, '\t_requests.erase(promiseId);\n', ''),
    (check_api_call, HOST, 'parsed.port(),', '-1,'),
    (check_api_call, HOST, 'if (_settingWatchers.size() >= std::size_t(kMaxSettingWatchers)) {', 'if (false) {'),
    (check_api_call, HOST, '\tfor (const auto fn : base::take(_settingWatchers)) {\n\t\t_engine->releaseFunction(fn);\n\t}\n', ''),
    (check_api_call, HOST, 'if (HotkeyReserved(handler.keys)) {', 'if (false) {'),
    (check_api_call, HOST, '\tstored = true;\n\treturn Result::Value(nullptr);', '\treturn Result::Value(nullptr);'),
    (check_api_call, HOST, 'if (!stored) {\n\t\t\t_engine->releaseFunction(*fn);', 'if (!stored) {\n\t\t\t(void)fn;'),
    (check_commit, MANAGER, 'return Root() + u"plugins/"_q + id;', 'return Root() + id;'),
    (check_commit, MANAGER, 'std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));', 'std::filesystem::path(utf8);'),
    (check_commit, MANAGER, '.text = Text(TruncateText(text.toStdString())),', '.text = text,'),
    (check_commit, MANAGER, '\t\tplugin->lastError = error;\n\t\tfireChangesLater();', '\t\tplugin->lastError = error;\n\t\tsave();'),
    (check_commit, MANAGER, '\tif (changesQueued) {\n\t\treturn;\n\t}\n', ''),
    (check_commit, MANAGER, 'if (NeedsConsent(plan.update, newPermissions, widened, plan.consented)) {',
     'if (NeedsConsent(plan.update, newPermissions, widened, true)) {'),
    (check_commit, MANAGER, '} else if (raw.toStdString() != reviewed) {', '} else if (false) {'),
    (check_commit, MANAGER, '? plan.shownPermissions', '? plan.manifest.permissions'),
    (check_commit, MANAGER, 'const auto shownHosts = plan.shownHosts;', 'const auto shownHosts = plan.manifest.network;'),
    (check_commit, MANAGER, 'if (!grant) {', 'if (false) {'),
    (check_commit, MANAGER, 'entry.granted = *grant;', 'entry.granted = manifest.permissions;'),
    (check_commit, MANAGER, 'return e.what(); // Path conversion or allocation.', 'throw;'),
    (check_commit, MANAGER, 'void Manager::remove(', 'QString PermissionTitle(Permission p) { return {}; }\nvoid Manager::remove('),
    (check_manager_api, MANAGER, 'void Manager::checkUpdates(', 'void Manager::checkForUpdates('),
    (check_resource, QRC, 'alias="luxury/plugins/api.tl"', 'alias="plugins/api.tl"'),
    (check_sites, APIWRAP, '\tif (!Luxury::Plugins::BeforeSend(message)) {\n\t\treturn false;\n\t}\n', ''),
    (check_sites, APIWRAP, '\tif (!Luxury::Plugins::BeforeSend(message)) {\n\t\treturn false;', '\tif (!Luxury::Plugins::BeforeSend(message)) {\n\t\treturn true;'),
    (check_sites, APIWRAP, '\tfinishForwarding(action);\n\treturn true;', '\tfinishForwarding(action);\n\treturn false;'),
    (check_sites, UPDATES, '\tif (!seq || seq == _updatesSeq + 1) {\n\t\tLuxury::Plugins::UpdatesReceived(_session, updates);\n\t}\n', ''),
    (check_sites, UPDATES, '\tLuxury::Plugins::UpdatesReceived(_session, result);\n', ''),
    (check_sites, UPDATES, '\tLuxury::Plugins::UpdatesReceived(_session, difference);\n', ''),
    (check_sites, UPDATES, 'void Updates::mtpUpdateReceived(const MTPUpdates &updates) {\n',
     'void Updates::mtpUpdateReceived(const MTPUpdates &updates) {\n\tLuxury::Plugins::UpdatesReceived(_session, updates);\n'),
    (check_sites, UPDATES, '\t\t\t\t\t\tLuxury::Plugins::MessageSent(local);\n', ''),
    (check_sites, SESSION, '\t\tLuxury::Plugins::IncomingMessage(result);\n', ''),
    (check_sites, WIDGET, '\t\t&& Luxury::Plugins::HasCommands()\n', ''),
    (check_sites, WIDGET, CANCEL, '	if (animate) {'),
    (check_sites, CHAT, CANCEL, '	if (animate) {'),
    (check_sites, CHAT, '\t} else if (Luxury::Plugins::HasCommands()\n', '\t} else if (false\n'),
    (check_sites, CHAT, '\t\t// A plugin\'s /command runs locally and is never sent.\n\t\t_composeControls->clear();\n', ''),
    (check_sites, WIDGET, '\tconst auto sent = session().api().sendMessage(',
     '\tcontroller()->sendingAnimation().appendSending({});\n\tconst auto sent = session().api().sendMessage('),
    (check_sites, CHAT, '\tconst auto sent = session().api().sendMessage(',
     '\tcontroller()->sendingAnimation().appendSending({});\n\tconst auto sent = session().api().sendMessage('),
    (check_sites, SCHEDULED, 'if (!session().api().sendMessage(std::move(message))) {', 'if (!session().api().sendMessage(std::move(message)) && false) {'),
    (check_sites, SHORTCUTS, '\tif (!_session->api().sendMessage(std::move(message))) {\n\t\t// A plugin cancelled it: the text stays in the field.\n\t\treturn;\n\t}\n',
     '\t_session->api().sendMessage(std::move(message));\n'),
    (check_sites, STORIES, '\t\t// A plugin cancelled it: the text stays in the field.\n\t\treturn false;', '\t\t// A plugin cancelled it: the text stays in the field.\n\t\treturn true;'),
    (check_sites, SHARE, '\t\tif (!sent) {\n\t\t\t// Plugins cancelled every copy', '\t\tif (false) {\n\t\t\t// Plugins cancelled every copy'),
    (check_sites, SYNC, '\t\treturn strong->api().sendMessage(std::move(message));',
     '\t\tstrong->api().sendMessage(std::move(message));\n\t\treturn true;'),
    (check_sites, DOCS, ',\n//                                           at-least-once: may repeat', ''),
    (check_sites, DOCS, '- business and bot grants: `account.updateConnectedBot`,', '- business and bot grants:'),
    (check_sites, APP, '\t// Plugins get \'unload\' while windows and sessions are still alive.\n\t_private->plugins = nullptr;\n', ''),
    (check_sites, INNER, '\t\tLuxury::Plugins::FillMessageMenu(_menu, item);\n', ''),
    # Sandbox hardening.
    (check_sandbox, HOST, 'std::chrono::steady_clock::now()', 'std::chrono::system_clock::now()'),
    (check_sandbox, HOST, 'std::chrono::microseconds', 'std::chrono::milliseconds'),
    (check_sandbox, HOST, 'if (_stopping || !_queue.push(std::move(event), std::move(payload))) {',
     'if (_stopping) {\n\t\treturn;\n\t}\n\tif (false) {'),
    (check_sandbox, HOST, 'const auto events = _queue.take();', 'const auto events = std::deque<std::pair<std::string, json>>{ _queue.events.front() };'),
    (check_sandbox, HOST, '\t\t\tif (gesture) {\n\t\t\t\t_gestureUntil = crl::now() + kGestureMs;', '\t\t\tif (true) {\n\t\t\t\t_gestureUntil = crl::now() + kGestureMs;'),
    (check_sandbox, HOOKS, 'instance->callLater(hotkey.fn, nullptr, true);', 'instance->callLater(hotkey.fn, nullptr);'),
    (check_sandbox, HOOKS, '} } }, true);', '} } });'),
    (check_sandbox, HOOKS, '\t\t\t\t}, true);\n\t\t\t\treturn true;', '\t\t\t\t});\n\t\t\t\treturn true;'),
    (check_sandbox, HOST, '&& !GestureActive(crl::now(), _gestureUntil)) {', '&& false) {'),
    (check_sandbox, HOST, '{ "granted", _plugin.granted.ids() },', ''),
    (check_sandbox, HOST, 'if (delta > 0 && !KnownEvent(event)) {', 'if (false) {'),
    (check_sandbox, HOST, 'Core::App().passcodeLocked()', 'false'),
    (check_sandbox, HOST, 'if (!ToastAllowed(now, _lastToast)) {', 'if (false) {'),
    (check_sandbox, HOST, '_lastToast = now;', ''),
    (check_sandbox, HOST, '} else if (_dialog) {', '} else if (false) {'),
    (check_sandbox, HOST, 'strong->_dialog = nullptr;', ''),
    (check_sandbox, HOST, 'const auto title = custom.isEmpty() ? name : (name + u": "_q + custom);', 'const auto title = custom;'),
    (check_sandbox, HOST, '\t_stopping = true;\n\t_queue.clear();\n\tif (const auto dialog = _dialog.get()) {\n\t\tdialog->closeBox();\n\t}\n',
     '\t_stopping = true;\n\t_queue.clear();\n'),
    (check_sandbox, HOST, '\t\t\taccountChanged();', '\t\t\t;'),
    (check_sandbox, HOST, 'for (const auto promiseId : base::take(_requests)) {', 'for (const auto promiseId : _requests) {'),
    (check_sandbox, HOST, 'if (!alive || alive->_stopping || !alive->_requests.contains(promiseId)) {', 'if (!alive) {'),
    (check_sandbox, HOST, 'if (IsServiceUserId(options.value("peer", json()))) {', 'if (false) {'),
    (check_sandbox, HOST, 'return (peer && peer->id == PeerData::kServiceNotificationsId)\n\t\t? nullptr\n\t\t: peer;', 'return peer;'),
    (check_sandbox, HOST, '\tif (!IsBareDialogId(dialogId)) {\n\t\treturn nullptr;\n\t}\n', ''),
    (check_sandbox, HOST, '} else if (!peer) {\n\t\treturn Result::Error("Error", "unknown peer");', '} else if (false) {\n\t\treturn Result::Error("Error", "unknown peer");'),
    (check_sandbox, HOST, 'const auto proxied = (QNetworkProxy::applicationProxy().type()\n\t\t!= QNetworkProxy::NoProxy);', 'const auto proxied = false;'),
    (check_sandbox, HOST, '}, std::exchange(first, false));', '});'),
    (check_sandbox, HOST, 'if (!strong || !strong->_requests.contains(promiseId)) {\n\t\t\treturn true;', 'if (!strong) {\n\t\t\treturn true;'),
    (check_sandbox, HOST, 'if (strong && strong->_requests.contains(promiseId)) {', 'if (strong) {'),
    (check_sandbox, HOST, '.toLower() == "host") {\n\t\t\t\tcontinue;', '.toLower() == "host") {\n\t\t\t\t;'),
    (check_sandbox, HOST, '\t\tif (isLiteral && !IsPublic(literal)) {\n', '\t\tif (false) {\n'),
    (check_sandbox, HOST, '\t\t\tif (!IsPublic(address)) {\n', '\t\t\tif (false) {\n'),
    (check_sandbox, HOST, 'if (!SameOrigin(reply->url(), next)) {', 'if (false) {'),
    (check_sandbox, HOST, 'std::move(kept),', 'headers,'),
    (check_sandbox, HOST, '(port(a) == port(b))', 'true'),
    (check_sandbox, HOST, 'result["base64"] = bytes.toBase64().toStdString();', 'result["body"] = bytes.toStdString();'),
)


def crlf(text):
    return text.replace('\r\n', '\n')


def check(root, read=source):
    files = {path: crlf(read(path)) for path in
             (HOOKS, HOOKS_H, LOGIC, HOST, MANAGER, HEADER, INSTALLER, QRC, APIWRAP, UPDATES, SESSION, WIDGET,
              CHAT, APP, INNER, VIEW_MENU, SYNC, SCHEDULED, SHORTCUTS, STORIES, SHARE, DOCS)}
    for test in CHECKS:
        test(files)
    for test, path, before, after in MUTANTS:
        assert before in files[path], f'mutant anchor moved: {before}'
        mutated = dict(files)
        mutated[path] = files[path].replace(before, after, 1)
        try:
            test(mutated)
        except (AssertionError, ValueError):
            continue
        raise AssertionError(f'{test.__name__} accepts the mutant: {after}')

    tree, objects = build_core(root, read)
    logic = crlf(read(LOGIC))
    code, out = fixture(root, tree, objects, logic, 'plugin_host')
    assert code == 0 and out.strip() == 'plugin host fixture ok', out
    for index, (before, after) in enumerate(LOGIC_MUTANTS):
        assert logic.count(before) == 1, f'logic mutant anchor moved: {before}'
        try:
            code, out = fixture(root, tree, objects, logic.replace(before, after), f'plugin_host_m{index}')
        except AssertionError:
            continue  # Did not compile: rejected as well.
        if code == 0:
            raise AssertionError(f'the host fixture accepts the mutant: {after}')
    print('Plugin host PASS: beforeSend folding, commands, !X refusal, network and ports, '
          'consent, commit grants, timer clamps, CPU budget, reserved hotkeys, in-flight cap, '
          'log truncation, compatibility, settings, labels, directory swap; hooks are guarded '
          'and placed, engine entries are budgeted, commit checks the reviewed bytes')


if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='luxury-plugin-host-') as directory:
        check(Path(directory))
