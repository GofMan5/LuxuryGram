/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mtproto/details/mtproto_websocket_socket.h"

#include "base/invoke_queued.h"
#include "base/random.h"

#include <QtCore/QCryptographicHash>
#include <QtCore/QPointer>
#include <QtCore/QTime>
#include <QtCore/QtEndian>

namespace MTP::details {
namespace {

// Local codes, only for logging and the refused/network split.
constexpr auto kWsRefusedCode = -1001;
constexpr auto kWsProtocolCode = -1002;
constexpr auto kWsTimeoutCode = -1003;

[[nodiscard]] std::span<const uint8_t> AsUnsigned(const QByteArray &bytes) {
	return {
		reinterpret_cast<const uint8_t*>(bytes.constData()),
		size_t(bytes.size()) };
}

} // namespace

WebSocketSocket::WebSocketSocket(
	not_null<QThread*> thread,
	WsEndpoint endpoint,
	bool protocolForFiles)
: AbstractSocket(thread)
, _endpoint(std::move(endpoint))
, _startTimer(thread, [=] { start(); })
, _handshakeTimer(thread, [=] {
	handleError(kWsTimeoutCode, u"handshake timeout"_q);
})
, _pingTimer(thread, [=] { sendPingIfIdle(); }) {
	_socket.moveToThread(thread);
	_socket.setProxy(QNetworkProxy(QNetworkProxy::NoProxy));
	if (protocolForFiles) {
		_socket.setSocketOption(
			QAbstractSocket::SendBufferSizeSocketOption,
			kFilesSendBufferSize);
		_socket.setSocketOption(
			QAbstractSocket::ReceiveBufferSizeSocketOption,
			kFilesReceiveBufferSize);
	}
	// Signals of an aborted attempt (the media host retry) carry an old
	// generation and are dropped.
	const auto wrap = [&](auto handler) {
		return [=](auto &&...args) {
			const auto generation = _generation;
			InvokeQueued(this, [=] {
				if (generation == _generation) {
					handler(args...);
				}
			});
		};
	};
	using Error = QAbstractSocket::SocketError;
	connect(
		&_socket,
		&QSslSocket::encrypted,
		wrap([=] { sendUpgrade(); }));
	connect(
		&_socket,
		&QAbstractSocket::disconnected,
		wrap([=] { socketDisconnected(); }));
	connect(
		&_socket,
		&QIODevice::readyRead,
		wrap([=] { socketRead(); }));
	connect(
		&_socket,
		&QAbstractSocket::errorOccurred,
		wrap([=](Error e) { handleError(e, _socket.errorString()); }));
	connect(
		&_socket,
		qOverload<const QList<QSslError>&>(&QSslSocket::sslErrors),
		wrap([=](const QList<QSslError> &errors) {
			auto list = QStringList();
			for (const auto &error : errors) {
				list.push_back(error.errorString());
			}
			// Strict TLS: never ignored, the handshake fails.
			handleError(-1, u"TLS: "_q + list.join(u"; "_q));
		}));
}

void WebSocketSocket::connectToHost(const QString &address, int port) {
	// TcpConnection passes the endpoint ip and 443 back to us.
	if (_endpoint.delayMs > 0) {
		log(u"wait %1ms"_q.arg(_endpoint.delayMs));
		_startTimer.callOnce(_endpoint.delayMs);
	} else {
		start();
	}
}

void WebSocketSocket::start() {
	const auto host = QString::fromStdString(_endpoint.host);
	log(u"ws %1 sni=%2"_q.arg(QString::fromStdString(_endpoint.ip), host));
	_startedAt = crl::now();
	_handshakeTimer.callOnce(kWsHandshakeTimeout);
	_socket.setPeerVerifyName(host);
	_socket.connectToHostEncrypted(
		QString::fromStdString(_endpoint.ip),
		quint16(kWsPort),
		host);
}

void WebSocketSocket::sendUpgrade() {
	auto random = QByteArray(16, Qt::Uninitialized);
	base::RandomFill(random.data(), size_t(random.size()));
	const auto key = random.toBase64();
	_expectedAccept = QCryptographicHash::hash(
		key + QByteArray(kWsGuid.data(), qsizetype(kWsGuid.size())),
		QCryptographicHash::Sha1).toBase64();
	const auto request = WsUpgradeRequest(
		_endpoint.host,
		_endpoint.path,
		std::string_view(key.constData(), size_t(key.size())));
	_socket.write(request.data(), qint64(request.size()));
	_lastWriteAt = crl::now();
}

void WebSocketSocket::socketRead() {
	if (_failed || _closed) {
		_socket.readAll();
	} else if (!_upgraded) {
		readHandshake();
	} else {
		readFrames();
	}
}

void WebSocketSocket::readHandshake() {
	_head += _socket.readAll();
	const auto result = WsParseUpgradeResponse(
		std::string_view(_head.constData(), size_t(_head.size())),
		std::string_view(
			_expectedAccept.constData(),
			size_t(_expectedAccept.size())));
	const auto status = QString::fromStdString(result.statusLine);
	switch (result.status) {
	case WsHandshake::NeedMore: return;
	case WsHandshake::Ok: break;
	case WsHandshake::Refused: {
		// Some fronts reject the media "-1" host, try the plain one once.
		const auto suffix = _endpoint.host.find("-1.");
		if (_endpoint.key.media
			&& suffix != std::string::npos
			&& !_retried) {
			_retried = true;
			log(u"refused (%1), retry without -1"_q.arg(status));
			_endpoint.host.erase(suffix, 2);
			_handshakeTimer.cancel();
			_head.clear();
			++_generation;
			_socket.abort();
			start();
		} else {
			handleError(kWsRefusedCode, u"refused (%1)"_q.arg(status));
		}
	} return;
	case WsHandshake::BadAccept:
		handleError(kWsProtocolCode, u"bad accept (%1)"_q.arg(status));
		return;
	case WsHandshake::TooLarge:
		handleError(kWsProtocolCode, u"upgrade response too large"_q);
		return;
	}
	_handshakeTimer.cancel();
	_upgraded = true;
	log(u"upgraded in %1ms"_q.arg(crl::now() - _startedAt));
	const auto rest = _head.mid(qsizetype(result.consumed));
	_head = QByteArray();
	_decoder.feed(AsUnsigned(rest));
	// F4: idle sessions get pinged before the server drops them.
	_pingTimer.callEach(kWsPingInterval / 2);

	// Any fire() may destroy us through TcpConnection::disconnectFromServer.
	const auto weak = QPointer<QObject>(static_cast<QObject*>(this));
	_connected.fire({});
	if (weak) {
		readFrames();
	}
}

void WebSocketSocket::readFrames() {
	_decoder.feed(AsUnsigned(_socket.readAll()));
	for (const auto &payload : _decoder.takePongs()) {
		sendFrame(WsOpcode::Pong, payload);
	}
	if (_decoder.failed()) {
		handleError(kWsProtocolCode, u"bad frame"_q);
		return;
	}
	const auto weak = QPointer<QObject>(static_cast<QObject*>(this));
	if (_decoder.available() > 0) {
		_readyRead.fire({});
		if (!weak) {
			return;
		}
	}
	if (_decoder.closed() && !_closed && !_failed) {
		_closed = true;
		_pingTimer.cancel();
		sendFrame(WsOpcode::Close, {});
		log(u"closed by server"_q);
		recordFailure(false, u"closed"_q);
		_disconnected.fire({});
	}
}

void WebSocketSocket::socketDisconnected() {
	if (!_upgraded) {
		handleError(QAbstractSocket::RemoteHostClosedError, u"closed"_q);
		return;
	} else if (_closed || _failed) {
		return;
	}
	_closed = true;
	_pingTimer.cancel();
	log(u"disconnected"_q);
	// F4: after the first MTProto bytes this only reconnects.
	recordFailure(false, u"disconnected"_q);
	_disconnected.fire({});
}

void WebSocketSocket::sendFrame(
		WsOpcode opcode,
		std::span<const uint8_t> payload) {
	uint8_t mask[4];
	base::RandomFill(mask, sizeof(mask));
	const auto frame = WsEncodeFrame(opcode, payload, mask);
	_socket.write(
		reinterpret_cast<const char*>(frame.data()),
		qint64(frame.size()));
	_lastWriteAt = crl::now();
}

void WebSocketSocket::sendPingIfIdle() {
	if (_upgraded
		&& !_closed
		&& !_failed
		&& WsPingDue(_lastWriteAt, crl::now())) {
		sendFrame(WsOpcode::Ping, {});
	}
}

bool WebSocketSocket::isGoodStartNonce(bytes::const_span nonce) {
	Expects(nonce.size() >= 2 * sizeof(uint32));

	const auto bytes = nonce.data();
	const auto zero = gsl::to_integer<uchar>(nonce.front());
	const auto first = qFromUnaligned<uint32>(bytes);
	const auto second = qFromUnaligned<uint32>(bytes + sizeof(uint32));
	const auto reserved01 = 0x000000EFU;
	const auto reserved11 = 0x44414548U;
	const auto reserved12 = 0x54534F50U;
	const auto reserved13 = 0x20544547U;
	const auto reserved14 = 0xEEEEEEEEU;
	const auto reserved15 = 0xDDDDDDDDU;
	const auto reserved16 = 0x02010316U;
	const auto reserved21 = 0x00000000U;
	return (zero != reserved01)
		&& (first != reserved11)
		&& (first != reserved12)
		&& (first != reserved13)
		&& (first != reserved14)
		&& (first != reserved15)
		&& (first != reserved16)
		&& (second != reserved21);
}

void WebSocketSocket::timedOut() {
	// SessionPrivate gave up on every racer: a started WS that never
	// answered is a network failure. A delayed one that never started is not.
	if (_startedAt && !_failed) {
		recordFailure(false, u"connect timeout"_q);
	}
}

bool WebSocketSocket::isConnected() {
	return _upgraded
		&& (_socket.state() == QAbstractSocket::ConnectedState);
}

bool WebSocketSocket::hasBytesAvailable() {
	return _decoder.available() > 0;
}

int64 WebSocketSocket::read(bytes::span buffer) {
	return int64(_decoder.read({
		reinterpret_cast<uint8_t*>(buffer.data()),
		size_t(buffer.size()) }));
}

void WebSocketSocket::write(
		bytes::const_span prefix,
		bytes::const_span buffer) {
	Expects(!buffer.empty());

	if (!_upgraded || _closed || _failed) {
		return;
	}
	auto payload = std::vector<uint8_t>(prefix.size() + buffer.size());
	if (!prefix.empty()) {
		memcpy(payload.data(), prefix.data(), prefix.size());
	}
	memcpy(payload.data() + prefix.size(), buffer.data(), buffer.size());
	sendFrame(WsOpcode::Binary, payload);
}

int32 WebSocketSocket::debugState() {
	return _socket.state();
}

QString WebSocketSocket::debugPostfix() const {
	return u"_ws"_q;
}

void WebSocketSocket::handleError(int errorCode, const QString &reason) {
	if (_failed || _closed) {
		return;
	}
	_failed = true;
	_startTimer.cancel();
	_handshakeTimer.cancel();
	_pingTimer.cancel();
	logError(errorCode, reason);
	log(reason);
	recordFailure(errorCode == kWsRefusedCode, reason);
	_error.fire({});
}

void WebSocketSocket::recordFailure(bool refused, const QString &reason) {
	if (_recorded) {
		return;
	}
	_recorded = true;
	// F4: only a failure before the first MTProto byte marks the route.
	if (!WsCountsAsRouteFailure(_decoder.received())) {
		return;
	}
	auto &table = WsRouteTable::Instance();
	const auto was = table.failed(_endpoint.key, refused, crl::now());
	const auto now = table.route(_endpoint.key);
	if (was != now) {
		log(u"route %1 -> %2 (%3)"_q.arg(
			QString::fromLatin1(WsRouteName(was)),
			QString::fromLatin1(WsRouteName(now)),
			reason));
	}
}

void WebSocketSocket::log(const QString &text) const {
	// Never keys or payload: endpoint names and reasons only.
	const auto line = QTime::currentTime().toString(u"HH:mm:ss"_q)
		+ QChar(' ')
		+ QString::fromStdString(WsKeyName(_endpoint.key))
		+ QChar(' ')
		+ text;
	WsLog::Instance().add(line.toStdString());
	LOG(("WS: %1").arg(line));
}

} // namespace MTP::details
