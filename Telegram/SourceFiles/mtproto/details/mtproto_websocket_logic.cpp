/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mtproto/details/mtproto_websocket_logic.h"

#include <algorithm>

namespace MTP::details {
namespace {

[[nodiscard]] char Lower(char ch) {
	return (ch >= 'A' && ch <= 'Z') ? char(ch - 'A' + 'a') : ch;
}

[[nodiscard]] bool EqualNoCase(std::string_view a, std::string_view b) {
	if (a.size() != b.size()) {
		return false;
	}
	for (size_t i = 0; i != a.size(); ++i) {
		if (Lower(a[i]) != Lower(b[i])) {
			return false;
		}
	}
	return true;
}

[[nodiscard]] std::string_view Trim(std::string_view value) {
	while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
		value.remove_prefix(1);
	}
	while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) {
		value.remove_suffix(1);
	}
	return value;
}

} // namespace

std::string WsHost(int dc, bool media) {
	const auto front = (dc == 203) ? 2 : dc;
	return "kws"
		+ std::to_string(front)
		+ (media ? "-1" : "")
		+ ".web.telegram.org";
}

std::string WsPath(bool test) {
	return test ? "/apiws_test" : "/apiws";
}

bool WsDcSupported(int dc, bool test) {
	return test
		? (dc >= 1 && dc <= 3)
		: ((dc >= 1 && dc <= 5) || dc == 203);
}

std::string_view WsDefaultIp(int dc, bool test) {
	if (test) {
		switch (dc) {
		case 1: return "149.154.175.10";
		case 2: return "149.154.167.40";
		case 3: return "149.154.175.117";
		}
		return {};
	}
	switch (dc) {
	case 1: return "149.154.175.50";
	case 2: return "149.154.167.51";
	case 3: return "149.154.175.100";
	case 4: return "149.154.167.91";
	case 5: return "149.154.171.5";
	case 203: return "91.105.192.100";
	}
	return {};
}

std::vector<std::string> WsCandidateIps(
		int dc,
		bool test,
		std::string_view optionIPv4,
		std::string_view optionIPv6) {
	auto result = std::vector<std::string>();
	const auto add = [&](std::string_view ip) {
		if (!ip.empty()
			&& std::find(begin(result), end(result), ip) == end(result)) {
			result.emplace_back(ip);
		}
	};
	add(kWsPinnedIp);
	add(optionIPv4);
	add(WsDefaultIp(dc, test));
	add(optionIPv6);
	return result;
}

bool WsAllowed(
		WsMode mode,
		int dc,
		bool test,
		bool proxy,
		bool systemProxy,
		bool specialDc,
		bool keysDestroyer) {
	return (mode != WsMode::Off)
		&& !proxy
		&& !systemProxy
		&& !specialDc
		&& !keysDestroyer
		&& WsDcSupported(dc, test);
}

std::string WsUpgradeRequest(
		std::string_view host,
		std::string_view path,
		std::string_view key) {
	auto result = std::string();
	result.append("GET ").append(path).append(" HTTP/1.1\r\n");
	result.append("Host: ").append(host).append("\r\n");
	result.append("Upgrade: websocket\r\n");
	result.append("Connection: Upgrade\r\n");
	result.append("Sec-WebSocket-Key: ").append(key).append("\r\n");
	result.append("Sec-WebSocket-Version: 13\r\n");
	result.append("Sec-WebSocket-Protocol: binary\r\n");
	result.append("\r\n");
	return result;
}

WsHandshakeResult WsParseUpgradeResponse(
		std::string_view head,
		std::string_view expectedAccept) {
	auto result = WsHandshakeResult();
	const auto end = head.find("\r\n\r\n");
	if (end == std::string_view::npos) {
		result.status = (head.size() >= kWsMaxHead)
			? WsHandshake::TooLarge
			: WsHandshake::NeedMore;
		return result;
	} else if (end + 4 > kWsMaxHead) {
		result.status = WsHandshake::TooLarge;
		return result;
	}
	result.consumed = end + 4;
	auto rest = head.substr(0, end + 2);
	const auto statusEnd = rest.find("\r\n");
	result.statusLine = std::string(rest.substr(0, statusEnd));
	rest.remove_prefix(statusEnd + 2);

	const auto space = result.statusLine.find(' ');
	const auto code = (space == std::string::npos)
		? std::string()
		: result.statusLine.substr(
			space,
			result.statusLine.find(' ', space + 1) - space);
	if (result.statusLine.rfind("HTTP/1.", 0) != 0 || code != " 101") {
		result.status = WsHandshake::Refused;
		return result;
	}
	auto accepted = false;
	auto binary = false;
	while (!rest.empty()) {
		const auto lineEnd = rest.find("\r\n");
		const auto line = rest.substr(0, lineEnd);
		rest.remove_prefix(lineEnd + 2);
		const auto colon = line.find(':');
		if (colon == std::string_view::npos) {
			continue;
		}
		const auto name = Trim(line.substr(0, colon));
		const auto value = Trim(line.substr(colon + 1));
		if (EqualNoCase(name, "sec-websocket-accept")) {
			accepted = (value == expectedAccept);
		} else if (EqualNoCase(name, "sec-websocket-protocol")) {
			binary = EqualNoCase(value, "binary");
		}
	}
	result.status = (accepted && binary)
		? WsHandshake::Ok
		: WsHandshake::BadAccept;
	return result;
}

