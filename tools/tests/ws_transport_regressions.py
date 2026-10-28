"""Native WebSocket MTProto transport: framing, upgrade and route memory.

docs/ws-transport.md. The client is not built here, so:
- check_logic: mtproto_websocket_logic.{h,cpp} (std only) are compiled
  with -Wall -Wextra -Werror and run against the RFC 6455 rules, the
  upgrade response (101, accept, Upgrade/Connection tokens, protocol),
  the kws endpoint map for dc 1-5, 203 and test dcs, the per-dc route
  table and the per-round failure count, including the field log of the
  reference proxy: no pool or per-dc cap (F1), a bad route never drops a
  candidate (F2), no idled socket (F3), idle pings and no route failure
  after the session was established (F4), built-in endpoints for every dc
  (F5); a TCP session that stalls after the handshake hands the next
  rounds to WS until its window ends;
- check_sources: the Qt half is asserted as source text (SOURCE_RULES):
  one socket per racer, strict TLS, pings and pongs, buffer options after
  connect, failures marked only before the first payload and recorded by
  SessionPrivate once per round, the proxy/system proxy/cdn gates and
  the handshake floor, the route table resets, the cmake entries, the 13
  EN and RU strings and uniform line endings of every touched file.
Compiled mutants (LOGIC_MUTANTS) must fail the logic run, and every text
mutant (SOURCE_MUTANTS) must break at least one source rule.

Bounded source check; not a native acceptance.
"""
from pathlib import Path
import re
import tempfile
from library_regressions import CXX, ROOT, STATIC, function, run, source, standalone

SRC = 'Telegram/SourceFiles/'
DETAILS = 'mtproto/details/'
LOGIC_H = DETAILS + 'mtproto_websocket_logic.h'
LOGIC_CPP = DETAILS + 'mtproto_websocket_logic.cpp'
SOCKET_H = DETAILS + 'mtproto_websocket_socket.h'
SOCKET_CPP = DETAILS + 'mtproto_websocket_socket.cpp'

