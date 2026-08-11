/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "settings.h"

enum {
	MaxSelectedItems = 100,

	LocalEncryptIterCount = 4000, // key derivation iteration count
	LocalEncryptNoPwdIterCount = 4, // key derivation iteration count without pwd (not secure anyway)
	LocalEncryptSaltSize = 32, // 256 bit

	AutoSearchTimeout = 900, // 0.9 secs

	PreloadHeightsCount = 3, // when 3 screens to scroll left make a preload request

	SearchPeopleLimit = 20,

	WebPageUserId = 701000,

	UpdateDelayConstPart = 8 * 3600, // 8 hour min time between update check requests
	UpdateDelayRandPart = 8 * 3600, // 8 hour max - min time between update check requests

	WrongPasscodeTimeout = 1500,

	ChoosePeerByDragTimeout = 1000, // 1 second mouse not moved to choose dialog when dragging a file
};

inline const char *cGUIDStr() {
#ifndef OS_MAC_STORE
	static const char *gGuidStr = "{87A94AB0-E370-4cde-98D3-ACC110C59666}";
#else // OS_MAC_STORE
	static const char *gGuidStr = "{E51FB841-8C0B-4EF9-9E9E-5A0078567666}";
#endif // OS_MAC_STORE

	return gGuidStr;
}

static const char *UpdatesPublicKey = "\
-----BEGIN RSA PUBLIC KEY-----\n\
MIICCgKCAgEA0fHaXZpTXYY9vfE6beIbByF/JNU+zhzE1kxpogzdUpDg2VTbAJHW\n\
HwCI9ZU7Gu4GVLgb7dT26NduGk+HNCnnNKddww/wgbD2ORKXSE+6KWIJYESisv1u\n\
TYRDtcGGVdRSP0WJmu8GDxKg7sH37f2oM2zhRiQ11pWNCxj8pzqX7ACLr8rhlKjC\n\
x8cqov9Q5JtREpxe9Aj7s1DSVET4b0nbfPLV6xWJKYlSl8XqOwrtFE96KIi/GK8E\n\
Yjb6WPO5LeN2bZ+wq9QZBc6sPlU7zxSuYLnzdTzyHoV+lLacssgxYhJuqN6vjzGr\n\
soypmldcDjoTvNvrK+KtsC3pLSSuzQLgZFfK9vcJW7L1cwPeBxh/n1HK3bOxNLiK\n\
ucQk87Hg7qH/yu6cnJ8T+zJz/ObGVsbYw+o+6VqMLFU0SPpmibju1Va9wuapqqM9\n\
9xZWBEI7v9Z6y1IQeXKqUDZX0RA/T2e5f6gN8PDANJ4wnQssS0PNGe/JhOiHWTi8\n\
JCig6DKPYBm7bDZb2R70s7mM+2iCQtmFDlKaB1mp6eosO9teD1WgLR2hUrgsO/PT\n\
ktCJ6PRfaIVbUgogEGXHexq9F1yU5amKNYBwJQAQVR2JFbVJSNbDIQGxQK83QD5H\n\
lBi3Hj42R81d8BVg/Zxm8+h9WzmhoN43nQOenMWz2aLGtzsO/b9Xfl8CAwEAAQ==\n\
-----END RSA PUBLIC KEY-----\
";

static const char *UpdatesPublicBetaKey = "\
-----BEGIN RSA PUBLIC KEY-----\n\
MIGJAoGBALWu9GGs0HED7KG7BM73CFZ6o0xufKBRQsdnq3lwA8nFQEvmdu+g/I1j\n\
0LQ+0IQO7GW4jAgzF/4+soPDb6uHQeNFrlVx1JS9DZGhhjZ5rf65yg11nTCIHZCG\n\
w/CVnbwQOw0g5GBwwFV3r0uTTvy44xx8XXxk+Qknu4eBCsmrAFNnAgMBAAE=\n\
-----END RSA PUBLIC KEY-----\
";

#if defined TDESKTOP_API_ID && defined TDESKTOP_API_HASH

constexpr auto ApiId = TDESKTOP_API_ID;
constexpr auto ApiHash = QT_STRINGIFY(TDESKTOP_API_HASH);

#else // TDESKTOP_API_ID && TDESKTOP_API_HASH

// To build your version of Telegram Desktop you're required to provide
// your own 'api_id' and 'api_hash' for the Telegram API access.
//
// How to obtain your 'api_id' and 'api_hash' is described here:
// https://core.telegram.org/api/obtaining_api_id
//
// If you're building the application not for deployment,
// but only for test purposes you can comment out the error below.
//
// This will allow you to use TEST ONLY 'api_id' and 'api_hash' which are
// very limited by the Telegram API server.
//
// Your users will start getting internal server errors on login
// if you deploy an app using those 'api_id' and 'api_hash'.

#error You are required to provide API_ID and API_HASH.

constexpr auto ApiId = 17349;
constexpr auto ApiHash = "344583e45741c457fe1862106095a5eb";

#endif // TDESKTOP_API_ID && TDESKTOP_API_HASH

#if Q_BYTE_ORDER == Q_BIG_ENDIAN
#error "Only little endian is supported!"
#endif // Q_BYTE_ORDER == Q_BIG_ENDIAN

#if (TDESKTOP_ALPHA_VERSION != 0)

// Private key for downloading closed alphas.
#include "../../../DesktopPrivate/alpha_private.h"

#else
static const char *AlphaPrivateKey = "";
#endif

extern QString gKeyFile;
inline const QString &cDataFile() {
	if (!gKeyFile.isEmpty()) return gKeyFile;
	static const QString res(u"data"_q);
	return res;
}

inline const QRegularExpression &cRussianLetters() {
	static QRegularExpression regexp(QString::fromUtf8("[а-яА-ЯёЁ]"));
	return regexp;
}
