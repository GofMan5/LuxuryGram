/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/const_string.h"

#define TDESKTOP_REQUESTED_ALPHA_VERSION (0ULL)

#ifdef TDESKTOP_ALLOW_CLOSED_ALPHA
#define TDESKTOP_ALPHA_VERSION TDESKTOP_REQUESTED_ALPHA_VERSION
#else // TDESKTOP_ALLOW_CLOSED_ALPHA
#define TDESKTOP_ALPHA_VERSION (0ULL)
#endif // TDESKTOP_ALLOW_CLOSED_ALPHA

// used in Updater.cpp and Setup.iss for Windows
constexpr auto AppId = "{72B0BF1B-CBA5-5A13-94AE-8A66FF278E2A}"_cs;
constexpr auto AppNameOld = "AyuGram for Windows"_cs;
constexpr auto AppName = "LuxuryGram Desktop"_cs;
constexpr auto AppFile = "LuxuryGram"_cs;
// Keep AppVersion on the upstream line for storage and update compatibility.
constexpr auto AppVersion = 7001001;
constexpr auto AppVersionStr = "7.1.1";
// Product version is bumped only for LuxuryGram releases.
constexpr auto LuxuryVersionStr = "1.0.4";
// Update packages are compared by this counter instead of the upstream
// AppVersion, so syncing Telegram Desktop never looks like an update.
// major * 1'000'000 + minor * 1'000 + patch
constexpr auto LuxuryUpdateVersion = 1'000'004;
constexpr auto AppBetaVersion = false;
constexpr auto AppAlphaVersion = TDESKTOP_ALPHA_VERSION;