MAIN = r'''
#include "mtproto/details/mtproto_websocket_logic.cpp"

#include <cstdlib>
#include <iostream>

using namespace MTP::details;

#define EXPECT(condition) do { if (!(condition)) { \
	std::cerr << "line " << __LINE__ << ": " #condition "\n"; std::abort(); } } while (0)

namespace {

constexpr auto kMinute = int64_t(60 * 1000);

std::vector<uint8_t> Bytes(std::string_view text) {
	return { text.begin(), text.end() };
}

std::vector<uint8_t> ServerFrame(uint8_t first, std::vector<uint8_t> payload) {
	auto result = std::vector<uint8_t>{ first };
	if (payload.size() < 126) {
		result.push_back(uint8_t(payload.size()));
	} else {
		result.push_back(126);
		result.push_back(uint8_t(payload.size() >> 8));
		result.push_back(uint8_t(payload.size()));
	}
	result.insert(result.end(), payload.begin(), payload.end());
	return result;
}

std::string Drain(WsFrameDecoder &decoder) {
	auto buffer = std::vector<uint8_t>(decoder.available());
	EXPECT(decoder.read(buffer) == buffer.size());
	return { buffer.begin(), buffer.end() };
}

bool Never(WsDecision decision) {
	return !decision.useTcp && !decision.useWs;
}

void Endpoints() {
	EXPECT(WsHost(2, false) == "kws2.web.telegram.org");
	EXPECT(WsHost(4, true) == "kws4-1.web.telegram.org");
	EXPECT(WsHost(203, false) == "kws2.web.telegram.org");
	EXPECT(WsHost(203, true) == "kws2-1.web.telegram.org");
	EXPECT(WsPath(false) == "/apiws");
	EXPECT(WsPath(true) == "/apiws_test");

	// F5: every production dc, 203 and the test dcs have a built-in front.
	for (const auto dc : { 1, 2, 3, 4, 5, 203 }) {
		EXPECT(WsDcSupported(dc, false));
		EXPECT(!WsDefaultIp(dc, false).empty());
		for (const auto media : { false, true }) {
			const auto host = WsHost(dc, media);
			EXPECT(host.starts_with("kws") && host.ends_with(".web.telegram.org"));
		}
		const auto ips = WsCandidateIps(dc, false, "", "");
		EXPECT(ips.size() == 2 && ips[0] == kWsPinnedIp);
	}
	for (const auto dc : { 1, 2, 3 }) {
		EXPECT(WsDcSupported(dc, true));
		EXPECT(!WsDefaultIp(dc, true).empty());
		EXPECT(WsDefaultIp(dc, true) != WsDefaultIp(dc, false));
	}
	EXPECT(WsDefaultIp(1, false) == "149.154.175.50");
	EXPECT(WsDefaultIp(203, false) == "91.105.192.100");
	EXPECT(!WsDcSupported(0, false) && !WsDcSupported(6, false));
	EXPECT(!WsDcSupported(4, true));

	const auto ips = WsCandidateIps(
		2,
		false,
		"149.154.167.51",
		"2001:67c:4e8:f002::a");
	EXPECT(ips.size() == 3);
	EXPECT(ips[0] == "149.154.167.220");
	EXPECT(ips[1] == "149.154.167.51");
	EXPECT(ips[2] == "2001:67c:4e8:f002::a");
	const auto pinned = WsCandidateIps(2, false, "149.154.167.220", "");
	EXPECT(pinned.size() == 2 && pinned[1] == "149.154.167.51");
}

void Gate() {
	EXPECT(WsAllowed(WsMode::Auto, 2, false, false, false, false, false));
	EXPECT(WsAllowed(WsMode::Always, 203, false, false, false, false, false));
	EXPECT(WsAllowed(WsMode::Auto, 3, true, false, false, false, false));
	EXPECT(!WsAllowed(WsMode::Off, 2, false, false, false, false, false));
	EXPECT(!WsAllowed(WsMode::Auto, 2, false, true, false, false, false));
	EXPECT(!WsAllowed(WsMode::Always, 2, false, true, false, false, false));
	EXPECT(!WsAllowed(WsMode::Auto, 2, false, false, true, false, false));
	EXPECT(!WsAllowed(WsMode::Auto, 2, false, false, false, true, false));
	EXPECT(!WsAllowed(WsMode::Auto, 2, false, false, false, false, true));
	EXPECT(!WsAllowed(WsMode::Auto, 7, false, false, false, false, false));
}

void Upgrade() {
	const auto request = WsUpgradeRequest(
		"kws2.web.telegram.org",
		"/apiws",
		"dGhlIHNhbXBsZSBub25jZQ==");
	auto position = size_t(0);
	for (const auto line : {
		"GET /apiws HTTP/1.1\r\n",
		"Host: kws2.web.telegram.org\r\n",
		"Upgrade: websocket\r\n",
		"Connection: Upgrade\r\n",
		"Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n",
		"Sec-WebSocket-Version: 13\r\n",
		"Sec-WebSocket-Protocol: binary\r\n",
	}) {
		const auto found = request.find(line, position);
		EXPECT(found != std::string::npos);
		position = found + std::string_view(line).size();
	}
	EXPECT(request.ends_with("\r\n\r\n"));

	const auto accept = "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=";
	const auto head = std::string("HTTP/1.1 101 Switching Protocols\r\n"
		"upgrade: websocket\r\nCONNECTION: Upgrade\r\n"
		"sec-websocket-accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n"
		"Sec-WebSocket-Protocol: binary\r\n\r\n");
	const auto ok = WsParseUpgradeResponse(head + "\x82\x01x", accept);
	EXPECT(ok.status == WsHandshake::Ok);
	EXPECT(ok.consumed == head.size());
	EXPECT(ok.statusLine == "HTTP/1.1 101 Switching Protocols");

	EXPECT(WsParseUpgradeResponse(head.substr(0, 40), accept).status
		== WsHandshake::NeedMore);
	const auto refused = WsParseUpgradeResponse(
		"HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n",
		accept);
	EXPECT(refused.status == WsHandshake::Refused);
	EXPECT(refused.statusLine == "HTTP/1.1 404 Not Found");
	EXPECT(WsParseUpgradeResponse(
		"HTTP/1.1 1010 Odd\r\n\r\n",
		accept).status == WsHandshake::Refused);
	EXPECT(WsParseUpgradeResponse(
		"HTTP/1.1 101 OK\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
		"Sec-WebSocket-Accept: wrong=\r\n"
		"Sec-WebSocket-Protocol: binary\r\n\r\n",
		accept).status == WsHandshake::BadAccept);
	EXPECT(WsParseUpgradeResponse(
		"HTTP/1.1 101 OK\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
		"Sec-WebSocket-Accept: "
		"s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n\r\n",
		accept).status == WsHandshake::BadAccept);
	EXPECT(WsParseUpgradeResponse(
		"HTTP/1.1 101 OK\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
		"Sec-WebSocket-Accept: "
		"s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\nSec-WebSocket-Protocol: chat\r\n\r\n",
		accept).status == WsHandshake::BadAccept);
	EXPECT(WsParseUpgradeResponse(std::string(9000, 'a'), accept).status
		== WsHandshake::TooLarge);

	// Upgrade: websocket and an Upgrade token in Connection are required.
	const auto tail = std::string("Sec-WebSocket-Accept: "
		"s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\nSec-WebSocket-Protocol: binary\r\n\r\n");
	const auto status = [&](std::string headers) {
		return WsParseUpgradeResponse(
			"HTTP/1.1 101 OK\r\n" + headers + tail,
			accept).status;
	};
	EXPECT(status("Upgrade: WebSocket\r\nConnection: keep-alive, upgrade\r\n")
		== WsHandshake::Ok);
	EXPECT(status("Connection: Upgrade\r\n") == WsHandshake::BadAccept);
	EXPECT(status("Upgrade: websocket\r\n") == WsHandshake::BadAccept);
	EXPECT(status("Upgrade: h2c\r\nConnection: Upgrade\r\n")
		== WsHandshake::BadAccept);
	EXPECT(status("Upgrade: websocket\r\nConnection: keep-alive\r\n")
		== WsHandshake::BadAccept);
	EXPECT(status("Upgrade: websocket\r\nConnection: upgraded\r\n")
		== WsHandshake::BadAccept);
}

void Encode() {
	const uint8_t mask[4] = { 0x37, 0xfa, 0x21, 0x3d };
	const auto hello = Bytes("Hello");
	const auto frame = WsEncodeFrame(WsOpcode::Binary, hello, mask);
	EXPECT(frame.size() == 2 + 4 + 5);
	EXPECT(frame[0] == 0x82 && frame[1] == 0x85);
	EXPECT(std::equal(mask, mask + 4, frame.begin() + 2));
	for (size_t i = 0; i != hello.size(); ++i) {
		EXPECT(frame[6 + i] == uint8_t(hello[i] ^ mask[i % 4]));
	}
	const auto medium = WsEncodeFrame(
		WsOpcode::Binary,
		std::vector<uint8_t>(126, 1),
		mask);
	EXPECT(medium[1] == (0x80 | 126) && medium[2] == 0 && medium[3] == 126);
	EXPECT(medium.size() == 4 + 4 + 126);
	const auto large = WsEncodeFrame(
		WsOpcode::Binary,
		std::vector<uint8_t>(65536, 1),
		mask);
	EXPECT(large[1] == (0x80 | 127));
	EXPECT(large[7] == 1 && large[8] == 0 && large[9] == 0);
	EXPECT(large.size() == 10 + 4 + 65536);
	const auto pong = WsEncodeFrame(WsOpcode::Pong, Bytes("p"), mask);
	EXPECT(pong[0] == 0x8A && (pong[1] & 0x80));
	const auto ping = WsEncodeFrame(WsOpcode::Ping, {}, mask);
	EXPECT(ping[0] == 0x89 && ping[1] == 0x80 && ping.size() == 6);
}

void Decode() {
	auto stream = ServerFrame(0x82, Bytes("abc"));
	const auto second = ServerFrame(0x82, std::vector<uint8_t>(300, 'z'));
	stream.insert(stream.end(), second.begin(), second.end());
	auto decoder = WsFrameDecoder();
	EXPECT(!decoder.received());
	for (const auto byte : stream) {
		decoder.feed(std::span<const uint8_t>(&byte, 1));
	}
	EXPECT(decoder.available() == 303);
	EXPECT(decoder.received());
	auto part = std::vector<uint8_t>(2);
	EXPECT(decoder.read(part) == 2 && part[0] == 'a' && part[1] == 'b');
	EXPECT(decoder.available() == 301);
	EXPECT(Drain(decoder) == "c" + std::string(300, 'z'));
	EXPECT(decoder.available() == 0);
	EXPECT(!decoder.failed() && !decoder.closed());

	auto fragments = WsFrameDecoder();
	fragments.feed(ServerFrame(0x02, Bytes("one ")));
	EXPECT(fragments.available() == 4);
	fragments.feed(ServerFrame(0x89, Bytes("hi")));
	fragments.feed(ServerFrame(0x80, Bytes("two")));
	EXPECT(Drain(fragments) == "one two");
	const auto pongs = fragments.takePongs();
	EXPECT(pongs.size() == 1 && pongs[0] == Bytes("hi"));
	EXPECT(fragments.takePongs().empty());
	fragments.feed(ServerFrame(0x8A, {}));
	EXPECT(!fragments.failed());

	auto closing = WsFrameDecoder();
	closing.feed(ServerFrame(0x88, { 0x03, 0xe8 }));
	EXPECT(closing.closed() && !closing.failed());

	auto masked = WsFrameDecoder();
	masked.feed(std::vector<uint8_t>{ 0x82, 0x81, 1, 2, 3, 4, 5 });
	EXPECT(masked.failed() && masked.available() == 0);

	auto text = WsFrameDecoder();
	text.feed(ServerFrame(0x81, Bytes("t")));
	EXPECT(text.failed());

	auto huge = WsFrameDecoder();
	huge.feed(std::vector<uint8_t>{ 0x82, 127, 0, 0, 0, 0, 0x01, 0, 0, 1 });
	EXPECT(huge.failed());

	auto stray = WsFrameDecoder();
	stray.feed(ServerFrame(0x80, Bytes("x")));
	EXPECT(stray.failed());

	// F1/F3: decoders share nothing, each connection owns its own state.
	auto many = std::vector<WsFrameDecoder>(10);
	for (auto i = 0; i != 10; ++i) {
		many[i].feed(ServerFrame(0x82, { uint8_t(i) }));
	}
	for (auto i = 0; i != 10; ++i) {
		EXPECT(many[i].available() == 1);
	}
}

void Alive() {
	// F4: ping on idle well before the ~90s server timeout.
	EXPECT(kWsPingInterval > 0 && kWsPingInterval <= 30000);
	EXPECT(!WsPingDue(1000, 1000 + kWsPingInterval - 1));
	EXPECT(WsPingDue(1000, 1000 + kWsPingInterval));
	EXPECT(WsPingDue(0, 90000));
	// F4: once a payload byte arrived the route is not blamed for a reset.
	EXPECT(WsCountsAsRouteFailure(false));
	EXPECT(!WsCountsAsRouteFailure(true));
}

void Routes() {
	auto table = WsRouteTable();
	const auto key = WsKey{ 2, true, false };
	auto now = int64_t(1000000);

	auto d = table.decide(key, WsMode::Auto, now);
	EXPECT(d.useTcp && d.useWs && d.wsDelayMs == 0);
	EXPECT(table.succeeded(key, false, now) == WsRoute::Unknown);
	d = table.decide(key, WsMode::Auto, now);
	EXPECT(d.useTcp && d.useWs && d.wsDelayMs == 2000);

	// Refused: WS only trails TCP for 30 minutes, it is never dropped (F2).
	table.failed(key, true, now);
	d = table.decide(key, WsMode::Auto, now + 30 * kMinute - 1);
	EXPECT(d.useTcp && d.useWs && d.wsDelayMs == kWsDirectHeadStart);
	EXPECT(table.snapshot()[0].second.wsCooldownUntil == now + 30 * kMinute);

	table.reset();
	EXPECT(table.snapshot().empty());
	table.failed(key, true, now);
	d = table.decide(key, WsMode::Auto, now + 30 * kMinute);
	EXPECT(d.useTcp && d.useWs && d.wsDelayMs == 0);

	// Network failures back off 30s, 60s, 120s ... capped at 15 minutes.
	table.reset();
	const int64_t expected[] = { 30, 60, 120, 240, 480, 900, 900, 900 };
	for (const auto seconds : expected) {
		table.failed(key, false, now);
		EXPECT(table.snapshot()[0].second.wsCooldownUntil
			== now + seconds * 1000);
	}
	d = table.decide(key, WsMode::Auto, now);
	EXPECT(d.useTcp && d.useWs && d.wsDelayMs == kWsDirectHeadStart);

	// WS wins: TCP is skipped until the direct re-probe is due.
	table.reset();
	table.succeeded(key, true, now);
	d = table.decide(key, WsMode::Auto, now + 5 * kMinute - 1);
	EXPECT(!d.useTcp && d.useWs && d.wsDelayMs == 0);
	d = table.decide(key, WsMode::Auto, now + 5 * kMinute);
	EXPECT(d.useTcp && d.useWs && d.wsDelayMs == 0);
	// A plain reconnect over WS keeps the probe time.
	table.succeeded(key, true, now + kMinute);
	EXPECT(table.snapshot()[0].second.directProbeAt == now + 5 * kMinute);
	const int64_t steps[] = { 10, 20, 40, 60, 60 };
	for (const auto minutes : steps) {
		now += 5 * kMinute;
		table.succeeded(key, true, now, true);
		const auto state = table.snapshot()[0].second;
		EXPECT(state.directProbeStep == minutes * kMinute);
		EXPECT(state.directProbeAt == now + minutes * kMinute);
	}
	// Direct came back: the probe ladder restarts.
	table.succeeded(key, false, now);
	EXPECT(table.snapshot()[0].second.directProbeStep == 0);

	// WS route cooling down: TCP comes back, WS still races (F2).
	table.reset();
	table.succeeded(key, true, now);
	table.failed(key, false, now);
	EXPECT(table.route(key) == WsRoute::WebSocket);
	d = table.decide(key, WsMode::Auto, now + 1);
	EXPECT(d.useTcp && d.useWs && d.wsDelayMs == kWsDirectHeadStart);
	table.failed(key, false, now);
	EXPECT(table.route(key) == WsRoute::Unknown);

	// Always ignores cooldowns, Off never offers WS.
	d = table.decide(key, WsMode::Always, now);
	EXPECT(!d.useTcp && d.useWs && d.wsDelayMs == 0);
	d = table.decide(key, WsMode::Off, now);
	EXPECT(d.useTcp && !d.useWs);

	// Stalls after the session was established are no route failures (F4).
	table.reset();
	table.succeeded(key, false, now);
	EXPECT(table.stalled(key, false, now) == WsRoute::Direct);
	EXPECT(table.route(key) == WsRoute::Unknown);
	EXPECT(table.snapshot()[0].second.wsFailures == 0);
	// DPI passing the handshake: TCP would win every race on priority, so
	// WS goes alone at once, and TCP is rechecked after the window (F2).
	d = table.decide(key, WsMode::Auto, now);
	EXPECT(!d.useTcp && d.useWs && d.wsDelayMs == 0);
	d = table.decide(key, WsMode::Auto, now + kWsTcpStallBase - 1);
	EXPECT(!d.useTcp && d.useWs);
	d = table.decide(key, WsMode::Auto, now + kWsTcpStallBase);
	EXPECT(d.useTcp && d.useWs && d.wsDelayMs == 0);
	// Repeated stalls double the window up to the cap.
	auto at = now + kWsTcpStallBase;
	for (const auto minutes : { 4, 8, 16, 30, 30 }) {
		table.stalled(key, false, at);
		EXPECT(table.snapshot()[0].second.tcpStallUntil
			== at + minutes * kMinute);
		at += minutes * kMinute;
	}
	// Stalls keep coming: the shift stays clamped, the window capped.
	for (auto i = 0; i != 64; ++i) {
		table.stalled(key, false, at);
		EXPECT(table.snapshot()[0].second.tcpStallUntil == at + 30 * kMinute);
		at += 30 * kMinute;
	}
	// A quiet half hour restarts the ladder.
	table.stalled(key, false, at + 31 * kMinute);
	EXPECT(table.snapshot()[0].second.tcpStalls == 1);
	// WS failing inside the window brings TCP back, also once the WS
	// cooldown is over.
	for (const auto refused : { false, true }) {
		table.reset();
		table.stalled(key, false, now);
		table.failed(key, refused, now);
		d = table.decide(key, WsMode::Auto, now + 1);
		EXPECT(d.useTcp && d.useWs);
		d = table.decide(key, WsMode::Auto, now + kWsFailBackoffBase + 1);
		EXPECT(d.useTcp && d.useWs);
	}
	// A WS-only round gets no head start, even on a Direct route.
	table.reset();
	table.stalled(key, false, now);
	table.succeeded(key, false, now);
	d = table.decide(key, WsMode::Auto, now + 1);
	EXPECT(!d.useTcp && d.useWs && d.wsDelayMs == 0);
	// WS stalling too: TCP races again, the ladder is kept.
	table.reset();
	table.stalled(key, false, now);
	table.stalled(key, true, now + 1);
	d = table.decide(key, WsMode::Auto, now + 1);
	EXPECT(d.useTcp && d.useWs);
	EXPECT(table.snapshot()[0].second.tcpStalls == 1);
	table.reset();
	table.succeeded(key, true, now);
	table.stalled(key, true, now);
	const auto stalled = table.snapshot()[0].second;
	EXPECT(stalled.route == WsRoute::Unknown);
	EXPECT(stalled.wsFailures == 0 && stalled.wsCooldownUntil == 0);

	// Keys are independent.
	table.reset();
	table.succeeded(WsKey{ 2, false, false }, true, now);
	EXPECT(table.route(WsKey{ 2, true, false }) == WsRoute::Unknown);
	EXPECT(table.route(WsKey{ 2, false, true }) == WsRoute::Unknown);
	EXPECT(table.snapshot().size() == 1);

	// F1/F2: no pool or per-dc cap; a burst of ten connections all get WS,
	// and no reachable state ever yields an empty candidate set.
	table.reset();
	table.succeeded(key, true, now);
	for (auto i = 0; i != 10; ++i) {
		const auto burst = table.decide(key, WsMode::Auto, now + i);
		EXPECT(burst.useWs);
	}
	for (auto round = 0; round != 64; ++round) {
		const auto at = now + round * 7 * kMinute;
		switch (round % 5) {
		case 0: table.failed(key, round % 2, at); break;
		case 1: table.succeeded(key, round % 3, at, round % 2); break;
		case 2: table.stalled(key, round % 2, at); break;
		case 3: table.failed(key, false, at); break;
		case 4: table.succeeded(key, true, at); break;
		}
		for (const auto mode : { WsMode::Off, WsMode::Auto, WsMode::Always }) {
			for (const auto later : { int64_t(0), kMinute, 61 * kMinute }) {
				EXPECT(!Never(table.decide(key, mode, at + later)));
			}
		}
	}
}

void Round() {
	// One route failure per round: the last WS racer ends it.
	auto round = WsRound();
	round.add();
	round.add();
	round.add();
	EXPECT(round.ended(WsFailure::Network) == WsFailure::None);
	EXPECT(round.ended(WsFailure::Refused) == WsFailure::None);
	EXPECT(round.ended(WsFailure::None) == WsFailure::Refused);
	EXPECT(round.ended(WsFailure::Network) == WsFailure::None);
	// Racers that never started (or got payload) fail nothing.
	round.add();
	round.add();
	EXPECT(round.ended(WsFailure::None) == WsFailure::None);
	EXPECT(round.ended(WsFailure::None) == WsFailure::None);
	round.add();
	EXPECT(round.ended(WsFailure::Network) == WsFailure::Network);
	// A racer that got MTProto bytes proves the route: nothing is failed.
	round.add();
	round.add();
	EXPECT(round.ended(WsFailure::Network) == WsFailure::None);
	EXPECT(round.ended(WsFailure::Worked) == WsFailure::None);
	round.add();
	round.add();
	EXPECT(round.ended(WsFailure::Worked) == WsFailure::None);
	EXPECT(round.ended(WsFailure::Refused) == WsFailure::None);
	// A new round forgets the abandoned one.
	round.add();
	round.add();
	EXPECT(round.ended(WsFailure::Refused) == WsFailure::None);
	round = WsRound();
	round.add();
	EXPECT(round.ended(WsFailure::Network) == WsFailure::Network);
}

void Log() {
	auto log = WsLog();
	const auto before = log.revision();
	for (auto i = 0; i != 250; ++i) {
		log.add("line " + std::to_string(i));
	}
	const auto lines = log.lines();
	EXPECT(lines.size() == 200);
	EXPECT(lines.front() == "line 50" && lines.back() == "line 249");
	EXPECT(log.revision() == before + 250);
	log.clear();
	EXPECT(log.lines().empty() && log.revision() == before + 251);
	EXPECT(WsKeyName(WsKey{ 4, true, true }) == "dc4 media test");
	EXPECT(std::string(WsRouteName(WsRoute::WebSocket)) == "WebSocket");
}

} // namespace

int main() {
	Endpoints();
	Gate();
	Upgrade();
	Encode();
	Decode();
	Alive();
	Routes();
	Round();
	Log();
	std::cout << "ws_transport logic ok\n";
	return 0;
}
'''

