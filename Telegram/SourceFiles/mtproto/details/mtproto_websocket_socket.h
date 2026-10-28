/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "mtproto/details/mtproto_abstract_socket.h"
#include "mtproto/details/mtproto_websocket_logic.h"
#include "base/timer.h"

#include <QtNetwork/QSslSocket>

namespace MTP::details {

// MTProto over wss://kwsN.web.telegram.org/apiws. TcpConnection keeps its
// obfuscation and framing, every write() becomes one binary frame.
// Opened on demand per connection: no pool, no shared state but the route
// memory, so a burst of connections never waits on anything.
class WebSocketSocket final : public AbstractSocket {
public:
	WebSocketSocket(
		not_null<QThread*> thread,
		WsEndpoint endpoint,
		bool protocolForFiles);

	void connectToHost(const QString &address, int port) override;
	bool isGoodStartNonce(bytes::const_span nonce) override;
	void timedOut() override;
	bool isConnected() override;
	bool hasBytesAvailable() override;
	int64 read(bytes::span buffer) override;
	void write(bytes::const_span prefix, bytes::const_span buffer) override;

	int32 debugState() override;
	QString debugPostfix() const override;

	// What this racer contributes to its round, None once it got bytes.
	[[nodiscard]] WsFailure failure() const;

private:
	void start();
	void applyBufferOptions();
	void sendUpgrade();
	void socketRead();
	void readHandshake();
	void readFrames();
	void socketDisconnected();
	void sendFrame(WsOpcode opcode, std::span<const uint8_t> payload);
	void sendPingIfIdle();
	void handleError(int errorCode, const QString &reason);
	void noteFailure(bool refused);
	void log(const QString &text) const;

	WsEndpoint _endpoint;
	bool _protocolForFiles = false;
	QSslSocket _socket;
	base::Timer _startTimer;
	base::Timer _handshakeTimer;
	base::Timer _pingTimer;
	WsFrameDecoder _decoder;
	QByteArray _head;
	QByteArray _expectedAccept;
	crl::time _startedAt = 0;
	crl::time _lastWriteAt = 0;
	int _generation = 0;
	bool _upgraded = false;
	bool _retried = false;
	bool _failed = false;
	bool _closed = false;
	WsFailure _failure = WsFailure::None;

};

} // namespace MTP::details
