/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QString>

#include <optional>

namespace Core::Updates {

enum class Channel : uchar;
struct VerifiedUpdate;

struct LegacyUpdate {
	QByteArray uncompressed;
	quint32 version = 0;
	quint64 alphaVersion = 0;
};

// Uses the caller's pinned v1 public key, never a beta-key fallback. Running
// version and legacy-alpha eligibility remain caller policy after authentication.
[[nodiscard]] std::optional<LegacyUpdate> VerifyLegacyUpdate(
	const QByteArray &bytes,
	const char *publicKeyPem,
	Channel buildChannel,
	QString *error = nullptr);

[[nodiscard]] std::optional<QByteArray> DecompressUpdatePayload(
	const VerifiedUpdate &verified,
	QString *error = nullptr);

// Extraction requires a fresh caller-controlled directory. A legacy alpha
// version must be supplied explicitly after v1 authentication and version policy.
[[nodiscard]] bool ExtractUpdateFiles(
	const QByteArray &uncompressed,
	quint64 expectedVersion,
	const QString &directory,
	QString *error = nullptr,
	quint64 expectedLegacyAlphaVersion = 0);

} // namespace Core::Updates