# (file, before, after): every one must still compile and fail at runtime.
LOGIC_MUTANTS = (
	(LOGIC_H, 'kWsDirectHeadStart = 2000', 'kWsDirectHeadStart = 0'),
	(LOGIC_H, 'kWsRefusedCooldown = 30 * 60 * 1000', 'kWsRefusedCooldown = 0'),
	(LOGIC_CPP, 'kWsFailBackoffBase\n\t\t<< (', 'kWsFailBackoffBase\n\t\t* ('),
	(LOGIC_CPP, 'nowMs + std::min(backoff', 'nowMs + std::max(backoff'),
	(LOGIC_CPP, 'if (masked) {', 'if (false) {'),
	(LOGIC_CPP, 'uint8_t(0x82)', 'uint8_t(0x02)'),
	(LOGIC_CPP, 'code != " 101"', 'code != " 10"'),
	(LOGIC_CPP, 'state.directProbeStep * 2', 'state.directProbeStep * 1'),
	(LOGIC_CPP, 'while (_lines.size() > kWsLogLines)', 'while (_lines.size() > kWsLogLines + 1)'),
	# Unmasked client frame.
	(LOGIC_CPP, 'const auto maskBit = uint8_t(0x80);', 'const auto maskBit = uint8_t(0x00);'),
	# Media dc mapped to kws{dc} first.
	(LOGIC_CPP, '(media ? "-1" : "")', '("")'),
	# 203 not mapped to 2.
	(LOGIC_CPP, '(dc == 203) ? 2 : dc', 'dc'),
	# Test dc without the _test path.
	(LOGIC_CPP, 'test ? "/apiws_test" : "/apiws"', '"/apiws"'),
	# Route never re-probes direct after WS won.
	(LOGIC_CPP, '|| (nowMs >= state.directProbeAt)', '|| false'),
	# WS used while a user proxy is set.
	(LOGIC_CPP, '&& !proxy\n', '\n'),
	# Setting Off still creating WS candidates.
	(LOGIC_CPP, 'return (mode != WsMode::Off)', 'return true'),
	(LOGIC_CPP, 'if (mode == WsMode::Off) {\n\t\treturn result;', 'if (false) {\n\t\treturn result;'),
	# F1: a per-dc cap on WS connections.
	(LOGIC_CPP, '\tresult.useWs = true;\n\tresult.useTcp = cooling',
		'\tresult.useWs = (++_states[key].wsFailures < 4);\n\tresult.useTcp = cooling'),
	# F2: a bad route drops WS instead of delaying it.
	(LOGIC_CPP, '\tresult.useWs = true;\n\tresult.useTcp = cooling',
		'\tresult.useWs = !cooling;\n\tresult.useTcp = cooling'),
	# F2: a cooling WS route leaves no TCP candidate.
	(LOGIC_CPP, 'result.useTcp = cooling\n\t\t|| (!tcpStalling', 'result.useTcp = (!tcpStalling'),
	# F3/F1: decoders sharing one buffer, as a pool would.
	(LOGIC_H, '\tstd::vector<uint8_t> _payload;\n', '\tinline static std::vector<uint8_t> _payload;\n'),
	# F4: idle pings too rare for the server timeout.
	(LOGIC_H, 'kWsPingInterval = 30000', 'kWsPingInterval = 120000'),
	# F4: a reset after the session was established blames the route.
	(LOGIC_CPP, 'return !received;', 'return true;'),
	(LOGIC_CPP, '\tif (state.route == (websocket ? WsRoute::WebSocket : WsRoute::Direct)) {',
		'\tif (websocket) {\n\t\tnetworkFailure(state, nowMs);\n\t}\n\tif (state.route == (websocket ? WsRoute::WebSocket : WsRoute::Direct)) {'),
	# F5: dc 1 or 203 missing a built-in endpoint.
	(LOGIC_CPP, '\tcase 1: return "149.154.175.50";\n', ''),
	(LOGIC_CPP, ': ((dc >= 1 && dc <= 5) || dc == 203);', ': (dc >= 2 && dc <= 5);'),
	(LOGIC_CPP, '\tadd(WsDefaultIp(dc, test));\n', ''),
	# Fragments, text frames and oversize frames.
	(LOGIC_CPP, '\t\tappend();\n\t\t_fragmented = !fin;\n\t\treturn true;\n\tcase WsOpcode::Binary:',
		'\t\t_fragmented = !fin;\n\t\treturn true;\n\tcase WsOpcode::Binary:'),
	(LOGIC_CPP, 'if (length > kWsMaxFrame) {', 'if (false) {'),
	(LOGIC_CPP, '\tcase WsOpcode::Text:\n\t\tbreak;', '\tcase WsOpcode::Text:\n\t\treturn true;'),
	(LOGIC_CPP, '_pongs.emplace_back(payload.begin(), payload.end());', ''),
	(LOGIC_CPP, 'binary = EqualNoCase(value, "binary");', 'binary = true;'),
	(LOGIC_CPP, 'if (EqualNoCase(name, "sec-websocket-accept")) {', 'if (name == "Sec-WebSocket-Accept") {'),
	# 7: Upgrade and Connection headers not required.
	(LOGIC_CPP, '(accepted && binary && upgrade && connection)', '(accepted && binary)'),
	(LOGIC_CPP, 'upgrade = EqualNoCase(value, "websocket");', 'upgrade = true;'),
	(LOGIC_CPP, 'connection = HasToken(value, "upgrade");', 'connection = EqualNoCase(value, "upgrade");'),
	(LOGIC_CPP, 'if (EqualNoCase(Trim(list.substr(0, comma)), token)) {',
		'if (Trim(list.substr(0, comma)).find(token.substr(1)) != std::string_view::npos) {'),
	# 2: a stalled TCP keeps racing (and winning on priority) every round.
	(LOGIC_CPP, '\t\t|| (!tcpStalling\n', '\t\t|| (true\n'),
	(LOGIC_CPP, '\tstate.tcpStallUntil = nowMs + std::min(window, kWsTcpStallMax);\n', ''),
	# 2/F2: the stall window locks TCP out for good, or never grows.
	(LOGIC_CPP, 'nowMs + std::min(window, kWsTcpStallMax)', 'nowMs + window * 1000'),
	(LOGIC_CPP, '<< (std::min(state.tcpStalls, 5) - 1)', '<< 0'),
	(LOGIC_CPP, '\t\tstate.tcpStallUntil = std::min(state.tcpStallUntil, nowMs);\n\t\treturn was;',
		'\t\treturn was;'),
	(LOGIC_CPP, '\tstate.tcpStallUntil = std::min(state.tcpStallUntil, nowMs);\n\tif (refused) {',
		'\tif (refused) {'),
	(LOGIC_CPP, '<< (std::min(state.tcpStalls, 5) - 1)', '<< (state.tcpStalls - 1)'),
	# 3: WS-only rounds wait for a TCP that is not racing.
	(LOGIC_CPP, '(result.useTcp\n\t\t&& (state.route', '((state.route'),
	(LOGIC_CPP, 'state.tcpStalls = 0;', ''),
	# 4: every WS racer of a round counted as a route failure.
	(LOGIC_CPP, 'if (--_racers > 0) {', 'if (--_racers > 0 && racer == WsFailure::None) {'),
	(LOGIC_CPP, '_failure = std::max(_failure, racer);', '_failure = racer;'),
	(LOGIC_CPP, '\t*this = WsRound();\n\treturn result;', '\t_racers = 0;\n\treturn result;'),
	# 4: a round where a WS racer got payload is still failed.
	(LOGIC_CPP, '\t\t_worked = true;\n', ''),
	(LOGIC_CPP, 'const auto result = _worked ? WsFailure::None : _failure;', 'const auto result = _failure;'),
)


