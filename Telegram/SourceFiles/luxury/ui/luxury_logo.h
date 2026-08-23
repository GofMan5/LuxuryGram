// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

#define ICON(name, value) const auto name##_ICON = QStringLiteral(value)

namespace LuxuryAssets {

ICON(DEFAULT, "default");
ICON(ALT, "alt");
ICON(DISCORD, "discord");
ICON(SPOTIFY, "spotify");
ICON(EXTERA, "extera");
ICON(NOTHING, "nothing");
ICON(BARD, "bard");
ICON(YAPLUS, "yaplus");
ICON(WIN95, "win95");
ICON(CHIBI, "chibi");
ICON(CHIBI2, "chibi2");
ICON(EXTERA2, "extera2");

bool isValidAppIcon(const QString &name);

// Writes the selected .ico next to the settings, returns true only when the
// file actually changed: refreshing the shell after it is expensive.
bool loadAppIco();

QString appIcoPath();

QImage loadPreview(const QString& name);

QString currentAppLogoName();
QImage currentAppLogo();
QImage currentAppLogoPad();

// Pushes the icon named in the settings out to the shell, the window and the
// tray. Both the picker and a settings reset change that name without touching
// anything that draws it.
void applyAppIcon();

}
