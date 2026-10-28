/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

// Qt-free part of the WebSocket MTProto transport: endpoints, the HTTP
// upgrade, RFC 6455 framing and the per-dc route memory. Compiled by
// tools/tests/ws_transport_regressions.py with plain g++.

#include <compare>
#include <cstdint>
#include <map>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace MTP::details {

inline constexpr int64_t kWsDirectHeadStart = 2000;
inline constexpr int64_t kWsRefusedCooldown = 30 * 60 * 1000;
inline constexpr int64_t kWsFailBackoffBase = 30 * 1000;
inline constexpr int64_t kWsFailBackoffMax = 15 * 60 * 1000;
inline constexpr int64_t kWsDirectProbeFirst = 5 * 60 * 1000;
inline constexpr int64_t kWsDirectProbeMax = 60 * 60 * 1000;
inline constexpr int64_t kWsHandshakeTimeout = 6000;
inline constexpr int64_t kWsPingInterval = 30000;
inline constexpr size_t kWsMaxHead = 8192;
inline constexpr uint64_t kWsMaxFrame = 16 * 1024 * 1024;
inline constexpr size_t kWsLogLines = 200;
inline constexpr std::string_view kWsGuid
	= "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

// kws front that serves every dc by SNI.
inline constexpr std::string_view kWsPinnedIp = "149.154.167.220";
inline constexpr int kWsPort = 443;

enum class WsMode {
	Off = 0,
	Auto = 1,
	Always = 2,
};

[[nodiscard]] std::string WsHost(int dc, bool media);
[[nodiscard]] std::string WsPath(bool test);

// Production dc 1..5 and 203 have kws fronts, test dc 1..3 use /apiws_test.
[[nodiscard]] bool WsDcSupported(int dc, bool test);

// Built-in DC addresses, used when DcOptions has no IPv4 for the dc.
[[nodiscard]] std::string_view WsDefaultIp(int dc, bool test);

// Pinned front, the DcOptions IPv4, the built-in IP, then IPv6.
// Empty entries are skipped and duplicates dropped; never empty.
[[nodiscard]] std::vector<std::string> WsCandidateIps(
	int dc,
	bool test,
	std::string_view optionIPv4,
	std::string_view optionIPv6);

// Any proxy, special or cdn dc, keys destroyer or Off mode skip WS.
[[nodiscard]] bool WsAllowed(
	WsMode mode,
	int dc,
	bool test,
	bool proxy,
	bool systemProxy,
	bool specialDc,
	bool keysDestroyer);

[[nodiscard]] std::string WsUpgradeRequest(
	std::string_view host,
	std::string_view path,
	std::string_view key);

enum class WsHandshake {
	Ok,
	NeedMore,
	Refused,
	BadAccept,
	TooLarge,
};

struct WsHandshakeResult {
	WsHandshake status = WsHandshake::NeedMore;
	size_t consumed = 0;
	std::string statusLine;
};

[[nodiscard]] WsHandshakeResult WsParseUpgradeResponse(
	std::string_view head,
	std::string_view expectedAccept);

enum class WsOpcode : uint8_t {
	Cont = 0,
	Text = 1,
	Binary = 2,
	Close = 8,
	Ping = 9,
	Pong = 10,
};

[[nodiscard]] std::vector<uint8_t> WsEncodeFrame(
	WsOpcode opcode,
	std::span<const uint8_t> payload,
	const uint8_t mask[4]);

// Server frames: unmasked binary stream, fragments allowed, control frames
// handled. Text frames, masked frames and oversized frames are errors.
class WsFrameDecoder final {
public:
	void feed(std::span<const uint8_t> bytes);

	[[nodiscard]] size_t available() const;
	size_t read(std::span<uint8_t> buffer);
	[[nodiscard]] std::vector<std::vector<uint8_t>> takePongs();

	[[nodiscard]] bool closed() const;
	[[nodiscard]] bool failed() const;

	// Any payload byte arrived: the session was established.
	[[nodiscard]] bool received() const;

private:
	bool parseFrame();

	std::vector<uint8_t> _in;
	size_t _inOffset = 0;
	std::vector<uint8_t> _payload;
	size_t _payloadOffset = 0;
	std::vector<std::vector<uint8_t>> _pongs;
	bool _fragmented = false;
	bool _closed = false;
	bool _failed = false;
	bool _received = false;

};

// Idle sessions get a ping before the server drops them.
[[nodiscard]] bool WsPingDue(int64_t lastWriteMs, int64_t nowMs);

// Only a failure before the first payload byte marks the route bad.
[[nodiscard]] bool WsCountsAsRouteFailure(bool received);

struct WsKey {
	int dc = 0;
	bool media = false;
	bool test = false;

	friend auto operator<=>(const WsKey&, const WsKey&) = default;
};

struct WsEndpoint {
	std::string ip;
	std::string host;
	std::string path;
	WsKey key;
	bool tcpOffered = false;
	int64_t delayMs = 0;
};

enum class WsRoute {
	Unknown,
	Direct,
	WebSocket,
};

[[nodiscard]] const char *WsRouteName(WsRoute route);
[[nodiscard]] std::string WsKeyName(WsKey key);

struct WsDecision {
	bool useTcp = true;
	bool useWs = false;
	int64_t wsDelayMs = 0;
};

struct WsRouteState {
	WsRoute route = WsRoute::Unknown;
	int wsFailures = 0;
	int64_t wsCooldownUntil = 0;
	int64_t directProbeAt = 0;
	int64_t directProbeStep = 0;
};

// Transitions return the route before the change, for logging.
class WsRouteTable final {
public:
	static WsRouteTable &Instance();

	[[nodiscard]] WsDecision decide(WsKey key, WsMode mode, int64_t nowMs);
	WsRoute succeeded(
		WsKey key,
		bool websocket,
		int64_t nowMs,
		bool tcpOffered = false);
	WsRoute failed(WsKey key, bool refused, int64_t nowMs);
	WsRoute stalled(WsKey key, bool websocket, int64_t nowMs);
	[[nodiscard]] WsRoute route(WsKey key) const;
	void reset();
	[[nodiscard]] std::vector<std::pair<WsKey, WsRouteState>> snapshot() const;

private:
	void networkFailure(WsRouteState &state, int64_t nowMs);

	mutable std::mutex _mutex;
	std::map<WsKey, WsRouteState> _states;

};

// Lines are prefixed with a timestamp by the caller. No keys or payload.
class WsLog final {
public:
	static WsLog &Instance();

	void add(std::string line);
	[[nodiscard]] std::vector<std::string> lines() const;
	void clear();
	[[nodiscard]] uint64_t revision() const;

private:
	mutable std::mutex _mutex;
	std::vector<std::string> _lines;
	uint64_t _revision = 0;

};

} // namespace MTP::details