def crlf(text):
	return text.replace('\r\n', '\n')


def build(root, name, files, strict=True):
	tree = root / name
	for path, text in files.items():
		target = tree / path
		target.parent.mkdir(parents=True, exist_ok=True)
		target.write_text(text, encoding='utf-8', newline='\n')
	main = tree / 'main.cpp'
	main.write_text(MAIN, encoding='utf-8', newline='\n')
	exe = tree / 'main.exe'
	# Mutants may leave a variable unused; only the real source must be warning-free.
	warnings = ('-Wall', '-Wextra', '-Werror') if strict else ()
	run([CXX, '-std=c++20', '-O0', *warnings, '-I', str(tree), str(main), *STATIC, '-o', str(exe)], tree)
	standalone(exe)
	return exe


def check_logic(root, read):
	files = {path: crlf(read(SRC + path)) for path in (LOGIC_H, LOGIC_CPP)}
	print(run([str(build(root, 'ws_logic', files))], root).strip())
	for index, (path, before, after) in enumerate(LOGIC_MUTANTS):
		assert files[path].count(before) == 1, f'logic mutant anchor moved: {before!r}'
		mutated = dict(files)
		mutated[path] = files[path].replace(before, after)
		try:
			exe = build(root, f'ws_mutant_{index}', mutated, strict=False)
		except AssertionError as error:
			raise AssertionError(f'mutant does not compile: {after!r}\n{error}')
		try:
			run([str(exe)], root)
		except AssertionError:
			continue
		raise AssertionError(f'accepts mutant: {before!r} -> {after!r}')


