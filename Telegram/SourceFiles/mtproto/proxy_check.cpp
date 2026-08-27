/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mtproto/proxy_check.h"

#include "base/call_delayed.h"
#include "mtproto/facade.h"
#include "mtproto/mtproto_dc_options.h"

namespace MTP {
namespace {

// A checker is not owned by a SessionPrivate, so nothing drives timedOut() for
// it. A peer that completes the TCP handshake and then never answers the fake
// req_pq emits neither connected() nor error(), and the caller is left in its
// "checking" state for the rest of the process's life. The target is this
// account's own home DC (mainDcId below), so two accounts probe different
// endpoints through the same proxy -- which is why one account can hang here
// while another resolves normally.
//
// Same budget ProxyRotationManager already uses for its own checks.
constexpr auto kCheckTimeout = 20 * crl::time(1000);

} // namespace

using Connection = details::AbstractConnection;

void ResetProxyCheckers(
		ProxyCheckConnection &v4,
		ProxyCheckConnection &v6) {
	v4 = nullptr;
	v6 = nullptr;
}

void DropProxyChecker(
		ProxyCheckConnection &v4,
		ProxyCheckConnection &v6,
		not_null<Connection*> raw) {
	if (v4.get() == raw) {
		v4 = nullptr;
	} else if (v6.get() == raw) {
		v6 = nullptr;
	}
}

bool HasProxyCheckers(
		const ProxyCheckConnection &v4,
		const ProxyCheckConnection &v6) {
	return v4 || v6;
}

void StartProxyCheck(
		not_null<Instance*> mtproto,
		const ProxyData &proxy,
		bool tryIPv6,
		ProxyCheckConnection &v4,
		ProxyCheckConnection &v6,
		Fn<void(Connection *raw, int ping)> done,
		Fn<void(Connection *raw)> fail) {
	using Variants = DcOptions::Variants;

	ResetProxyCheckers(v4, v6);
	if (proxy.type == ProxyData::Type::Web) {
		return;
	}
	const auto connType = (proxy.type == ProxyData::Type::Http)
		? Variants::Http
		: Variants::Tcp;
	const auto dcId = mtproto->mainDcId();
	const auto setup = [&](ProxyCheckConnection &checker, const bytes::vector &secret) {
		checker = Connection::Create(
			mtproto,
			connType,
			QThread::currentThread(),
			secret,
			proxy);
		const auto raw = checker.get();
		raw->connect(raw, &Connection::connected, [=] {
			if (done) {
				done(raw, raw->pingTime());
			}
		});
		const auto failed = [=] {
			if (fail) {
				fail(raw);
			}
		};
		raw->connect(raw, &Connection::disconnected, failed);
		raw->connect(raw, &Connection::error, failed);
		// Guarded by a QPointer, so it is dropped with the connection, and every
		// caller's fail() already ignores a check that finished.
		base::call_delayed(kCheckTimeout, raw, failed);
	};
	if (proxy.type == ProxyData::Type::Mtproto) {
		const auto secret = proxy.secretFromMtprotoPassword();
		setup(v4, secret);
		v4->connectToServer(
			proxy.host,
			proxy.port,
			secret,
			dcId,
			false);
		return;
	}
	const auto options = mtproto->dcOptions().lookup(
		dcId,
		DcType::Regular,
		true);
	const auto tryConnect = [&](ProxyCheckConnection &checker, Variants::Address address) {
		const auto &list = options.data[address][connType];
		if (list.empty() || ((address == Variants::IPv6) && !tryIPv6)) {
			checker = nullptr;
			return;
		}
		const auto &endpoint = list.front();
		setup(checker, endpoint.secret);
		checker->connectToServer(
			QString::fromStdString(endpoint.ip),
			endpoint.port,
			endpoint.secret,
			dcId,
			false);
	};
	tryConnect(v4, Variants::IPv4);
	tryConnect(v6, Variants::IPv6);
}

} // namespace MTP