std::vector<uint8_t> WsEncodeFrame(
		WsOpcode opcode,
		std::span<const uint8_t> payload,
		const uint8_t mask[4]) {
	const auto maskBit = uint8_t(0x80);
	const auto size = uint64_t(payload.size());
	auto result = std::vector<uint8_t>();
	result.reserve(payload.size() + 14);
	result.push_back((opcode == WsOpcode::Binary)
		? uint8_t(0x82)
		: uint8_t(0x80 | uint8_t(opcode)));
	if (size < 126) {
		result.push_back(uint8_t(maskBit | size));
	} else if (size <= 0xFFFF) {
		result.push_back(uint8_t(maskBit | 126));
		result.push_back(uint8_t(size >> 8));
		result.push_back(uint8_t(size));
	} else {
		result.push_back(uint8_t(maskBit | 127));
		for (auto shift = 56; shift >= 0; shift -= 8) {
			result.push_back(uint8_t(size >> shift));
		}
	}
	result.insert(end(result), mask, mask + 4);
	for (size_t i = 0; i != payload.size(); ++i) {
		result.push_back(uint8_t(payload[i] ^ mask[i % 4]));
	}
	return result;
}

void WsFrameDecoder::feed(std::span<const uint8_t> bytes) {
	if (_closed || _failed) {
		return;
	}
	_in.insert(end(_in), bytes.begin(), bytes.end());
	while (!_closed && !_failed && parseFrame()) {
	}
	if (_inOffset > 0) {
		_in.erase(begin(_in), begin(_in) + _inOffset);
		_inOffset = 0;
	}
}

bool WsFrameDecoder::parseFrame() {
	const auto data = std::span<const uint8_t>(_in).subspan(_inOffset);
	if (data.size() < 2) {
		return false;
	}
	const auto fin = (data[0] & 0x80) != 0;
	const auto opcode = WsOpcode(data[0] & 0x0F);
	const auto masked = (data[1] & 0x80) != 0;
	auto length = uint64_t(data[1] & 0x7F);
	auto header = size_t(2);
	if (data[0] & 0x70) {
		_failed = true;
		return false;
	}
	if (masked) {
		_failed = true;
		return false;
	}
	if (length == 126 || length == 127) {
		const auto bytes = (length == 126) ? size_t(2) : size_t(8);
		if (data.size() < header + bytes) {
			return false;
		}
		length = 0;
		for (size_t i = 0; i != bytes; ++i) {
			length = (length << 8) | data[header + i];
		}
		header += bytes;
	}
	if (length > kWsMaxFrame) {
		_failed = true;
		return false;
	}
	const auto control = (uint8_t(opcode) & 0x08) != 0;
	if (control && (!fin || length > 125)) {
		_failed = true;
		return false;
	}
	if (data.size() - header < length) {
		return false;
	}
	const auto payload = data.subspan(header, size_t(length));
	_inOffset += header + size_t(length);

	const auto append = [&] {
		_payload.insert(end(_payload), payload.begin(), payload.end());
		_received = _received || !payload.empty();
	};
	switch (opcode) {
	case WsOpcode::Cont:
		if (!_fragmented) {
			_failed = true;
			return false;
		}
		append();
		_fragmented = !fin;
		return true;
	case WsOpcode::Binary:
		if (_fragmented) {
			_failed = true;
			return false;
		}
		append();
		_fragmented = !fin;
		return true;
	case WsOpcode::Close:
		_closed = true;
		return false;
	case WsOpcode::Ping:
		_pongs.emplace_back(payload.begin(), payload.end());
		return true;
	case WsOpcode::Pong:
		return true;
	case WsOpcode::Text:
		break;
	}
	_failed = true;
	return false;
}

size_t WsFrameDecoder::available() const {
	return _payload.size() - _payloadOffset;
}

size_t WsFrameDecoder::read(std::span<uint8_t> buffer) {
	const auto count = std::min(buffer.size(), available());
	std::copy_n(begin(_payload) + _payloadOffset, count, buffer.begin());
	_payloadOffset += count;
	if (_payloadOffset == _payload.size()) {
		_payload.clear();
		_payloadOffset = 0;
	}
	return count;
}

std::vector<std::vector<uint8_t>> WsFrameDecoder::takePongs() {
	return std::exchange(_pongs, {});
}

bool WsFrameDecoder::closed() const {
	return _closed;
}

bool WsFrameDecoder::failed() const {
	return _failed;
}

bool WsFrameDecoder::received() const {
	return _received;
}

bool WsPingDue(int64_t lastWriteMs, int64_t nowMs) {
	return nowMs - lastWriteMs >= kWsPingInterval;
}

bool WsCountsAsRouteFailure(bool received) {
	return !received;
}