MTPROTO = SRC + 'mtproto/'
SOCKET = SRC + SOCKET_CPP
SESSION_PRIVATE = MTPROTO + 'session_private.cpp'
CONNECTION_TCP = MTPROTO + 'connection_tcp.cpp'
SESSION = MTPROTO + 'session.cpp'
INSTANCE = MTPROTO + 'mtp_instance.cpp'
ACCOUNT = SRC + 'main/main_account.cpp'
CMAKE = 'Telegram/cmake/td_mtproto.cmake'
LANG_EN = 'Telegram/Resources/langs/lang.strings'
LANG_RU = SRC + 'luxury/luxury_settings.cpp'
LANG_KEYS = (
	'luxury_NetworkHeader',
	'luxury_WebSocketTransport',
	'luxury_WebSocketTransportOff',
	'luxury_WebSocketTransportAuto',
	'luxury_WebSocketTransportAlways',
	'luxury_WebSocketTransportDescription',
	'luxury_WebSocketTransportProxyActive',
	'luxury_WebSocketStatusOff',
	'luxury_WebSocketStatusProxy',
	'luxury_WebSocketStatusConnecting',
	'luxury_WebSocketStatusDirect',
	'luxury_WebSocketStatusActive',
	'luxury_WebSocketLog',
)
# Files whose line endings must stay uniform: an edit that eats CRLF
# leaves a mix. The ending itself follows the checkout (core.autocrlf).
LINE_ENDINGS = (
	SRC + LOGIC_H, SRC + LOGIC_CPP, SRC + SOCKET_H, SOCKET,
	MTPROTO + 'session_private.h', SESSION_PRIVATE,
	MTPROTO + 'connection_tcp.h', CONNECTION_TCP,
	SESSION, INSTANCE, ACCOUNT, SRC + 'main/main_account.h', CMAKE,
)

