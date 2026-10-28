"""Native WebSocket MTProto transport: framing, upgrade and route memory.

docs/ws-transport.md. The client is not built here, so:
- mtproto_websocket_logic.{h,cpp} (std only) are compiled with -Wall
  -Wextra -Werror and run against the RFC 6455 rules, the kws endpoint
  map for dc 1-5, 203 and test dcs, and the per-dc route table, including
  the field log of the reference proxy: no pool or per-dc cap (F1), a bad
  route never drops a candidate (F2), no idled socket (F3), idle pings and
  no route failure after the session was established (F4), built-in
  endpoints for every dc (F5);
- the Qt half is checked as source: the socket is registered, created
  only for marked connections, never ignores TLS errors, pings when idle,
  records failures only before the first payload; SessionPrivate races it
  only without a proxy; the setting is persisted, has EN and RU strings.
Every check is also run against a mutant it must reject.

Bounded source check; not a native acceptance.
"""
from pathlib import Path
import re
import tempfile
from library_regressions import CXX, ROOT, STATIC, run, source, standalone

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
		"HTTP/1.1 101 OK\r\nSec-WebSocket-Accept: wrong=\r\n"
		"Sec-WebSocket-Protocol: binary\r\n\r\n",
		accept).status == WsHandshake::BadAccept);
	EXPECT(WsParseUpgradeResponse(
		"HTTP/1.1 101 OK\r\nSec-WebSocket-Accept: "
		"s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n\r\n",
		accept).status == WsHandshake::BadAccept);
	EXPECT(WsParseUpgradeResponse(std::string(9000, 'a'), accept).status
		== WsHandshake::TooLarge);
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
	masked.feed({ 0x82, 0x81, 1, 2, 3, 4, 5 });
	EXPECT(masked.failed() && masked.available() == 0);

	auto text = WsFrameDecoder();
	text.feed(ServerFrame(0x81, Bytes("t")));
	EXPECT(text.failed());

	auto huge = WsFrameDecoder();
	huge.feed({ 0x82, 127, 0, 0, 0, 0, 0x01, 0, 0, 1 });
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
	EXPECT(table.decide(key, WsMode::Auto, now).wsDelayMs == 0);
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
	(LOGIC_CPP, '\tresult.useWs = true;\n\tresult.useTcp = (state.route',
		'\tresult.useWs = (++_states[key].wsFailures < 4);\n\tresult.useTcp = (state.route'),
	# F2: a bad route drops WS instead of delaying it.
	(LOGIC_CPP, '\tresult.useWs = true;\n\tresult.useTcp = (state.route',
		'\tresult.useWs = !cooling;\n\tresult.useTcp = (state.route'),
	# F2: a cooling WS route leaves no TCP candidate.
	(LOGIC_CPP, '|| cooling\n', '\n'),
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
)


def crlf(text):
	return text.replace('\r\n', '\n')


def build(root, name, files):
	tree = root / name
	for path, text in files.items():
		target = tree / path
		target.parent.mkdir(parents=True, exist_ok=True)
		target.write_text(text, encoding='utf-8', newline='\n')
	main = tree / 'main.cpp'
	main.write_text(MAIN, encoding='utf-8', newline='\n')
	exe = tree / 'main.exe'
	run([CXX, '-std=c++20', '-O0', '-Wall', '-Wextra', '-Werror', '-I', str(tree), str(main), *STATIC, '-o', str(exe)], tree)
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
			exe = build(root, f'ws_mutant_{index}', mutated)
		except AssertionError as error:
			raise AssertionError(f'mutant does not compile: {after!r}\n{error}')
		try:
			run([str(exe)], root)
		except AssertionError:
			continue
		raise AssertionError(f'accepts mutant: {before!r} -> {after!r}')


def check(root, read=source):
	check_logic(root, read)


if __name__ == '__main__':
	with tempfile.TemporaryDirectory(prefix='ws_transport_') as d:
		check(Path(d))
	print('ws_transport_regressions: ok')