const char *WsRouteName(WsRoute route) {
	switch (route) {
	case WsRoute::Direct: return "Direct";
	case WsRoute::WebSocket: return "WebSocket";
	case WsRoute::Unknown: break;
	}
	return "Unknown";
}

std::string WsKeyName(WsKey key) {
	return "dc"
		+ std::to_string(key.dc)
		+ (key.media ? " media" : "")
		+ (key.test ? " test" : "");
}

WsRouteTable &WsRouteTable::Instance() {
	static auto result = WsRouteTable();
	return result;
}

WsDecision WsRouteTable::decide(WsKey key, WsMode mode, int64_t nowMs) {
	auto result = WsDecision();
	if (mode == WsMode::Off) {
		return result;
	} else if (mode == WsMode::Always) {
		result.useTcp = false;
		result.useWs = true;
		return result;
	}
	auto lock = std::lock_guard(_mutex);
	const auto i = _states.find(key);
	const auto state = (i != end(_states)) ? i->second : WsRouteState();
	// A bad route only delays WS behind TCP, it never drops a candidate.
	const auto cooling = (nowMs < state.wsCooldownUntil);
	result.useWs = true;
	result.useTcp = (state.route != WsRoute::WebSocket)
		|| cooling
		|| (nowMs >= state.directProbeAt);
	result.wsDelayMs = (state.route == WsRoute::Direct || cooling)
		? kWsDirectHeadStart
		: 0;
	return result;
}

void WsRouteTable::networkFailure(WsRouteState &state, int64_t nowMs) {
	++state.wsFailures;
	const auto backoff = kWsFailBackoffBase
		<< (std::min(state.wsFailures, 6) - 1);
	state.wsCooldownUntil = nowMs + std::min(backoff, kWsFailBackoffMax);
	if (state.route == WsRoute::WebSocket && state.wsFailures >= 2) {
		state.route = WsRoute::Unknown;
	}
}

WsRoute WsRouteTable::succeeded(
		WsKey key,
		bool websocket,
		int64_t nowMs,
		bool tcpOffered) {
	auto lock = std::lock_guard(_mutex);
	auto &state = _states[key];
	const auto was = state.route;
	state.wsFailures = 0;
	if (!websocket) {
		state.route = WsRoute::Direct;
		state.directProbeStep = 0;
		return was;
	}
	state.route = WsRoute::WebSocket;
	state.wsCooldownUntil = 0;
	if (was != WsRoute::WebSocket) {
		state.directProbeStep = kWsDirectProbeFirst;
	} else if (tcpOffered) {
		// Direct was probed and lost again: probe less often.
		state.directProbeStep = std::clamp(
			state.directProbeStep * 2,
			kWsDirectProbeFirst,
			kWsDirectProbeMax);
	} else {
		// Plain reconnect over WS keeps the probe schedule.
		return was;
	}
	state.directProbeAt = nowMs + state.directProbeStep;
	return was;
}

WsRoute WsRouteTable::failed(WsKey key, bool refused, int64_t nowMs) {
	auto lock = std::lock_guard(_mutex);
	auto &state = _states[key];
	const auto was = state.route;
	if (refused) {
		state.wsCooldownUntil = nowMs + kWsRefusedCooldown;
		state.wsFailures = 0;
		if (state.route == WsRoute::WebSocket) {
			state.route = WsRoute::Unknown;
		}
	} else {
		networkFailure(state, nowMs);
	}
	return was;
}

WsRoute WsRouteTable::stalled(
		WsKey key,
		bool websocket,
		[[maybe_unused]] int64_t nowMs) {
	auto lock = std::lock_guard(_mutex);
	auto &state = _states[key];
	const auto was = state.route;
	// The session was established, so this is no route failure: only
	// forget the winner so the next round races both without a delay.
	if (state.route == (websocket ? WsRoute::WebSocket : WsRoute::Direct)) {
		state.route = WsRoute::Unknown;
	}
	return was;
}

WsRoute WsRouteTable::route(WsKey key) const {
	auto lock = std::lock_guard(_mutex);
	const auto i = _states.find(key);
	return (i != end(_states)) ? i->second.route : WsRoute::Unknown;
}

void WsRouteTable::reset() {
	auto lock = std::lock_guard(_mutex);
	_states.clear();
}

auto WsRouteTable::snapshot() const
-> std::vector<std::pair<WsKey, WsRouteState>> {
	auto lock = std::lock_guard(_mutex);
	return { begin(_states), end(_states) };
}

WsLog &WsLog::Instance() {
	static auto result = WsLog();
	return result;
}

void WsLog::add(std::string line) {
	auto lock = std::lock_guard(_mutex);
	_lines.push_back(std::move(line));
	while (_lines.size() > kWsLogLines) {
		_lines.erase(begin(_lines));
	}
	++_revision;
}

std::vector<std::string> WsLog::lines() const {
	auto lock = std::lock_guard(_mutex);
	return _lines;
}

void WsLog::clear() {
	auto lock = std::lock_guard(_mutex);
	_lines.clear();
	++_revision;
}

uint64_t WsLog::revision() const {
	auto lock = std::lock_guard(_mutex);
	return _revision;
}

} // namespace MTP::details