CONNECT_TO_SERVER = 'void SessionPrivate::connectToServer('
APPEND_WS = 'void SessionPrivate::appendWebSocketConnections('
APPEND_TEST = 'void SessionPrivate::appendTestConnection('
WAIT_RECEIVED = 'void SessionPrivate::waitReceivedFailed('
WS_SUCCEEDED = 'void SessionPrivate::wsSucceeded('
WS_ENDED = 'void SessionPrivate::wsRacerEnded('

# (name, path, scope function or None, needle, expected): expected is True
# or False for presence, an int for an exact count.
SOURCE_RULES = (
	# F1: a fresh socket per racer, nothing pooled or shared.
	('F1 socket per connection', CONNECTION_TCP, None, 'std::make_unique<WebSocketSocket>(', True),
	('F1 racer per candidate', SESSION_PRIVATE, APPEND_WS, 'appendTestConnection(', True),
	# F2: the table only orders candidates; an allowed WS always races.
	('F2 route consulted per round', SESSION_PRIVATE, CONNECT_TO_SERVER, 'WsRouteTable::Instance().decide(', True),
	('F2 ws racers appended', SESSION_PRIVATE, CONNECT_TO_SERVER, 'if (wsDecision.useWs) {\n\t\t\tappendWebSocketConnections(variants, wsDecision);', True),
	# F3: closing an established session is a stall, never a route failure.
	('F3 silent session stalls', SESSION_PRIVATE, WAIT_RECEIVED, 'WsRouteTable::Instance().stalled(', True),
	('F3 silent session not failed', SESSION_PRIVATE, WAIT_RECEIVED, '.failed(', False),
	('F3 failure before payload only', SOCKET, 'void WebSocketSocket::noteFailure(', 'WsCountsAsRouteFailure(_decoder.received())', True),
	# F4: 30s ping, pongs answered.
	('F4 ping timer', SOCKET, None, '_pingTimer.callEach(kWsPingInterval / 2);', True),
	('F4 ping handler', SOCKET, None, '_pingTimer(thread, [=] { sendPingIfIdle(); })', True),
	('F4 pong reply', SOCKET, 'void WebSocketSocket::readFrames(', 'sendFrame(WsOpcode::Pong, payload);', True),
	# F5: endpoints of the reference.
	('F5 host per dc and media', SESSION_PRIVATE, APPEND_WS, '.host = WsHost(key.dc, key.media),', True),
	('F5 path per test', SESSION_PRIVATE, APPEND_WS, '.path = WsPath(key.test),', True),
	('F5 candidate ips', SESSION_PRIVATE, APPEND_WS, 'WsCandidateIps(', True),
	# TLS strictness.
	('TLS never ignored', SOCKET, None, 'ignoreSslErrors', False),
	('TLS errors fail', SOCKET, None, 'handleError(-1, u"TLS: "_q', True),
	('TLS peer name', SOCKET, 'void WebSocketSocket::start(', '_socket.setPeerVerifyName(host);', True),
	('no system proxy on ws', SOCKET, None, '_socket.setProxy(QNetworkProxy(QNetworkProxy::NoProxy));', True),
	# 5: buffer options once the socket engine exists.
	('5 buffers on connected', SOCKET, None, '&QAbstractSocket::connected,\n\t\twrap([=] { applyBufferOptions(); })', True),
	('5 buffers only there', SOCKET, None, 'setSocketOption(', 2),
	('5 buffers applied', SOCKET, 'void WebSocketSocket::applyBufferOptions(', 'ReceiveBufferSizeSocketOption', True),
	# 4: the socket only reports; SessionPrivate counts once per round.
	('4 socket records nothing', SOCKET, None, 'WsRouteTable', False),
	('4 payload is a win', SOCKET, 'WsFailure WebSocketSocket::failure(', '_decoder.received() ? WsFailure::Worked', True),
	('4 failure exposed', CONNECTION_TCP, 'WsFailure TcpConnection::webSocketFailure(', '->failure()', True),
	('4 round counts racers', SESSION_PRIVATE, APPEND_TEST, '_wsRound.add();', True),
	('4 round aggregates', SESSION_PRIVATE, WS_ENDED, '_wsRound.ended(', True),
	('4 one failure per round', SESSION_PRIVATE, WS_ENDED, 'WsRouteTable::Instance().failed(', True),
	('4 failures only here', SESSION_PRIVATE, None, 'WsRouteTable::Instance().failed(', 1),
	('4 error ends racer', SESSION_PRIVATE, 'void SessionPrivate::onError(', 'wsRacerEnded(connection);\n\tremoveTestConnection(connection);', True),
	('4 disconnect ends racer', SESSION_PRIVATE, 'void SessionPrivate::onDisconnected(', 'wsRacerEnded(connection);\n\tremoveTestConnection(connection);', True),
	('4 timeout ends round', SESSION_PRIVATE, 'void SessionPrivate::connectingTimedOut(', 'wsRacerEnded(racer);', True),
	('4 winner ends round', SESSION_PRIVATE, WS_SUCCEEDED, '_wsRound = WsRound();', True),
	('4 round reset', SESSION_PRIVATE, 'void SessionPrivate::destroyAllConnections(', '_wsRound = WsRound();', True),
	# A working direct route wins inside kWaitForBetterTimeout.
	('ws priority below tcp', SESSION_PRIVATE, APPEND_TEST, 'const auto priority = ws ? 0 :', True),
	# 1: system proxy only when the OS reports one.
	('1 system proxy queried', SESSION_PRIVATE, None, 'QNetworkProxyFactory::systemProxyForQuery(', True),
	('1 system proxy host', SESSION_PRIVATE, None, 'u"kws2.web.telegram.org"_q,\n\t\t\t443,', True),
	('1 no proxy is none', SESSION_PRIVATE, None, '(type != QNetworkProxy::NoProxy)', True),
	('1 default proxy is none', SESSION_PRIVATE, None, '(type != QNetworkProxy::DefaultProxy)', True),
	# The lookup runs last, only when every cheap gate already let WS in.
	('1 setting checked', SESSION_PRIVATE, CONNECT_TO_SERVER, '_instance->isKeysDestroyer())\n\t\t\t&& !(_options->systemProxy && SystemProxyPresent());', True),
	('1 lookup once', SESSION_PRIVATE, None, 'SystemProxyPresent()', 2),
	# The lookup can block on WPAD: never on the main thread.
	('1 no lookup on main thread', SESSION, None, 'systemProxyForQuery', False),
	# Gates: proxy, system proxy, cdn/special dc, keys destroyer.
	('gate proxy', SESSION_PRIVATE, CONNECT_TO_SERVER, '_options->proxy.type != ProxyData::Type::None,\n\t\t\tfalse, // System proxy last', True),
	('gate cdn', SESSION_PRIVATE, CONNECT_TO_SERVER, 'special || (_currentDcType == DcType::Cdn),', True),
	('gate keys destroyer', SESSION_PRIVATE, CONNECT_TO_SERVER, 'special || (_currentDcType == DcType::Cdn),\n\t\t\t_instance->isKeysDestroyer())', True),
	# 8: routes are learned only in rounds the gate allowed.
	('8 round gate reset', SESSION_PRIVATE, CONNECT_TO_SERVER, '_wsAllowed = false;', True),
	('8 success gated', SESSION_PRIVATE, WS_SUCCEEDED, 'if (!_wsAllowed) {\n\t\treturn;\n\t}', True),
	('8 stall gated', SESSION_PRIVATE, WAIT_RECEIVED, 'if (_connection && _wsAllowed) {', True),
	('8 failure gated', SESSION_PRIVATE, WS_ENDED, '|| !_wsAllowed', True),
	# The connect timeout covers the head start plus the WS handshake.
	('handshake floor', SESSION_PRIVATE, CONNECT_TO_SERVER, 'crl::time(wsDecision.wsDelayMs + kWsHandshakeTimeout))', True),
	('handshake timer', SOCKET, 'void WebSocketSocket::start(', '_handshakeTimer.callOnce(kWsHandshakeTimeout);', True),
	# Resets: a new network always, a proxy only when toggled (6).
	('reachability reset', INSTANCE, None, 'availableChanges(\n\t) | rpl::on_next([=](bool available) {\n\t\t// A new network invalidates what we learned about blocked routes.\n\t\tWsRouteTable::Instance().reset();', True),
	('6 reset on proxy toggle', ACCOUNT, 'void Account::watchProxyChanges(', 'if (bool(change.was) != bool(change.now)) {\n\t\t\tMTP::details::WsRouteTable::Instance().reset();', True),
	('6 reset only there', ACCOUNT, None, 'WsRouteTable::Instance().reset();', 1),
	*((f'cmake {name}', CMAKE, None, f'    mtproto/details/mtproto_websocket_{name}\n', True)
		for name in ('logic.cpp', 'logic.h', 'socket.cpp', 'socket.h')),
	*((f'EN {key}', LANG_EN, None, f'\n"{key}" = "', True) for key in LANG_KEYS),
	*((f'RU {key}', LANG_RU, None, f'{{ "{key}", "', True) for key in LANG_KEYS),
)

# (name, path, before, after): each must break at least one rule.
SOURCE_MUTANTS = (
	('F1 pooled socket', CONNECTION_TCP, 'std::make_unique<WebSocketSocket>(', 'WebSocketPool::Acquire<WebSocketSocket>('),
	('F2 ws dropped when tcp offered', SESSION_PRIVATE, 'if (wsDecision.useWs) {\n\t\t\tappendWebSocketConnections', 'if (wsDecision.useWs && !wsDecision.useTcp) {\n\t\t\tappendWebSocketConnections'),
	('F3 silent session fails route', SESSION_PRIVATE, 'const auto was = WsRouteTable::Instance().stalled(', 'const auto was = WsRouteTable::Instance().failed('),
	('F3 failure after payload', SOCKET, 'WsCountsAsRouteFailure(_decoder.received())', 'WsCountsAsRouteFailure(false)'),
	('F4 rare pings', SOCKET, '_pingTimer.callEach(kWsPingInterval / 2);', '_pingTimer.callEach(kWsPingInterval * 4);'),
	('F4 no pong', SOCKET, '\t\tsendFrame(WsOpcode::Pong, payload);\n', ''),
	('F5 media host lost', SESSION_PRIVATE, '.host = WsHost(key.dc, key.media),', '.host = WsHost(key.dc, false),'),
	('TLS errors ignored', SOCKET, '\t\t\t// Strict TLS: never ignored, the handshake fails.\n', '\t\t\t_socket.ignoreSslErrors();\n'),
	('TLS no peer name', SOCKET, '\t_socket.setPeerVerifyName(host);\n', ''),
	('5 buffers before connect', SOCKET, '\t_socket.setProxy(QNetworkProxy(QNetworkProxy::NoProxy));\n', '\t_socket.setProxy(QNetworkProxy(QNetworkProxy::NoProxy));\n\t_socket.setSocketOption(QAbstractSocket::SendBufferSizeSocketOption, kFilesSendBufferSize);\n'),
	('5 buffers on encrypted', SOCKET, '&QAbstractSocket::connected,\n\t\twrap([=] { applyBufferOptions(); })', '&QSslSocket::encrypted,\n\t\twrap([=] { applyBufferOptions(); })'),
	('4 socket records failure', SOCKET, '\t\t_failure = refused ? WsFailure::Refused : WsFailure::Network;\n', '\t\tWsRouteTable::Instance().failed(_endpoint.key, refused, crl::now());\n'),
	('4 every racer fails', SESSION_PRIVATE, 'const auto failure = _wsRound.ended(\n\t\tstatic_cast<TcpConnection*>(i->data.get())->webSocketFailure());', 'const auto failure = static_cast<TcpConnection*>(i->data.get())->webSocketFailure();'),
	('4 timeout not counted', SESSION_PRIVATE, '\t\twsRacerEnded(racer);\n', '\t\tracer->timedOut();\n'),
	('4 round leaks', SESSION_PRIVATE, '\t_connection = nullptr;\n\t_wsRound = WsRound();\n', '\t_connection = nullptr;\n'),
	('ws priority raised', SESSION_PRIVATE, 'const auto priority = ws ? 0 :', 'const auto priority = ws ? 3 :'),
	('1 proxy setting trusted', SESSION_PRIVATE, '!(_options->systemProxy && SystemProxyPresent())', '!_options->systemProxy'),
	('1 lookup before gates', SESSION_PRIVATE, '\t\tconst auto wsAllowed = WsAllowed(', '\t\tconst auto eager = SystemProxyPresent();\n\t\tconst auto wsAllowed = WsAllowed('),
	('1 default proxy counted', SESSION_PRIVATE, '\n\t\t&& (type != QNetworkProxy::DefaultProxy);', ';'),
	('1 lookup on main thread', SESSION, '\toptions.systemProxy = settings.isSystem();', '\toptions.systemProxy = settings.isSystem()\n\t\t&& !QNetworkProxyFactory::systemProxyForQuery().isEmpty();'),
	('4 racers not counted', SESSION_PRIVATE, '\t\t_wsRound.add();\n', ''),
	('4 error leaves round', SESSION_PRIVATE, '\t}\n\twsRacerEnded(connection);\n\tremoveTestConnection(connection);', '\t}\n\tremoveTestConnection(connection);'),
	('4 disconnect leaves round', SESSION_PRIVATE, 'connection) {\n\twsRacerEnded(connection);\n', 'connection) {\n'),
	('4 winner keeps round', SESSION_PRIVATE, '\t_wsRound = WsRound();\n\t// Proxy, cdn', '\t// Proxy, cdn'),
	('4 payload not a win', SOCKET, 'return _decoder.received() ? WsFailure::Worked : _failure;', 'return _failure;'),
	('8 failure through proxy', SESSION_PRIVATE, 'failure == WsFailure::None || !_wsAllowed', 'failure == WsFailure::None'),
	('gate keys destroyer lost', SESSION_PRIVATE, '\t\t\t_instance->isKeysDestroyer())\n', '\t\t\tfalse)\n'),
	('gate cdn lost', SESSION_PRIVATE, 'special || (_currentDcType == DcType::Cdn),', 'special,'),
	('gate system proxy lost', SESSION_PRIVATE, '\n\t\t\t&& !(_options->systemProxy && SystemProxyPresent());', ';'),
	('8 success through proxy', SESSION_PRIVATE, '\tif (!_wsAllowed) {\n\t\treturn;\n\t}\n\tconst auto was = WsRouteTable::Instance().succeeded(', '\tconst auto was = WsRouteTable::Instance().succeeded('),
	('8 stall through proxy', SESSION_PRIVATE, 'if (_connection && _wsAllowed) {', 'if (_connection) {'),
	('8 gate kept between rounds', SESSION_PRIVATE, '\tauto wsDecision = WsDecision();\n\t_wsAllowed = false;\n', '\tauto wsDecision = WsDecision();\n'),
	('handshake floor lost', SESSION_PRIVATE, 'crl::time(wsDecision.wsDelayMs + kWsHandshakeTimeout))', 'crl::time(wsDecision.wsDelayMs))'),
	('reachability keeps routes', INSTANCE, '\t\tWsRouteTable::Instance().reset();\n\t\tconst auto line', '\t\tconst auto line'),
	('6 reset on every change', ACCOUNT, 'if (bool(change.was) != bool(change.now)) {', 'if (true) {'),
	('cmake socket missing', CMAKE, '    mtproto/details/mtproto_websocket_socket.cpp\n', ''),
	('EN key missing', LANG_EN, '"luxury_WebSocketStatusActive" = "', '"luxury_WebSocketStatusActiv" = "'),
	('RU key missing', LANG_RU, '{ "luxury_WebSocketLog", "', '{ "luxury_WebSocketLogs", "'),
)


def broken_rules(files):
	broken = []
	for name, path, scope, needle, expected in SOURCE_RULES:
		text = files[path]
		if scope:
			try:
				text = function(text, scope)
			except ValueError:
				broken.append(name)
				continue
		found = text.count(needle)
		if (bool(found) != expected) if isinstance(expected, bool) else (found != expected):
			broken.append(name)
	return broken


def mixed_endings(data):
	crlf = data.count(b'\r\n')
	return 0 < crlf < data.count(b'\n') or data.count(b'\r') != crlf


def check_sources(read, raw):
	files = {path: crlf(read(path)) for path in {rule[1] for rule in SOURCE_RULES}}
	broken = broken_rules(files)
	assert not broken, f'source rules broken: {broken}'
	for name, path, before, after in SOURCE_MUTANTS:
		assert files[path].count(before) == 1, f'source mutant anchor moved: {name}'
		mutated = dict(files)
		mutated[path] = files[path].replace(before, after)
		assert broken_rules(mutated), f'accepts source mutant: {name}'
	for path in LINE_ENDINGS:
		data = raw(path)
		assert not mixed_endings(data), f'mixed line endings: {path}'
		# Mutant: the first line flipped, as a CRLF-eating edit leaves it.
		line = data.index(b'\n')
		flipped = (data[:line - 1] + data[line:]) if data[line - 1:line] == b'\r' \
			else (data[:line] + b'\r' + data[line:])
		assert mixed_endings(flipped), f'accepts flipped line ending: {path}'
	print(f'ws_transport sources ok: {len(SOURCE_RULES)} rules, '
		f'{len(SOURCE_MUTANTS)} mutants, {len(LINE_ENDINGS)} files')


# raw always reads the checkout: line endings are a property of the real
# files, a read override (text) cannot carry them.
def check(root, read=source, raw=lambda path: (ROOT / path).read_bytes()):
	check_logic(root, read)
	check_sources(read, raw)


if __name__ == '__main__':
	with tempfile.TemporaryDirectory(prefix='ws_transport_') as d:
		check(Path(d))
	print('ws_transport_regressions: ok')
