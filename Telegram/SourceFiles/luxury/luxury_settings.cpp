// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "luxury/luxury_settings.h"
#include "lang/lang_instance.h"

#include "lang_auto.h"
#include "tray.h"
#include "luxury/libs/json.hpp"
#include "luxury/libs/json_ext.hpp"
#include "luxury/luxury_ui_settings.h"
#include "luxury/luxury_worker.h"
#include "luxury/features/streamer_mode/streamer_mode.h"
#include "luxury/ui/luxury_logo.h"
#include "luxury/utils/telegram_helpers.h"
#include "base/debug_log.h"
#include "base/timer.h"
#include "core/application.h"
#include "crl/crl_on_main.h"
#include "features/filters/filters_cache_controller.h"
#include "features/translator/luxury_translator.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "platform/platform_translate_provider.h"
#include "rpl/combine.h"
#include "window/window_controller.h"
#include "window/themes/window_themes_embedded.h"

#include <QApplication>
#include <QFile>
#include <QSaveFile>
#include <QThread>

#include <charconv>

using json = nlohmann::json;

NLOHMANN_JSON_SERIALIZE_ENUM(PeerIdDisplay, {
	{PeerIdDisplay::Hidden, 0},
	{PeerIdDisplay::TelegramApi, 1},
	{PeerIdDisplay::BotApi, 2},
})

NLOHMANN_JSON_SERIALIZE_ENUM(ChannelBottomButton, {
	{ChannelBottomButton::Hidden, 0},
	{ChannelBottomButton::MuteUnmute, 1},
	{ChannelBottomButton::DiscussWithFallback, 2},
})

NLOHMANN_JSON_SERIALIZE_ENUM(ContextMenuVisibility, {
	{ContextMenuVisibility::Hidden, 0},
	{ContextMenuVisibility::Visible, 1},
	{ContextMenuVisibility::VisibleWithModifier, 2},
})

NLOHMANN_JSON_SERIALIZE_ENUM(TranslationProvider, {
	{TranslationProvider::Telegram, "telegram"},
	{TranslationProvider::Google, "google"},
	{TranslationProvider::Yandex, "yandex"},
	{TranslationProvider::Native, "native"},
})

NLOHMANN_JSON_SERIALIZE_ENUM(SendWithoutSoundOption, {
	{SendWithoutSoundOption::Never, 0},
	{SendWithoutSoundOption::InGhostMode, 1},
	{SendWithoutSoundOption::Always, 2},
})

namespace {

constexpr auto kMaxSettingsBytes = 4 * 1024 * 1024;
// ponytail: desktop state is bounded; raise only for a real 64-account profile.
constexpr auto kMaxGhostAccounts = std::size_t(64);
constexpr auto kMaxShadowBanIds = std::size_t(65'536);
// Watching a chat costs traffic and disk on every message it receives, so a set
// this large is already past any use for it -- the cap is here to stop a
// hand-edited settings file from turning into an unbounded read.
constexpr auto kMaxWatchedDialogs = std::size_t(4'096);
constexpr auto kMaxMarkLength = 64;
constexpr auto kMaxFontFamilyLength = 256;
constexpr auto kMaxThemeTitleLength = 512;

QString getSettingsPath() {
	return cWorkingDir() + u"tdata/luxury_settings.json"_q;
}

constexpr auto kSaveDelay = crl::time(500);
bool SettingsWriteBlocked = false;

void writeSettings() {
	if (SettingsWriteBlocked) {
		LOG(("LuxuryGramSettings: not overwriting an unrecovered settings file"));
		return;
	}
	json p = LuxurySettings::getInstance();
	const auto data = QByteArray::fromStdString(p.dump(4));
	QSaveFile file(getSettingsPath());
	if (!file.open(QIODevice::WriteOnly)
		|| file.write(data) != data.size()
		|| !file.commit()) {
		LOG(("LuxuryGramSettings: failed to save settings file"));
	}
}

// from_json() reads two hundred keys through j.value(), which throws on a type
// mismatch rather than falling back to the default. So one hand-edited or
// half-written value used to abort the whole load, leaving defaults in memory --
// which the next save() then wrote over the user's file. Drop the mismatched
// keys instead, so a broken value costs that one setting.
//
// The expected shape comes from a default-constructed instance, so it cannot go
// stale as settings are added. Dynamic account IDs share the ghost shape, not
// the empty default account map. Report repairs so load() preserves the original.
bool dropMismatchedKeys(json &p, const json &shape, const QString &path) {
	if (!p.is_object() || !shape.is_object()) {
		return false;
	}
	const auto ghostAccounts = (path == u"ghostModeSettings."_q);
	const auto accountShape = ghostAccounts
		? json(GhostModeAccountSettings())
		: json();
	auto modified = false;
	for (auto i = p.begin(); i != p.end();) {
		const auto known = shape.find(i.key());
		const auto name = path + QString::fromStdString(i.key());
		if (ghostAccounts) {
			const auto &key = i.key();
			auto id = uint64(0);
			const auto [end, error] = std::from_chars(
				key.data(),
				key.data() + key.size(),
				id);
			if (error != std::errc()
				|| end != key.data() + key.size()
				|| !i->is_object()) {
				LOG(("LuxuryGramSettings: dropping %1, invalid ghost account").arg(name));
				i = p.erase(i);
				modified = true;
			} else {
				modified |= dropMismatchedKeys(i.value(), accountShape, name + '.');
				++i;
			}
		} else if (known == shape.end()) {
			++i; // A key from a newer or older build; from_json ignores it.
		} else if (known->is_object() && i->is_object()) {
			modified |= dropMismatchedKeys(i.value(), *known, name + '.');
			++i;
		} else if (known->type() == i->type()
			|| (known->is_number() && i->is_number())
			|| (i.key() == "sendWithoutSound" && i->is_boolean())) {
			++i;
		} else {
			LOG(("LuxuryGramSettings: dropping %1, unexpected type").arg(name));
			i = p.erase(i);
			modified = true;
		}
	}
	return modified;
}

// Single-shot, so it has already cancelled itself by the time it fires. A
// base::Timer belongs to the thread that constructed it, and this one is
// constructed by whichever caller reaches save() first -- in practice the
// main thread, since every setter runs there. Nothing depends on that: save()
// compares before it starts the timer, so a first caller on some other thread
// only means that thread owns it and the other ones queue onto the main one.
struct SaveTimer {
	base::Timer timer{ [] { writeSettings(); } };
	// base::Timer inherits QObject privately, so it cannot be asked which
	// thread owns it. Recording the thread beside it is the same answer, and
	// the two are created together so they cannot disagree.
	QThread *thread = QThread::currentThread();
};

SaveTimer &saveTimer() {
	static SaveTimer result;
	return result;
}

void repaintApp() {
	for (QWidget *widget : QApplication::allWidgets()) {
		widget->update();
	}
}

} // namespace

GhostModeAccountSettings::GhostModeAccountSettings() {
	rpl::combine(
		_sendReadMessages.value(),
		_sendReadMessagesLocked.value(),
		_sendReadStories.value(),
		_sendReadStoriesLocked.value(),
		_sendOnlinePackets.value(),
		_sendOnlinePacketsLocked.value(),
		_sendUploadProgress.value(),
		_sendUploadProgressLocked.value(),
		_sendOfflinePacketAfterOnline.value(),
		_sendOfflinePacketAfterOnlineLocked.value()
	) | rpl::on_next([=](
			bool readMsg, bool readMsgLocked,
			bool readStories, bool readStoriesLocked,
			bool online, bool onlineLocked,
			bool upload, bool uploadLocked,
			bool offline, bool offlineLocked) {
		_ghostModeActive.force_assign((readMsgLocked || !readMsg)
			&& (readStoriesLocked || !readStories)
			&& (onlineLocked || !online)
			&& (uploadLocked || !upload)
			&& (offlineLocked || offline));
	}, _lifetime);
}

void GhostModeAccountSettings::setSendReadMessages(bool val) {
	++_privacyRevisions[0];
	if (_sendReadMessages.current() == val) return;
	_sendReadMessages = val;
	LuxurySettings::save();
}

void GhostModeAccountSettings::setSendReadStories(bool val) {
	++_privacyRevisions[1];
	if (_sendReadStories.current() == val) return;
	_sendReadStories = val;
	LuxurySettings::save();
}

void GhostModeAccountSettings::setSendOnlinePackets(bool val) {
	++_privacyRevisions[2];
	if (_sendOnlinePackets.current() == val) return;
	_sendOnlinePackets = val;
	LuxurySettings::save();
}

void GhostModeAccountSettings::setSendUploadProgress(bool val) {
	++_privacyRevisions[3];
	if (_sendUploadProgress.current() == val) return;
	_sendUploadProgress = val;
	LuxurySettings::save();
}

void GhostModeAccountSettings::setSendOfflinePacketAfterOnline(bool val) {
	++_privacyRevisions[4];
	if (_sendOfflinePacketAfterOnline.current() == val) return;
	_sendOfflinePacketAfterOnline = val;
	LuxurySettings::save();
	if (val) {
		// Turning this on is itself a reason to send one, and the poll sits idle
		// until something says so.
		LuxuryWorker::wake();
	}
}

void GhostModeAccountSettings::setMarkReadAfterAction(bool val) {
	if (_markReadAfterAction.current() == val) return;
	_markReadAfterAction = val;
	LuxurySettings::save();
}

void GhostModeAccountSettings::setUseScheduledMessages(bool val) {
	if (_useScheduledMessages.current() == val) return;
	_useScheduledMessages = val;
	LuxurySettings::save();
}

bool GhostModeAccountSettings::shouldSendWithoutSound() const {
	switch (_sendWithoutSound.current()) {
	case SendWithoutSoundOption::Never:
		return false;
	case SendWithoutSoundOption::InGhostMode:
		return isGhostModeActive();
	case SendWithoutSoundOption::Always:
		return true;
	}
	Unexpected("Value in GhostModeAccountSettings::shouldSendWithoutSound.");
}

void GhostModeAccountSettings::setSendWithoutSound(
		SendWithoutSoundOption val) {
	if (_sendWithoutSound.current() == val) return;
	_sendWithoutSound = val;
	LuxurySettings::save();
}

void GhostModeAccountSettings::setSuggestGhostModeBeforeViewingStory(bool val) {
	if (_suggestGhostModeBeforeViewingStory.current() == val) return;
	_suggestGhostModeBeforeViewingStory = val;
	LuxurySettings::save();
}

void GhostModeAccountSettings::setGhostModeEnabled(bool val) {
	const auto unlocked = std::array{ !sendReadMessagesLocked(), !sendReadStoriesLocked(),
		!sendOnlinePacketsLocked(), !sendUploadProgressLocked(), !sendOfflinePacketAfterOnlineLocked() };
	for (auto i = 0; i != int(unlocked.size()); ++i) {
		if (unlocked[i]) ++_privacyRevisions[i];
	}
	if (!_sendReadMessagesLocked.current()) _sendReadMessages = !val;
	if (!_sendReadStoriesLocked.current()) _sendReadStories = !val;
	if (!_sendOnlinePacketsLocked.current()) _sendOnlinePackets = !val;
	if (!_sendUploadProgressLocked.current()) _sendUploadProgress = !val;
	if (!_sendOfflinePacketAfterOnlineLocked.current()) _sendOfflinePacketAfterOnline = val;
	LuxurySettings::save();

	if (val) {
		if (const auto window = Core::App().activeWindow()) {
			if (const auto session = window->maybeSession()) {
				LuxuryWorker::markAsOnline(session);
			}
		}
	}
}

void GhostModeAccountSettings::setSendReadMessagesLocked(bool val) {
	if (_sendReadMessagesLocked.current() == val) return;
	_sendReadMessagesLocked = val;
	LuxurySettings::save();
}

void GhostModeAccountSettings::setSendReadStoriesLocked(bool val) {
	if (_sendReadStoriesLocked.current() == val) return;
	_sendReadStoriesLocked = val;
	LuxurySettings::save();
}

void GhostModeAccountSettings::setSendOnlinePacketsLocked(bool val) {
	if (_sendOnlinePacketsLocked.current() == val) return;
	_sendOnlinePacketsLocked = val;
	LuxurySettings::save();
}

void GhostModeAccountSettings::setSendUploadProgressLocked(bool val) {
	if (_sendUploadProgressLocked.current() == val) return;
	_sendUploadProgressLocked = val;
	LuxurySettings::save();
}

void GhostModeAccountSettings::setSendOfflinePacketAfterOnlineLocked(bool val) {
	if (_sendOfflinePacketAfterOnlineLocked.current() == val) return;
	_sendOfflinePacketAfterOnlineLocked = val;
	LuxurySettings::save();
}

void to_json(nlohmann::json &j, const GhostModeAccountSettings &s) {
	j = nlohmann::json{
		{"sendReadMessages", s._sendReadMessages.current()},
		{"sendReadStories", s._sendReadStories.current()},
		{"sendOnlinePackets", s._sendOnlinePackets.current()},
		{"sendUploadProgress", s._sendUploadProgress.current()},
		{"sendOfflinePacketAfterOnline", s._sendOfflinePacketAfterOnline.current()},
		{"markReadAfterAction", s._markReadAfterAction.current()},
		{"useScheduledMessages", s._useScheduledMessages.current()},
		{"sendWithoutSound", s._sendWithoutSound.current()},
		{"suggestGhostModeBeforeViewingStory", s._suggestGhostModeBeforeViewingStory.current()},
		{"sendReadMessagesLocked", s._sendReadMessagesLocked.current()},
		{"sendReadStoriesLocked", s._sendReadStoriesLocked.current()},
		{"sendOnlinePacketsLocked", s._sendOnlinePacketsLocked.current()},
		{"sendUploadProgressLocked", s._sendUploadProgressLocked.current()},
		{"sendOfflinePacketAfterOnlineLocked", s._sendOfflinePacketAfterOnlineLocked.current()}
	};
}

void from_json(const nlohmann::json &j, GhostModeAccountSettings &s) {
	s._sendReadMessages = j.value("sendReadMessages", true);
	s._sendReadStories = j.value("sendReadStories", true);
	s._sendOnlinePackets = j.value("sendOnlinePackets", true);
	s._sendUploadProgress = j.value("sendUploadProgress", true);
	s._sendOfflinePacketAfterOnline = j.value("sendOfflinePacketAfterOnline", false);
	s._markReadAfterAction = j.value("markReadAfterAction", true);
	s._useScheduledMessages = j.value("useScheduledMessages", false);
	const auto sendWithoutSound = j.find("sendWithoutSound");
	s._sendWithoutSound = (sendWithoutSound == j.end())
		? SendWithoutSoundOption::Never
		: sendWithoutSound->is_boolean()
		? (sendWithoutSound->get<bool>()
			? SendWithoutSoundOption::Always
			: SendWithoutSoundOption::Never)
		: sendWithoutSound->get<SendWithoutSoundOption>();
	s._suggestGhostModeBeforeViewingStory = j.value("suggestGhostModeBeforeViewingStory", true);
	s._sendReadMessagesLocked = j.value("sendReadMessagesLocked", false);
	s._sendReadStoriesLocked = j.value("sendReadStoriesLocked", false);
	s._sendOnlinePacketsLocked = j.value("sendOnlinePacketsLocked", false);
	s._sendUploadProgressLocked = j.value("sendUploadProgressLocked", false);
	s._sendOfflinePacketAfterOnlineLocked = j.value("sendOfflinePacketAfterOnlineLocked", false);
}

void MessageShotSettings::setShowBackground(bool val) {
	if (_showBackground.current() == val) return;
	_showBackground = val;
	LuxurySettings::save();
}

void MessageShotSettings::setShowDate(bool val) {
	if (_showDate.current() == val) return;
	_showDate = val;
	LuxurySettings::save();
}

void MessageShotSettings::setShowReactions(bool val) {
	if (_showReactions.current() == val) return;
	_showReactions = val;
	LuxurySettings::save();
}

void MessageShotSettings::setShowHeaderDecorations(bool val) {
	if (_showHeaderDecorations.current() == val) return;
	_showHeaderDecorations = val;
	LuxurySettings::save();
}

void MessageShotSettings::setShowColorfulReplies(bool val) {
	if (_showColorfulReplies.current() == val) return;
	_showColorfulReplies = val;
	LuxurySettings::save();
}

void MessageShotSettings::setRevealSpoilers(bool val) {
	if (_revealSpoilers.current() == val) return;
	_revealSpoilers = val;
	LuxurySettings::save();
}

void MessageShotSettings::setAnonymous(bool val) {
	if (_anonymous.current() == val) return;
	_anonymous = val;
	LuxurySettings::save();
}

bool MessageShotSettings::isCloudThemeEmpty() const {
	return !_cloudThemeId.current()
		&& !_cloudThemeAccessHash.current()
		&& !_cloudThemeDocumentId.current()
		&& _cloudThemeTitle.current().isEmpty();
}

void MessageShotSettings::clearCloudThemeData() {
	_cloudThemeId = uint64(0);
	_cloudThemeAccessHash = uint64(0);
	_cloudThemeDocumentId = uint64(0);
	_cloudThemeTitle = QString();
	_cloudThemeAccountId = uint64(0);
}

void MessageShotSettings::setEmbeddedTheme(int type, uint32 accentColor) {
	if (_embeddedThemeType.current() == type
		&& _embeddedThemeAccentColor.current() == accentColor
		&& isCloudThemeEmpty()) {
		return;
	}
	_embeddedThemeType = type;
	_embeddedThemeAccentColor = accentColor;
	clearCloudThemeData();
	LuxurySettings::save();
}

void MessageShotSettings::setCloudTheme(uint64 accountId, uint64 id, uint64 accessHash, uint64 documentId, const QString &title) {
	const auto validatedTitle = title.left(
		safeCutPosition(title, kMaxThemeTitleLength));
	if (_embeddedThemeType.current() == -1
		&& _embeddedThemeAccentColor.current() == 0
		&& _cloudThemeAccountId.current() == accountId
		&& _cloudThemeId.current() == id
		&& _cloudThemeAccessHash.current() == accessHash
		&& _cloudThemeDocumentId.current() == documentId
		&& _cloudThemeTitle.current() == validatedTitle) {
		return;
	}
	_embeddedThemeType = -1;
	_embeddedThemeAccentColor = uint32(0);
	_cloudThemeAccountId = accountId;
	_cloudThemeId = id;
	_cloudThemeAccessHash = accessHash;
	_cloudThemeDocumentId = documentId;
	_cloudThemeTitle = validatedTitle;
	LuxurySettings::save();
}

void MessageShotSettings::clearTheme() {
	if (_embeddedThemeType.current() == -1
		&& _embeddedThemeAccentColor.current() == 0
		&& isCloudThemeEmpty()) {
		return;
	}
	_embeddedThemeType = -1;
	_embeddedThemeAccentColor = uint32(0);
	clearCloudThemeData();
	LuxurySettings::save();
}

void to_json(nlohmann::json &j, const MessageShotSettings &s) {
	j = nlohmann::json{
		{"showBackground", s._showBackground.current()},
		{"showDate", s._showDate.current()},
		{"showReactions", s._showReactions.current()},
		{"showHeaderDecorations", s._showHeaderDecorations.current()},
		{"showColorfulReplies", s._showColorfulReplies.current()},
		{"revealSpoilers", s._revealSpoilers.current()},
		{"anonymous", s._anonymous.current()},
		{"embeddedThemeType", s._embeddedThemeType.current()},
		{"embeddedThemeAccentColor", s._embeddedThemeAccentColor.current()},
		{"cloudThemeId", s._cloudThemeId.current()},
		{"cloudThemeAccessHash", s._cloudThemeAccessHash.current()},
		{"cloudThemeDocumentId", s._cloudThemeDocumentId.current()},
		{"cloudThemeTitle", s._cloudThemeTitle.current()},
		{"cloudThemeAccountId", s._cloudThemeAccountId.current()},
	};
}

void from_json(const nlohmann::json &j, MessageShotSettings &s) {
	s._showBackground = j.value("showBackground", true);
	s._showDate = j.value("showDate", false);
	s._showReactions = j.value("showReactions", false);
	s._showHeaderDecorations = j.value("showHeaderDecorations", true);
	s._showColorfulReplies = j.value("showColorfulReplies", true);
	s._revealSpoilers = j.value("revealSpoilers", true);
	s._anonymous = j.value("anonymous", false);
	s._embeddedThemeType = j.value("embeddedThemeType", j.value("themeType", -1));
	s._embeddedThemeAccentColor = j.value("embeddedThemeAccentColor", j.value("themeAccentColor", uint32(0)));
	s._cloudThemeId = j.value("cloudThemeId", uint64(0));
	s._cloudThemeAccessHash = j.value("cloudThemeAccessHash", uint64(0));
	s._cloudThemeDocumentId = j.value("cloudThemeDocumentId", uint64(0));
	s._cloudThemeTitle = j.value("cloudThemeTitle", QString());
	s._cloudThemeAccountId = j.value("cloudThemeAccountId", uint64(0));
}

LuxurySettings::LuxurySettings()
: _appIcon(LuxuryAssets::DEFAULT_ICON) {
}

LuxurySettings &LuxurySettings::getInstance() {
	static LuxurySettings instance;
	return instance;
}

void LuxurySettings::load() {
	const auto startupGuard = gsl::finally([] {
		if (!cGhost()) return;
		auto &settings = getInstance();
		auto &global = ghostProfile(0);
		applyStartupGhost(global);
		for (auto &[id, profile] : settings._ghostAccounts) {
			applyStartupGhost(*profile);
		}
	});
	QFile file(getSettingsPath());
	if (!file.open(QIODevice::ReadOnly)) {
		SettingsWriteBlocked = QFile::exists(getSettingsPath());
		return;
	}
	SettingsWriteBlocked = false;
	// A repaired or unreadable file is still the user's to recover. Never
	// replace an earlier recovery copy, or overwrite this file if rescue fails.
	const auto rescue = [&] {
		file.close();
		const auto brokenBase = getSettingsPath() + u".broken"_q;
		auto broken = brokenBase;
		for (auto suffix = 1; QFile::exists(broken); ++suffix) {
			broken = brokenBase + '.' + QString::number(suffix);
		}
		SettingsWriteBlocked = !QFile::rename(getSettingsPath(), broken);
		if (SettingsWriteBlocked) {
			LOG(("LuxuryGramSettings: failed to preserve the settings file, saving disabled"));
		} else {
			LOG(("LuxuryGramSettings: moved the damaged file to %1"
				).arg(broken));
		}
	};
	if (file.size() > kMaxSettingsBytes) {
		LOG(("LuxuryGramSettings: settings file exceeds size limit"));
		rescue();
		return;
	}
	const auto data = file.read(kMaxSettingsBytes + 1);
	if (data.size() > kMaxSettingsBytes) {
		LOG(("LuxuryGramSettings: settings file exceeds size limit"));
		rescue();
		return;
	}

	auto &settings = getInstance();

	auto damaged = false;
	try {
		auto p = json::parse(data.constData(), data.constData() + data.size());
		if (!p.is_object()) {
			LOG(("LuxuryGramSettings: settings root is not an object"));
			rescue();
			return;
		}

		if (!p.contains("ghostModeSettings")) {
			auto account = json(GhostModeAccountSettings());
			for (auto &[key, value] : account.items()) {
				if (p.contains(key)) {
					value = p[key];
				}
			}
			p["ghostModeSettings"] = json::object({ {"0", std::move(account)} });
			p["useGlobalGhostMode"] = true;

			LOG(("LuxuryGramSettings: migrated ghost mode settings to per-account format"));
		}

		damaged = dropMismatchedKeys(p, json(LuxurySettings()), QString());

		auto loaded = LuxurySettings();
		from_json(p, loaded);
		settings = std::move(loaded);
	} catch (const std::exception &ex) {
		LOG(("LuxuryGramSettings: failed to read settings file: %1"
			).arg(ex.what()));
		damaged = true;
	} catch (...) {
		LOG(("LuxuryGramSettings: failed to read settings file"));
		damaged = true;
	}
	if (damaged) {
		rescue();
	}

	settings.validate();
}

void LuxurySettings::save() {
	// Every setter calls this, and some of them fire per mouse move, so a
	// synchronous 4 KB dump plus file rename per call used to show up as
	// stutter while dragging a slider. Coalesce into one write instead.
	auto &saver = saveTimer();
	if (saver.thread != QThread::currentThread()) {
		// A timer owned by another thread cannot be started from here, and
		// dropping the write is not an option. writeSettings() running on
		// this thread would race one on the timer's thread, so queue the
		// write on the main thread instead.
		if (Core::IsAppLaunched()) {
			crl::on_main([] { writeSettings(); });
			return;
		}
		writeSettings();
		return;
	}
	saver.timer.callOnce(kSaveDelay);
}

void LuxurySettings::saveIfScheduled() {
	auto &timer = saveTimer().timer;
	if (timer.isActive()) {
		timer.cancel();
		writeSettings();
	}
}

bool LuxurySettings::reset() {
	auto &settings = getInstance();
	// Assigning the struct bypasses every setter, and for a third of them the side
	// effect is what makes the setting real: the lib_ui mirrors live in a second
	// singleton this does not touch, streamer mode's capture exclusion is applied
	// nowhere else, and the app icon is a file on disk. Push the defaults out by
	// hand afterwards, and report the ones only a restart can push.
	const LuxurySettings defaults;
	// These are read once before style::StartManager() or cached for the process
	// lifetime, so their own setters cannot apply them live either -- every one of
	// them is behind a ShowRestartPrompt in the UI.
	const auto needsRestart = (settings.monoFont() != defaults.monoFont())
		|| (settings.wideMultiplier() != defaults.wideMultiplier())
		|| (settings.messageBubbleRadius() != defaults.messageBubbleRadius())
		|| (settings.avatarCorners() != defaults.avatarCorners())
		|| (settings.disableStories() != defaults.disableStories())
		|| (settings.filterZalgo() != defaults.filterZalgo());
	const auto wasStreamerMode = settings.streamerMode();

	settings = LuxurySettings();

	LuxuryUiSettings::setMaterialSwitches(settings.materialSwitches());
	LuxuryUiSettings::setAvatarCorners(settings.avatarCorners());
	LuxuryUiSettings::setMenuRadius(settings.menuRadius());
	if (wasStreamerMode != settings.streamerMode()) {
		LuxuryFeatures::StreamerMode::apply(settings.streamerMode());
	}
	// Covers the notification badge too: it ends in the same three App() calls
	// setHideNotificationBadge makes.
	LuxuryAssets::applyAppIcon();
	if (const auto manager = Luxury::Translator::TranslateManager::currentInstance()) {
		manager->resetCache();
	}
	FiltersCacheController::dropResults();
	repaintApp();
	Core::App().tray().updateMenuText();

	save();
	return needsRestart;
}

GhostModeAccountSettings &LuxurySettings::ghost(not_null<Main::Session*> session) {
	return ghost(session->uniqueId());
}

GhostModeAccountSettings &LuxurySettings::ghost(uint64 userId) {
	return ghostProfile(getInstance().getOverriddenGhostUserId(userId));
}

void LuxurySettings::applyStartupGhost(GhostModeAccountSettings &profile) {
	if (!cGhost() || profile._startupGhostApplied) return;
	profile._startupGhostApplied = true;
	profile._sendReadMessages = false;
	profile._sendReadStories = false;
	profile._sendOnlinePackets = false;
	profile._sendUploadProgress = false;
	profile._sendOfflinePacketAfterOnline = true;
}

GhostModeAccountSettings &LuxurySettings::ghostProfile(uint64 overriddenId) {
	auto &settings = getInstance();
	auto it = settings._ghostAccounts.find(overriddenId);
	if (it == settings._ghostAccounts.end()) {
		if (settings._ghostAccounts.size() >= kMaxGhostAccounts) {
			static bool logged = false;
			if (!logged) {
				logged = true;
				LOG(("LuxuryGramSettings: ghost account cap of %1 reached, ignoring further accounts").arg(kMaxGhostAccounts));
			}
			// ponytail: cap where the map grows, not where it persists;
			// a rejected account shares this dummy and its writes are
			// never saved by design.
			static GhostModeAccountSettings dummy;
			applyStartupGhost(dummy);
			return dummy;
		}
		auto account = std::make_unique<GhostModeAccountSettings>();
		it = settings._ghostAccounts.emplace(overriddenId, std::move(account)).first;
	}

	applyStartupGhost(*it->second);
	return *it->second;
}

GhostModeAccountSettings &LuxurySettings::ghost() {
	if (const auto window = Core::App().activeWindow()) {
		if (const auto session = window->maybeSession()) {
			return ghost(session);
		}
	}
	return ghost(0);
}

void LuxurySettings::setUseGlobalGhostMode(bool val) {
	if (_useGlobalGhostMode.current() == val) return;
	_useGlobalGhostMode = val;
	save();
}

void LuxurySettings::addShadowBan(int64 id) {
	if (_shadowBanIds.size() >= kMaxShadowBanIds
		&& !_shadowBanIds.contains(id)) {
		return;
	}
	if (_shadowBanIds.insert(id).second) {
		// The shadow ban list is not part of the compiled patterns, so only the
		// remembered per-message verdicts have to go.
		FiltersCacheController::dropResults();
		save();
	}
}

void LuxurySettings::removeShadowBan(int64 id) {
	if (_shadowBanIds.erase(id) > 0) {
		FiltersCacheController::dropResults();
		save();
	}
}

void LuxurySettings::setWatched(int64 dialogId, bool watched) {
	if (watched) {
		if (_watchedDialogs.size() >= kMaxWatchedDialogs
			&& !_watchedDialogs.contains(dialogId)) {
			return;
		}
		if (!_watchedDialogs.insert(dialogId).second) {
			return;
		}
	} else if (!_watchedDialogs.erase(dialogId)) {
		return;
	}
	save();
}

void LuxurySettings::validate() {
	LuxurySettings defaults;
	auto modified = false;

	auto validateRange = [&](auto &var, auto min, auto max, const auto &defaultVar) {
		if (var.current() < min || var.current() > max) {
			var = defaultVar.current();
			modified = true;
		}
	};

	auto validateEnum = [&](auto &var, const auto &defaultVar, int max = 2) {
		auto intVal = static_cast<int>(var.current());
		if (intVal < 0 || intVal > max) {
			var = defaultVar.current();
			modified = true;
		}
	};
	auto validateText = [&](auto &var, int maxLength) {
		if (var.current().size() > maxLength) {
			var = var.current().left(
				safeCutPosition(var.current(), maxLength));
			modified = true;
		}
	};

	validateEnum(_showPeerId, defaults._showPeerId);
	validateEnum(_channelBottomButton, defaults._channelBottomButton);
	validateEnum(_showReactionsPanelInContextMenu, defaults._showReactionsPanelInContextMenu);
	validateEnum(_showViewsPanelInContextMenu, defaults._showViewsPanelInContextMenu);
	validateEnum(_showHideMessageInContextMenu, defaults._showHideMessageInContextMenu);
	validateEnum(_showUserMessagesInContextMenu, defaults._showUserMessagesInContextMenu);
	validateEnum(_showMessageDetailsInContextMenu, defaults._showMessageDetailsInContextMenu);
	validateEnum(_showRepeatMessageInContextMenu, defaults._showRepeatMessageInContextMenu);
	validateEnum(_showAddFilterInContextMenu, defaults._showAddFilterInContextMenu);
	validateEnum(_showTranslateInContextMenu, defaults._showTranslateInContextMenu);
	validateEnum(_showEditsHistoryInContextMenu, defaults._showEditsHistoryInContextMenu);
	validateEnum(_showReadUntilInContextMenu, defaults._showReadUntilInContextMenu);
	validateEnum(_showExpireMediaInContextMenu, defaults._showExpireMediaInContextMenu);
	validateEnum(_showForwardVariantsInContextMenu, defaults._showForwardVariantsInContextMenu);

	validateEnum(_translationProvider, defaults._translationProvider, 3);
	if ((_translationProvider.current() == TranslationProvider::Native)
		&& !Platform::IsTranslateProviderAvailable()) {
		_translationProvider = defaults._translationProvider.current();
		modified = true;
	}
	if (!LuxuryAssets::isValidAppIcon(_appIcon.current())) {
		_appIcon = defaults._appIcon.current();
		modified = true;
	}

	validateRange(_messageBubbleRadius, 0, 16, defaults._messageBubbleRadius);
	validateRange(_wideMultiplier, 1.0, 4.0, defaults._wideMultiplier);
	validateRange(_avatarCorners, 0, LuxuryUiSettings::kMaxAvatarCorners, defaults._avatarCorners);
	validateRange(
		_menuRadius,
		LuxuryUiSettings::kMinMenuRadius,
		LuxuryUiSettings::kMaxMenuRadius,
		defaults._menuRadius);
	validateText(_deletedMark, kMaxMarkLength);
	validateText(_editedMark, kMaxMarkLength);
	validateText(_monoFont, kMaxFontFamilyLength);
	validateText(_messageShotSettings._cloudThemeTitle, kMaxThemeTitleLength);

	const auto embeddedType = _messageShotSettings._embeddedThemeType.current();
	const auto embeddedTypeValid = (embeddedType >= -1)
		&& (embeddedType <= int(Window::Theme::EmbeddedType::NightGreen));
	if (!embeddedTypeValid) {
		_messageShotSettings._embeddedThemeType = defaults._messageShotSettings._embeddedThemeType.current();
		_messageShotSettings._embeddedThemeAccentColor = defaults._messageShotSettings._embeddedThemeAccentColor.current();
		modified = true;
	}

	if (modified) {
		save();
	}
}

void LuxurySettings::setSaveDeletedMessages(bool val) {
	if (_saveDeletedMessages.current() == val) return;
	_saveDeletedMessages = val;
	save();
}

void LuxurySettings::setSaveDeletedMessagesEvenWhenLocked(bool val) {
	if (_saveDeletedMessagesEvenWhenLocked.current() == val) return;
	_saveDeletedMessagesEvenWhenLocked = val;
	save();
}

void LuxurySettings::setTrackOnlineHistory(bool val) {
	if (_trackOnlineHistory.current() == val) return;
	_trackOnlineHistory = val;
	save();
}

void LuxurySettings::setTrackOnlineEvenWhenLocked(bool val) {
	if (_trackOnlineEvenWhenLocked.current() == val) return;
	_trackOnlineEvenWhenLocked = val;
	save();
}

void LuxurySettings::setDisableChatSelectionLimit(bool val) {
	if (_disableChatSelectionLimit.current() == val) return;
	_disableChatSelectionLimit = val;
	save();
}

void LuxurySettings::setSaveMessagesHistory(bool val) {
	if (_saveMessagesHistory.current() == val) return;
	_saveMessagesHistory = val;
	save();
}

void LuxurySettings::setSaveForBots(bool val) {
	if (_saveForBots.current() == val) return;
	_saveForBots = val;
	save();
}

void LuxurySettings::setFiltersEnabled(bool val) {
	if (_filtersEnabled.current() == val) return;
	_filtersEnabled = val;
	save();
}

void LuxurySettings::setFiltersEnabledInChats(bool val) {
	if (_filtersEnabledInChats.current() == val) return;
	_filtersEnabledInChats = val;
	save();
}

void LuxurySettings::setHideFromBlocked(bool val) {
	if (_hideFromBlocked.current() == val) return;
	_hideFromBlocked = val;
	save();
}

void LuxurySettings::setSemiTransparentDeletedMessages(bool val) {
	if (_semiTransparentDeletedMessages.current() == val) return;
	_semiTransparentDeletedMessages = val;
	save();
}

void LuxurySettings::setDisableAds(bool val) {
	if (_disableAds.current() == val) return;
	_disableAds = val;
	save();
}

void LuxurySettings::setDisableStories(bool val) {
	if (_disableStories.current() == val) return;
	_disableStories = val;
	save();
}

void LuxurySettings::setDisableCustomBackgrounds(bool val) {
	if (_disableCustomBackgrounds.current() == val) return;
	_disableCustomBackgrounds = val;
	save();
}

void LuxurySettings::setHidePremiumStatuses(bool val) {
	if (_hidePremiumStatuses.current() == val) return;
	_hidePremiumStatuses = val;
	// Badges and name statuses read this flag only while painting.
	repaintApp();
	save();
}

void LuxurySettings::setShowOnlyAddedEmojisAndStickers(bool val) {
	if (_showOnlyAddedEmojisAndStickers.current() == val) return;
	_showOnlyAddedEmojisAndStickers = val;
	save();
}

void LuxurySettings::setCollapseSimilarChannels(bool val) {
	if (_collapseSimilarChannels.current() == val) return;
	_collapseSimilarChannels = val;
	save();
}

void LuxurySettings::setHideSimilarChannels(bool val) {
	if (_hideSimilarChannels.current() == val) return;
	_hideSimilarChannels = val;
	save();
}

void LuxurySettings::setMessageBubbleRadius(int val) {
	if (_messageBubbleRadius.current() == val) return;
	_messageBubbleRadius = val;
	save();
}

void LuxurySettings::setWideMultiplier(double val) {
	if (_wideMultiplier.current() == val) return;
	_wideMultiplier = val;
	// Styles read it once before style::StartManager(): applies after restart.
	save();
}

void LuxurySettings::setSpoofWebviewAsAndroid(bool val) {
	if (_spoofWebviewAsAndroid.current() == val) return;
	_spoofWebviewAsAndroid = val;
	save();
}

void LuxurySettings::setDisableOpenLinkWarning(bool val) {
	if (_disableOpenLinkWarning.current() == val) return;
	_disableOpenLinkWarning = val;
	save();
}

void LuxurySettings::setIncreaseWebviewHeight(bool val) {
	if (_increaseWebviewHeight.current() == val) return;
	_increaseWebviewHeight = val;
	save();
}

void LuxurySettings::setIncreaseWebviewWidth(bool val) {
	if (_increaseWebviewWidth.current() == val) return;
	_increaseWebviewWidth = val;
	save();
}

void LuxurySettings::setMaterialSwitches(bool val) {
	if (_materialSwitches.current() == val) return;
	_materialSwitches = val;
	LuxuryUiSettings::setMaterialSwitches(val);
	repaintApp();
	save();
}

void LuxurySettings::setRemoveMessageTail(bool val) {
	if (_removeMessageTail.current() == val) return;
	_removeMessageTail = val;
	save();
}

void LuxurySettings::setDisableNotificationsDelay(bool val) {
	if (_disableNotificationsDelay.current() == val) return;
	_disableNotificationsDelay = val;
	save();
}

void LuxurySettings::setLocalPremium(bool val) {
	if (_localPremium.current() == val) return;
	_localPremium = val;
	save();
}

void LuxurySettings::setShowChannelReactions(bool val) {
	if (_showChannelReactions.current() == val) return;
	_showChannelReactions = val;
	save();
}

void LuxurySettings::setShowGroupReactions(bool val) {
	if (_showGroupReactions.current() == val) return;
	_showGroupReactions = val;
	save();
}

void LuxurySettings::setShowPrivateChatReactions(bool val) {
	if (_showPrivateChatReactions.current() == val) return;
	_showPrivateChatReactions = val;
	save();
}

void LuxurySettings::setAppIcon(const QString &val) {
	const auto validated = LuxuryAssets::isValidAppIcon(val)
		? val
		: LuxuryAssets::DEFAULT_ICON;
	if (_appIcon.current() == validated) return;
	_appIcon = validated;
	save();
}

void LuxurySettings::setSimpleQuotesAndReplies(bool val) {
	if (_simpleQuotesAndReplies.current() == val) return;
	_simpleQuotesAndReplies = val;
	// Assigning above is what drops the quote caches every ChatStyle built from
	// this setting -- see the note in ChatStyle's constructor. They only refill on
	// a paint, so ask for one.
	repaintApp();
	save();
}

void LuxurySettings::setHideFastShare(bool val) {
	if (_hideFastShare.current() == val) return;
	_hideFastShare = val;
	save();
}

void LuxurySettings::setReplaceBottomInfoWithIcons(bool val) {
	if (_replaceBottomInfoWithIcons.current() == val) return;
	_replaceBottomInfoWithIcons = val;
	save();
}

void LuxurySettings::setDeletedMark(const QString &val) {
	const auto validated = val.left(safeCutPosition(val, kMaxMarkLength));
	if (_deletedMark.current() == validated) return;
	_deletedMark = validated;
	save();
}

QString LuxurySettings::editedMark() const {
	const auto &mark = _editedMark.current();
	return mark.isEmpty() ? tr::lng_edited(tr::now) : mark;
}

rpl::producer<QString> LuxurySettings::editedMarkValue() const {
	return rpl::combine(
		_editedMark.value(),
		tr::lng_edited()
	) | rpl::map([](const QString &mark, const QString &language) {
		return mark.isEmpty() ? language : mark;
	});
}

void LuxurySettings::setEditedMark(const QString &val) {
	auto validated = val.left(safeCutPosition(val, kMaxMarkLength));
	if (validated == tr::lng_edited(tr::now)) {
		validated = QString(); // The language's own word: keep following it.
	}
	if (_editedMark.current() == validated) return;
	_editedMark = validated;
	save();
}

void LuxurySettings::setUnlimitedRecentStickers(bool val) {
	if (_unlimitedRecentStickers.current() == val) return;
	_unlimitedRecentStickers = val;
	save();
}

void LuxurySettings::setUnlimitedFavedStickers(bool val) {
	if (_unlimitedFavedStickers.current() == val) return;
	_unlimitedFavedStickers = val;
	save();
}

void LuxurySettings::setShowReactionsPanelInContextMenu(ContextMenuVisibility val) {
	if (_showReactionsPanelInContextMenu.current() == val) return;
	_showReactionsPanelInContextMenu = val;
	save();
}

void LuxurySettings::setShowViewsPanelInContextMenu(ContextMenuVisibility val) {
	if (_showViewsPanelInContextMenu.current() == val) return;
	_showViewsPanelInContextMenu = val;
	save();
}

void LuxurySettings::setShowHideMessageInContextMenu(ContextMenuVisibility val) {
	if (_showHideMessageInContextMenu.current() == val) return;
	_showHideMessageInContextMenu = val;
	save();
}

void LuxurySettings::setShowUserMessagesInContextMenu(ContextMenuVisibility val) {
	if (_showUserMessagesInContextMenu.current() == val) return;
	_showUserMessagesInContextMenu = val;
	save();
}

void LuxurySettings::setShowMessageDetailsInContextMenu(ContextMenuVisibility val) {
	if (_showMessageDetailsInContextMenu.current() == val) return;
	_showMessageDetailsInContextMenu = val;
	save();
}

void LuxurySettings::setShowRepeatMessageInContextMenu(ContextMenuVisibility val) {
	if (_showRepeatMessageInContextMenu.current() == val) return;
	_showRepeatMessageInContextMenu = val;
	save();
}

void LuxurySettings::setShowAddFilterInContextMenu(ContextMenuVisibility val) {
	if (_showAddFilterInContextMenu.current() == val) return;
	_showAddFilterInContextMenu = val;
	save();
}

void LuxurySettings::setShowTranslateInContextMenu(ContextMenuVisibility val) {
	if (_showTranslateInContextMenu.current() == val) return;
	_showTranslateInContextMenu = val;
	save();
}

void LuxurySettings::setShowEditsHistoryInContextMenu(ContextMenuVisibility val) {
	if (_showEditsHistoryInContextMenu.current() == val) return;
	_showEditsHistoryInContextMenu = val;
	save();
}

void LuxurySettings::setShowReadUntilInContextMenu(ContextMenuVisibility val) {
	if (_showReadUntilInContextMenu.current() == val) return;
	_showReadUntilInContextMenu = val;
	save();
}

void LuxurySettings::setShowExpireMediaInContextMenu(ContextMenuVisibility val) {
	if (_showExpireMediaInContextMenu.current() == val) return;
	_showExpireMediaInContextMenu = val;
	save();
}

void LuxurySettings::setShowForwardVariantsInContextMenu(ContextMenuVisibility val) {
	if (_showForwardVariantsInContextMenu.current() == val) return;
	_showForwardVariantsInContextMenu = val;
	save();
}

void LuxurySettings::setShowAttachButtonInMessageField(bool val) {
	if (_showAttachButtonInMessageField.current() == val) return;
	_showAttachButtonInMessageField = val;
	save();
}

void LuxurySettings::setShowCommandsButtonInMessageField(bool val) {
	if (_showCommandsButtonInMessageField.current() == val) return;
	_showCommandsButtonInMessageField = val;
	save();
}

void LuxurySettings::setShowEmojiButtonInMessageField(bool val) {
	if (_showEmojiButtonInMessageField.current() == val) return;
	_showEmojiButtonInMessageField = val;
	save();
}

void LuxurySettings::setShowMicrophoneButtonInMessageField(bool val) {
	if (_showMicrophoneButtonInMessageField.current() == val) return;
	_showMicrophoneButtonInMessageField = val;
	save();
}

void LuxurySettings::setShowAutoDeleteButtonInMessageField(bool val) {
	if (_showAutoDeleteButtonInMessageField.current() == val) return;
	_showAutoDeleteButtonInMessageField = val;
	save();
}

void LuxurySettings::setShowGiftButtonInMessageField(bool val) {
	if (_showGiftButtonInMessageField.current() == val) return;
	_showGiftButtonInMessageField = val;
	save();
}

void LuxurySettings::setShowAiEditorButtonInMessageField(bool val) {
	if (_showAiEditorButtonInMessageField.current() == val) return;
	_showAiEditorButtonInMessageField = val;
	save();
}

void LuxurySettings::setShowAttachPopup(bool val) {
	if (_showAttachPopup.current() == val) return;
	_showAttachPopup = val;
	save();
}

void LuxurySettings::setShowEmojiPopup(bool val) {
	if (_showEmojiPopup.current() == val) return;
	_showEmojiPopup = val;
	save();
}

void LuxurySettings::setShowMyProfileInDrawer(bool val) {
	if (_showMyProfileInDrawer.current() == val) return;
	_showMyProfileInDrawer = val;
	save();
}

void LuxurySettings::setShowBotsInDrawer(bool val) {
	if (_showBotsInDrawer.current() == val) return;
	_showBotsInDrawer = val;
	save();
}

void LuxurySettings::setShowNewGroupInDrawer(bool val) {
	if (_showNewGroupInDrawer.current() == val) return;
	_showNewGroupInDrawer = val;
	save();
}

void LuxurySettings::setShowNewChannelInDrawer(bool val) {
	if (_showNewChannelInDrawer.current() == val) return;
	_showNewChannelInDrawer = val;
	save();
}

void LuxurySettings::setShowContactsInDrawer(bool val) {
	if (_showContactsInDrawer.current() == val) return;
	_showContactsInDrawer = val;
	save();
}

void LuxurySettings::setShowCallsInDrawer(bool val) {
	if (_showCallsInDrawer.current() == val) return;
	_showCallsInDrawer = val;
	save();
}

void LuxurySettings::setShowSavedMessagesInDrawer(bool val) {
	if (_showSavedMessagesInDrawer.current() == val) return;
	_showSavedMessagesInDrawer = val;
	save();
}

void LuxurySettings::setShowLReadToggleInDrawer(bool val) {
	if (_showLReadToggleInDrawer.current() == val) return;
	_showLReadToggleInDrawer = val;
	save();
}

void LuxurySettings::setShowSReadToggleInDrawer(bool val) {
	if (_showSReadToggleInDrawer.current() == val) return;
	_showSReadToggleInDrawer = val;
	save();
}

void LuxurySettings::setShowNightModeToggleInDrawer(bool val) {
	if (_showNightModeToggleInDrawer.current() == val) return;
	_showNightModeToggleInDrawer = val;
	save();
}

void LuxurySettings::setShowGhostToggleInDrawer(bool val) {
	if (_showGhostToggleInDrawer.current() == val) return;
	_showGhostToggleInDrawer = val;
	save();
}

void LuxurySettings::setShowStreamerToggleInDrawer(bool val) {
	if (_showStreamerToggleInDrawer.current() == val) return;
	_showStreamerToggleInDrawer = val;
	save();
}

void LuxurySettings::setShowGhostToggleInTray(bool val) {
	if (_showGhostToggleInTray.current() == val) return;
	_showGhostToggleInTray = val;
	save();
}

void LuxurySettings::setShowStreamerToggleInTray(bool val) {
	if (_showStreamerToggleInTray.current() == val) return;
	_showStreamerToggleInTray = val;
	save();
}

void LuxurySettings::setMonoFont(const QString &val) {
	const auto validated = val.left(
		safeCutPosition(val, kMaxFontFamilyLength));
	if (_monoFont.current() == validated) return;
	_monoFont = validated;
	save();
}

void LuxurySettings::setHideNotificationCounters(bool val) {
	if (_hideNotificationCounters.current() == val) return;
	_hideNotificationCounters = val;
	save();
}

void LuxurySettings::setHideNotificationBadge(bool val) {
	if (_hideNotificationBadge.current() == val) return;
	_hideNotificationBadge = val;
	Core::App().refreshApplicationIcon();
	Core::App().tray().updateIconCounters();
	Core::App().domain().notifyUnreadBadgeChanged();
	save();
}

void LuxurySettings::setHideAllChatsFolder(bool val) {
	if (_hideAllChatsFolder.current() == val) return;
	_hideAllChatsFolder = val;
	save();
}

void LuxurySettings::setChannelBottomButton(ChannelBottomButton val) {
	if (_channelBottomButton.current() == val) return;
	_channelBottomButton = val;
	save();
}

void LuxurySettings::setQuickAdminShortcuts(bool val) {
	if (_quickAdminShortcuts.current() == val) return;
	_quickAdminShortcuts = val;
	save();
}

void LuxurySettings::setDisableGreetingSticker(bool val) {
	if (_disableGreetingSticker.current() == val) return;
	_disableGreetingSticker = val;
	save();
}

void LuxurySettings::setShowPeerId(PeerIdDisplay val) {
	if (_showPeerId.current() == val) return;
	_showPeerId = val;
	save();
}

void LuxurySettings::setShowMessageSeconds(bool val) {
	if (_showMessageSeconds.current() == val) return;
	_showMessageSeconds = val;
	save();
}

void LuxurySettings::setShowLastSeenSeconds(bool val) {
	if (_showLastSeenSeconds.current() == val) return;
	_showLastSeenSeconds = val;
	save();
}

void LuxurySettings::setShowMessageShot(bool val) {
	if (_showMessageShot.current() == val) return;
	_showMessageShot = val;
	save();
}

void LuxurySettings::setFilterZalgo(bool val) {
	if (_filterZalgo.current() == val) return;
	_filterZalgo = val;
	save();
}

void LuxurySettings::setStickerConfirmation(bool val) {
	if (_stickerConfirmation.current() == val) return;
	_stickerConfirmation = val;
	save();
}

void LuxurySettings::setGifConfirmation(bool val) {
	if (_gifConfirmation.current() == val) return;
	_gifConfirmation = val;
	save();
}

void LuxurySettings::setVoiceConfirmation(bool val) {
	if (_voiceConfirmation.current() == val) return;
	_voiceConfirmation = val;
	save();
}

void LuxurySettings::setRoundConfirmation(bool val) {
	if (_roundConfirmation.current() == val) return;
	_roundConfirmation = val;
	save();
}

void LuxurySettings::setTranslationProvider(TranslationProvider val) {
	if ((val == TranslationProvider::Native)
		&& !Platform::IsTranslateProviderAvailable()) {
		val = TranslationProvider::Telegram;
	}
	if (_translationProvider.current() == val) return;
	_translationProvider = val;
	if (const auto manager = Luxury::Translator::TranslateManager::currentInstance()) {
		manager->resetCache();
	}
	save();
}

void LuxurySettings::setAdaptiveCoverColor(bool val) {
	if (_adaptiveCoverColor.current() == val) return;
	_adaptiveCoverColor = val;
	save();
}

void LuxurySettings::setImproveLinkPreviews(bool val) {
	if (_improveLinkPreviews.current() == val) return;
	_improveLinkPreviews = val;
	save();
}

void LuxurySettings::setCrashReporting(bool val) {
	if (_crashReporting.current() == val) return;
	_crashReporting = val;
	save();
}

void LuxurySettings::setAvatarCorners(int val) {
	if (_avatarCorners.current() == val) return;
	_avatarCorners = val;
	LuxuryUiSettings::setAvatarCorners(val);
	save();
}

void LuxurySettings::setMenuRadius(int val) {
	if (_menuRadius.current() == val) return;
	_menuRadius = val;
	// Applied to every popup menu created afterwards, no restart needed.
	LuxuryUiSettings::setMenuRadius(val);
	save();
}

void LuxurySettings::setSingleCornerRadius(bool val) {
	if (_singleCornerRadius.current() == val) return;
	_singleCornerRadius = val;
	repaintApp();
	save();
}

void LuxurySettings::setStreamerMode(bool val) {
	if (_streamerMode.current() == val) return;
	_streamerMode = val;
	LuxuryFeatures::StreamerMode::apply(val);
	save();
}

void to_json(nlohmann::json &j, const LuxurySettings &s) {
	auto ghostAccounts = nlohmann::json::object();
	for (const auto &[key, value] : s._ghostAccounts) {
		ghostAccounts[std::to_string(key)] = *value;
	}

	j = nlohmann::json{
		{"ghostModeSettings", ghostAccounts},
		{"useGlobalGhostMode", s._useGlobalGhostMode.current()},
		{"saveDeletedMessages", s._saveDeletedMessages.current()},
		{"saveDeletedMessagesEvenWhenLocked", s._saveDeletedMessagesEvenWhenLocked.current()},
		{"trackOnlineHistory", s._trackOnlineHistory.current()},
		{"trackOnlineEvenWhenLocked", s._trackOnlineEvenWhenLocked.current()},
		{"disableChatSelectionLimit", s._disableChatSelectionLimit.current()},
		{"saveMessagesHistory", s._saveMessagesHistory.current()},
		{"saveForBots", s._saveForBots.current()},
		{"shadowBanIds", s._shadowBanIds},
		{"watchedDialogs", s._watchedDialogs},
		{"filtersEnabled", s._filtersEnabled.current()},
		{"filtersEnabledInChats", s._filtersEnabledInChats.current()},
		{"hideFromBlocked", s._hideFromBlocked.current()},
		{"semiTransparentDeletedMessages", s._semiTransparentDeletedMessages.current()},
		{"disableAds", s._disableAds.current()},
		{"disableStories", s._disableStories.current()},
		{"disableCustomBackgrounds", s._disableCustomBackgrounds.current()},
		{"hidePremiumStatuses", s._hidePremiumStatuses.current()},
		{"showOnlyAddedEmojisAndStickers", s._showOnlyAddedEmojisAndStickers.current()},
		{"collapseSimilarChannels", s._collapseSimilarChannels.current()},
		{"hideSimilarChannels", s._hideSimilarChannels.current()},
		{"messageBubbleRadius", s._messageBubbleRadius.current()},
		{"disableOpenLinkWarning", s._disableOpenLinkWarning.current()},
		{"wideMultiplier", s._wideMultiplier.current()},
		{"spoofWebviewAsAndroid", s._spoofWebviewAsAndroid.current()},
		{"increaseWebviewHeight", s._increaseWebviewHeight.current()},
		{"increaseWebviewWidth", s._increaseWebviewWidth.current()},
		{"materialSwitches", s._materialSwitches.current()},
		{"menuRadius", s._menuRadius.current()},
		{"removeMessageTail", s._removeMessageTail.current()},
		{"disableNotificationsDelay", s._disableNotificationsDelay.current()},
		{"localPremium", s._localPremium.current()},
		{"showChannelReactions", s._showChannelReactions.current()},
		{"showGroupReactions", s._showGroupReactions.current()},
		{"showPrivateChatReactions", s._showPrivateChatReactions.current()},
		{"appIcon", s._appIcon.current()},
		{"simpleQuotesAndReplies", s._simpleQuotesAndReplies.current()},
		{"hideFastShare", s._hideFastShare.current()},
		{"replaceBottomInfoWithIcons", s._replaceBottomInfoWithIcons.current()},
		{"deletedMark", s._deletedMark.current()},
		{"customEditedMark", s._editedMark.current()},
		{"unlimitedRecentStickers", s._unlimitedRecentStickers.current()},
		{"unlimitedFavedStickers", s._unlimitedFavedStickers.current()},
		{"showReactionsPanelInContextMenu", s._showReactionsPanelInContextMenu.current()},
		{"showViewsPanelInContextMenu", s._showViewsPanelInContextMenu.current()},
		{"showHideMessageInContextMenu", s._showHideMessageInContextMenu.current()},
		{"showUserMessagesInContextMenu", s._showUserMessagesInContextMenu.current()},
		{"showMessageDetailsInContextMenu", s._showMessageDetailsInContextMenu.current()},
		{"showRepeatMessageInContextMenu", s._showRepeatMessageInContextMenu.current()},
		{"showAddFilterInContextMenu", s._showAddFilterInContextMenu.current()},
		{"showTranslateInContextMenu", s._showTranslateInContextMenu.current()},
		{"showEditsHistoryInContextMenu", s._showEditsHistoryInContextMenu.current()},
		{"showReadUntilInContextMenu", s._showReadUntilInContextMenu.current()},
		{"showExpireMediaInContextMenu", s._showExpireMediaInContextMenu.current()},
		{"showForwardVariantsInContextMenu", s._showForwardVariantsInContextMenu.current()},
		{"showAttachButtonInMessageField", s._showAttachButtonInMessageField.current()},
		{"showCommandsButtonInMessageField", s._showCommandsButtonInMessageField.current()},
		{"showEmojiButtonInMessageField", s._showEmojiButtonInMessageField.current()},
		{"showMicrophoneButtonInMessageField", s._showMicrophoneButtonInMessageField.current()},
		{"showAutoDeleteButtonInMessageField", s._showAutoDeleteButtonInMessageField.current()},
		{"showGiftButtonInMessageField", s._showGiftButtonInMessageField.current()},
		{"showAiEditorButtonInMessageField", s._showAiEditorButtonInMessageField.current()},
		{"showAttachPopup", s._showAttachPopup.current()},
		{"showEmojiPopup", s._showEmojiPopup.current()},
		{"showMyProfileInDrawer", s._showMyProfileInDrawer.current()},
		{"showBotsInDrawer", s._showBotsInDrawer.current()},
		{"showNewGroupInDrawer", s._showNewGroupInDrawer.current()},
		{"showNewChannelInDrawer", s._showNewChannelInDrawer.current()},
		{"showContactsInDrawer", s._showContactsInDrawer.current()},
		{"showCallsInDrawer", s._showCallsInDrawer.current()},
		{"showSavedMessagesInDrawer", s._showSavedMessagesInDrawer.current()},
		{"showLReadToggleInDrawer", s._showLReadToggleInDrawer.current()},
		{"showSReadToggleInDrawer", s._showSReadToggleInDrawer.current()},
		{"showNightModeToggleInDrawer", s._showNightModeToggleInDrawer.current()},
		{"showGhostToggleInDrawer", s._showGhostToggleInDrawer.current()},
		{"showStreamerToggleInDrawer", s._showStreamerToggleInDrawer.current()},
		{"showGhostToggleInTray", s._showGhostToggleInTray.current()},
		{"showStreamerToggleInTray", s._showStreamerToggleInTray.current()},
		{"monoFont", s._monoFont.current()},
		{"hideNotificationCounters", s._hideNotificationCounters.current()},
		{"hideNotificationBadge", s._hideNotificationBadge.current()},
		{"hideAllChatsFolder", s._hideAllChatsFolder.current()},
		{"channelBottomButton", s._channelBottomButton.current()},
		{"quickAdminShortcuts", s._quickAdminShortcuts.current()},
		{"disableGreetingSticker", s._disableGreetingSticker.current()},
		{"showPeerId", s._showPeerId.current()},
		{"showMessageSeconds", s._showMessageSeconds.current()},
		{"showLastSeenSeconds", s._showLastSeenSeconds.current()},
		{"showMessageShot", s._showMessageShot.current()},
		{"filterZalgo", s._filterZalgo.current()},
		{"stickerConfirmation", s._stickerConfirmation.current()},
		{"gifConfirmation", s._gifConfirmation.current()},
		{"voiceConfirmation", s._voiceConfirmation.current()},
		{"roundConfirmation", s._roundConfirmation.current()},
		{"translationProvider", s._translationProvider.current()},
		{"adaptiveCoverColor", s._adaptiveCoverColor.current()},
		{"improveLinkPreviews", s._improveLinkPreviews.current()},
		{"crashReporting", s._crashReporting.current()},
		{"avatarCorners", s._avatarCorners.current()},
		{"singleCornerRadius", s._singleCornerRadius.current()},
		{"streamerMode", s._streamerMode.current()},
		{"messageShotSettings", s._messageShotSettings}
	};
}

void from_json(const nlohmann::json &j, LuxurySettings &s) {
	LuxurySettings defaults;

	if (j.contains("ghostModeSettings") && j["ghostModeSettings"].is_object()) {
		s._ghostAccounts.clear();
		for (auto &[key, value] : j["ghostModeSettings"].items()) {
			if (s._ghostAccounts.size() >= kMaxGhostAccounts) {
				break;
			}
			auto id = uint64();
			const auto [end, error] = std::from_chars(
				key.data(),
				key.data() + key.size(),
				id);
			if (error != std::errc()
				|| end != key.data() + key.size()
				|| !value.is_object()) {
				continue;
			}
			try {
				auto account = std::make_unique<GhostModeAccountSettings>();
				value.get_to(*account);
				s._ghostAccounts[id] = std::move(account);
			} catch (const std::exception &ex) {
				LOG(("LuxuryGramSettings: dropping ghost account %1: %2"
					).arg(id).arg(ex.what()));
			}
		}
	}

	s._useGlobalGhostMode = j.value("useGlobalGhostMode", defaults._useGlobalGhostMode.current());
	s._saveDeletedMessages = j.value("saveDeletedMessages", defaults._saveDeletedMessages.current());
	s._saveDeletedMessagesEvenWhenLocked = j.value("saveDeletedMessagesEvenWhenLocked", defaults._saveDeletedMessagesEvenWhenLocked.current());
	s._trackOnlineHistory = j.value("trackOnlineHistory", defaults._trackOnlineHistory.current());
	s._trackOnlineEvenWhenLocked = j.value("trackOnlineEvenWhenLocked", defaults._trackOnlineEvenWhenLocked.current());
	s._disableChatSelectionLimit = j.value("disableChatSelectionLimit", defaults._disableChatSelectionLimit.current());
	s._saveMessagesHistory = j.value("saveMessagesHistory", defaults._saveMessagesHistory.current());
	s._saveForBots = j.value("saveForBots", defaults._saveForBots.current());
	s._shadowBanIds.clear();
	const auto shadowBans = j.find("shadowBanIds");
	if (shadowBans != j.end() && shadowBans->is_array()) {
		for (const auto &value : *shadowBans) {
			if (s._shadowBanIds.size() >= kMaxShadowBanIds) {
				break;
			}
			if (!value.is_number_integer()) {
				continue;
			}
			s._shadowBanIds.insert(value.get<int64>());
		}
	}
	s._watchedDialogs.clear();
	const auto watched = j.find("watchedDialogs");
	if (watched != j.end() && watched->is_array()) {
		for (const auto &value : *watched) {
			if (s._watchedDialogs.size() >= kMaxWatchedDialogs) {
				break;
			}
			if (!value.is_number_integer()) {
				continue;
			}
			s._watchedDialogs.insert(value.get<int64>());
		}
	}
	s._filtersEnabled = j.value("filtersEnabled", defaults._filtersEnabled.current());
	s._filtersEnabledInChats = j.value("filtersEnabledInChats", defaults._filtersEnabledInChats.current());
	s._hideFromBlocked = j.value("hideFromBlocked", defaults._hideFromBlocked.current());
	s._semiTransparentDeletedMessages = j.value("semiTransparentDeletedMessages", defaults._semiTransparentDeletedMessages.current());
	s._disableAds = j.value("disableAds", defaults._disableAds.current());
	s._disableStories = j.value("disableStories", defaults._disableStories.current());
	s._disableCustomBackgrounds = j.value("disableCustomBackgrounds", defaults._disableCustomBackgrounds.current());
	s._hidePremiumStatuses = j.value("hidePremiumStatuses", defaults._hidePremiumStatuses.current());
	s._showOnlyAddedEmojisAndStickers = j.value("showOnlyAddedEmojisAndStickers", defaults._showOnlyAddedEmojisAndStickers.current());
	s._collapseSimilarChannels = j.value("collapseSimilarChannels", defaults._collapseSimilarChannels.current());
	s._hideSimilarChannels = j.value("hideSimilarChannels", defaults._hideSimilarChannels.current());
	s._messageBubbleRadius = j.value("messageBubbleRadius", defaults._messageBubbleRadius.current());
	s._disableOpenLinkWarning = j.value("disableOpenLinkWarning", defaults._disableOpenLinkWarning.current());
	s._wideMultiplier = j.value("wideMultiplier", defaults._wideMultiplier.current());
	s._spoofWebviewAsAndroid = j.value("spoofWebviewAsAndroid", defaults._spoofWebviewAsAndroid.current());
	s._increaseWebviewHeight = j.value("increaseWebviewHeight", defaults._increaseWebviewHeight.current());
	s._increaseWebviewWidth = j.value("increaseWebviewWidth", defaults._increaseWebviewWidth.current());
	s._materialSwitches = j.value("materialSwitches", defaults._materialSwitches.current());
	s._menuRadius = j.value("menuRadius", defaults._menuRadius.current());
	s._removeMessageTail = j.value("removeMessageTail", defaults._removeMessageTail.current());
	s._disableNotificationsDelay = j.value("disableNotificationsDelay", defaults._disableNotificationsDelay.current());
	s._localPremium = j.value("localPremium", defaults._localPremium.current());
	s._showChannelReactions = j.value("showChannelReactions", defaults._showChannelReactions.current());
	s._showGroupReactions = j.value("showGroupReactions", defaults._showGroupReactions.current());
	s._showPrivateChatReactions = j.value("showPrivateChatReactions", defaults._showPrivateChatReactions.current());
	s._appIcon = j.value("appIcon", defaults._appIcon.current());
	s._simpleQuotesAndReplies = j.value("simpleQuotesAndReplies", defaults._simpleQuotesAndReplies.current());
	s._hideFastShare = j.value("hideFastShare", defaults._hideFastShare.current());
	s._replaceBottomInfoWithIcons = j.value("replaceBottomInfoWithIcons", defaults._replaceBottomInfoWithIcons.current());
	s._deletedMark = j.value("deletedMark", defaults._deletedMark.current());
	s._editedMark = j.value("customEditedMark", defaults._editedMark.current());
	// Earlier builds saved the default mark as the English word itself under
	// "editedMark", which then stayed English in every interface language.
	const auto legacyEditedMark = j.find("editedMark");
	if (!j.contains("customEditedMark")
		&& legacyEditedMark != j.end()
		&& legacyEditedMark->is_string()) {
		const auto mark = legacyEditedMark->get<QString>();
		if (mark != Lang::GetOriginalValue(tr::lng_edited.base)) {
			s._editedMark = mark;
		}
	}
	s._unlimitedRecentStickers = j.value("unlimitedRecentStickers", defaults._unlimitedRecentStickers.current());
	s._unlimitedFavedStickers = j.value("unlimitedFavedStickers", defaults._unlimitedFavedStickers.current());
	s._showReactionsPanelInContextMenu = j.value("showReactionsPanelInContextMenu", defaults._showReactionsPanelInContextMenu.current());
	s._showViewsPanelInContextMenu = j.value("showViewsPanelInContextMenu", defaults._showViewsPanelInContextMenu.current());
	s._showHideMessageInContextMenu = j.value("showHideMessageInContextMenu", defaults._showHideMessageInContextMenu.current());
	s._showUserMessagesInContextMenu = j.value("showUserMessagesInContextMenu", defaults._showUserMessagesInContextMenu.current());
	s._showMessageDetailsInContextMenu = j.value("showMessageDetailsInContextMenu", defaults._showMessageDetailsInContextMenu.current());
	s._showRepeatMessageInContextMenu = j.value("showRepeatMessageInContextMenu", defaults._showRepeatMessageInContextMenu.current());
	s._showAddFilterInContextMenu = j.value("showAddFilterInContextMenu", defaults._showAddFilterInContextMenu.current());
	s._showTranslateInContextMenu = j.value("showTranslateInContextMenu", defaults._showTranslateInContextMenu.current());
	s._showEditsHistoryInContextMenu = j.value("showEditsHistoryInContextMenu", defaults._showEditsHistoryInContextMenu.current());
	s._showReadUntilInContextMenu = j.value("showReadUntilInContextMenu", defaults._showReadUntilInContextMenu.current());
	s._showExpireMediaInContextMenu = j.value("showExpireMediaInContextMenu", defaults._showExpireMediaInContextMenu.current());
	s._showForwardVariantsInContextMenu = j.value("showForwardVariantsInContextMenu", defaults._showForwardVariantsInContextMenu.current());
	s._showAttachButtonInMessageField = j.value("showAttachButtonInMessageField", defaults._showAttachButtonInMessageField.current());
	s._showCommandsButtonInMessageField = j.value("showCommandsButtonInMessageField", defaults._showCommandsButtonInMessageField.current());
	s._showEmojiButtonInMessageField = j.value("showEmojiButtonInMessageField", defaults._showEmojiButtonInMessageField.current());
	s._showMicrophoneButtonInMessageField = j.value("showMicrophoneButtonInMessageField", defaults._showMicrophoneButtonInMessageField.current());
	s._showAutoDeleteButtonInMessageField = j.value("showAutoDeleteButtonInMessageField", defaults._showAutoDeleteButtonInMessageField.current());
	s._showGiftButtonInMessageField = j.value("showGiftButtonInMessageField", defaults._showGiftButtonInMessageField.current());
	s._showAiEditorButtonInMessageField = j.value("showAiEditorButtonInMessageField", defaults._showAiEditorButtonInMessageField.current());
	s._showAttachPopup = j.value("showAttachPopup", defaults._showAttachPopup.current());
	s._showEmojiPopup = j.value("showEmojiPopup", defaults._showEmojiPopup.current());
	s._showMyProfileInDrawer = j.value("showMyProfileInDrawer", defaults._showMyProfileInDrawer.current());
	s._showBotsInDrawer = j.value("showBotsInDrawer", defaults._showBotsInDrawer.current());
	s._showNewGroupInDrawer = j.value("showNewGroupInDrawer", defaults._showNewGroupInDrawer.current());
	s._showNewChannelInDrawer = j.value("showNewChannelInDrawer", defaults._showNewChannelInDrawer.current());
	s._showContactsInDrawer = j.value("showContactsInDrawer", defaults._showContactsInDrawer.current());
	s._showCallsInDrawer = j.value("showCallsInDrawer", defaults._showCallsInDrawer.current());
	s._showSavedMessagesInDrawer = j.value("showSavedMessagesInDrawer", defaults._showSavedMessagesInDrawer.current());
	s._showLReadToggleInDrawer = j.value("showLReadToggleInDrawer", defaults._showLReadToggleInDrawer.current());
	s._showSReadToggleInDrawer = j.value("showSReadToggleInDrawer", defaults._showSReadToggleInDrawer.current());
	s._showNightModeToggleInDrawer = j.value("showNightModeToggleInDrawer", defaults._showNightModeToggleInDrawer.current());
	s._showGhostToggleInDrawer = j.value("showGhostToggleInDrawer", defaults._showGhostToggleInDrawer.current());
	s._showStreamerToggleInDrawer = j.value("showStreamerToggleInDrawer", defaults._showStreamerToggleInDrawer.current());
	s._showGhostToggleInTray = j.value("showGhostToggleInTray", defaults._showGhostToggleInTray.current());
	s._showStreamerToggleInTray = j.value("showStreamerToggleInTray", defaults._showStreamerToggleInTray.current());
	s._monoFont = j.value("monoFont", defaults._monoFont.current());
	s._hideNotificationCounters = j.value("hideNotificationCounters", defaults._hideNotificationCounters.current());
	s._hideNotificationBadge = j.value("hideNotificationBadge", defaults._hideNotificationBadge.current());
	s._hideAllChatsFolder = j.value("hideAllChatsFolder", defaults._hideAllChatsFolder.current());
	s._channelBottomButton = j.value("channelBottomButton", defaults._channelBottomButton.current());
	s._quickAdminShortcuts = j.value("quickAdminShortcuts", defaults._quickAdminShortcuts.current());
	s._disableGreetingSticker = j.value("disableGreetingSticker", defaults._disableGreetingSticker.current());
	s._showPeerId = j.value("showPeerId", defaults._showPeerId.current());
	s._showMessageSeconds = j.value("showMessageSeconds", defaults._showMessageSeconds.current());
	s._showLastSeenSeconds = j.value("showLastSeenSeconds", defaults._showLastSeenSeconds.current());
	s._showMessageShot = j.value("showMessageShot", defaults._showMessageShot.current());
	s._filterZalgo = j.value("filterZalgo", defaults._filterZalgo.current());
	s._stickerConfirmation = j.value("stickerConfirmation", defaults._stickerConfirmation.current());
	s._gifConfirmation = j.value("gifConfirmation", defaults._gifConfirmation.current());
	s._voiceConfirmation = j.value("voiceConfirmation", defaults._voiceConfirmation.current());
	s._roundConfirmation = j.value("roundConfirmation", defaults._roundConfirmation.current());
	s._translationProvider = j.value("translationProvider", defaults._translationProvider.current());
	s._adaptiveCoverColor = j.value("adaptiveCoverColor", defaults._adaptiveCoverColor.current());
	s._improveLinkPreviews = j.value("improveLinkPreviews", defaults._improveLinkPreviews.current());
	s._crashReporting = j.value("crashReporting", defaults._crashReporting.current());
	s._avatarCorners = j.value("avatarCorners", defaults._avatarCorners.current());
	s._singleCornerRadius = j.value("singleCornerRadius", defaults._singleCornerRadius.current());
	s._streamerMode = j.value("streamerMode", defaults._streamerMode.current());

	if (j.contains("messageShotSettings") && j["messageShotSettings"].is_object()) {
		j["messageShotSettings"].get_to(s._messageShotSettings);
	}
}

namespace LuxuryRussian {

// Russian overlay for every luxury_* key. Server language packs
// never carry our keys, so without this RU users read English here. Applied through applyValue() on every language
// switch (see Lang::Instance::reset), EN originals restored first, and
// again after the cached pack is loaded at startup (fillFromSerialized).
// Every key listed here must keep an entry in lang.strings;
// check_ru_mirror.py verifies the two stay in sync.
const std::pair<const char *, const char *> kRussianSettingsStrings[] = {
	{ "luxury_PremiumPreviewPrevious", "Предыдущая функция" },
	{ "luxury_PremiumPreviewNext", "Следующая функция" },
	{ "luxury_SearchPreviousResult", "Предыдущий результат" },
	{ "luxury_SearchNextResult", "Следующий результат" },
	{ "luxury_SearchResultsList", "Результаты поиска" },
	{ "luxury_BackgroundPreviewLight", "Предпросмотр в светлом режиме" },
	{ "luxury_BackgroundPreviewDark", "Предпросмотр в тёмном режиме" },
	{ "luxury_ShowAccounts", "Показать аккаунты" },
	{ "luxury_HideAccounts", "Скрыть аккаунты" },
	{ "luxury_ResetScaleAndRestart", "Вернуть масштаб интерфейса к 100% и перезапустить LuxuryGram" },
	{ "luxury_MessageSelfDestructingOpened", "Самоуничтожающееся медиа открыто" },
	{ "luxury_PassportRowReady", "Готово" },
	{ "luxury_PassportRowMissing", "Не хватает данных" },
	{ "luxury_PassportRowError", "Требуется внимание" },
	{ "luxury_PlayerEnterFullscreen", "Открыть на весь экран" },
	{ "luxury_PlayerExitFullscreen", "Выйти из полноэкранного режима" },
	{ "luxury_PlayerPictureInPicture", "Открыть картинку в картинке" },
	{ "luxury_PlayerPosition", "Позиция воспроизведения" },
	{ "luxury_PlayerSettings", "Скорость и качество воспроизведения" },
	{ "luxury_AppIconHeader", "Иконка приложения" },
	{ "luxury_AppIconPickerAccessible", "Выбор иконки приложения. Перемещайтесь стрелками и нажимайте Enter, чтобы применить." },
	{ "luxury_AvatarCorners", "Углы аватаров" },
	{ "luxury_AvatarCornersCircle", "Круглые" },
	{ "luxury_AvatarCornersSquare", "Квадратные" },
	{ "luxury_CaseInsensitiveExpression", "Без учёта регистра" },
	{ "luxury_CategoriesHeader", "Категории" },
	{ "luxury_CategoryAppearance", "Внешний вид" },
	{ "luxury_CategoryChats", "Чаты" },
	{ "luxury_CategoryFilters", "Фильтры" },
	{ "luxury_CategoryGeneral", "Общие" },
	{ "luxury_CategoryOther", "Прочее" },
	{ "luxury_ChannelBottomButton", "Нижняя кнопка канала" },
	{ "luxury_ChannelBottomButtonDiscuss", "Обсудить" },
	{ "luxury_ChannelBottomButtonHide", "Скрыть" },
	{ "luxury_ChannelBottomButtonMute", "Без звука" },
	{ "luxury_ChatFoldersHeader", "Папки чатов" },
	{ "luxury_CollapseSimilarChannels", "Сворачивать похожие каналы" },
	{ "luxury_ConfirmationsTitle", "Подтверждения отправки" },
	{ "luxury_ContextHideMessage", "Скрыть" },
	{ "luxury_ContextMenuElementsHeader", "Элементы контекстного меню" },
	{ "luxury_CrashReporting", "Отчёты о падениях" },
	{ "luxury_CrashReportingDescription", "Если опция включена, после падения приложения будет предложено отправить отчёт. Решение за вами." },
	{ "luxury_DeletedMarkText", "Пометка удалённых" },
	{ "luxury_DisableAds", "Отключить рекламу" },
	{ "luxury_DisableChatSelectionLimit", "Без лимита выделения" },
	{ "luxury_DisableCustomBackgrounds", "Скрыть обои отдельных чатов" },
	{ "luxury_DisableGreetingSticker", "Без приветственного стикера" },
	{ "luxury_DisableNotificationsDelay", "Без задержки уведомлений" },
	{ "luxury_DisableOpenLinkWarning", "Не предупреждать о ссылках" },
	{ "luxury_DisableSimilarChannels", "Без похожих каналов" },
	{ "luxury_DisableStories", "Отключить истории" },
	{ "luxury_DontReadMessages", "Не читать сообщения" },
	{ "luxury_DontReadStories", "Не читать истории" },
	{ "luxury_DontSendOnlinePackets", "Не отправлять онлайн" },
	{ "luxury_DontSendUploadProgress", "Не отправлять набор текста" },
	{ "luxury_DrawerElementsHeader", "Элементы шторки" },
	{ "luxury_EditedMarkText", "Пометка изменений" },
	{ "luxury_EditsHistoryMenuText", "История" },
	{ "luxury_EnableExpression", "Включить фильтр" },
	{ "luxury_EnableGhostModeTray", "Включить гост-режим" },
	{ "luxury_EnableStreamerModeTray", "Включить режим стримера" },
	{ "luxury_ExpireMediaContextMenuText", "Сжечь" },
	{ "luxury_ExtraHeader", "Luxury Extra" },
	{ "luxury_FilterZalgo", "Фильтровать залго" },
	{ "luxury_FiltersClearPopupActionText", "Очистить" },
	{ "luxury_FiltersClearPopupText", "Удалить все фильтры?" },
	{ "luxury_FiltersHideFromBlocked", "Скрывать от заблокированных" },
	{ "luxury_FiltersMenuClear", "Очистить" },
	{ "luxury_FiltersMenuExport", "Экспорт" },
	{ "luxury_FiltersMenuImport", "Импорт" },
	{ "luxury_FiltersMenuSelectChat", "Выбрать чат" },
	{ "luxury_FiltersShadowBan", "Теневой бан" },
	{ "luxury_FontDefault", "По умолчанию" },
	{ "luxury_GIFConfirmation", "GIF" },
	{ "luxury_GhostEssentialsHeader", "Главное для госта" },
	{ "luxury_GhostModeGlobalSettings", "Общие настройки" },
	{ "luxury_GhostModeOptionShiftDescription", "Shift-клик или Shift+Пробел блокирует и разблокирует опцию. Заблокированные опции не меняются при переключении гост-режима." },
	{ "luxury_GhostModeOptionLocked", "Заблокировано. Для разблокировки используйте Shift-клик, Shift+Пробел или действие доступности «Переключить блокировку опции»." },
	{ "luxury_GhostModeToggleOptionLock", "Переключить блокировку опции" },
	{ "luxury_GhostModeSwitchedToGlobalSettings", "Единые настройки для всех аккаунтов." },
	{ "luxury_GhostModeSwitchedToIndividualSettings", "Отдельные настройки для каждого аккаунта." },
	{ "luxury_GhostModeToggle", "Гост-режим" },
	{ "luxury_HideAllChats", "Скрыть вкладку «Все чаты»" },
	{ "luxury_HideNotificationBadge", "Скрыть бейдж уведомлений" },
	{ "luxury_HideNotificationBadgeDescription", "Скрывает счётчик уведомлений на иконке в панели задач и трее." },
	{ "luxury_HideNotificationCounters", "Скрыть счётчики уведомлений" },
	{ "luxury_HidePremiumStatuses", "Скрыть премиум-статусы" },
	{ "luxury_HideReactions", "Скрыть реакции" },
	{ "luxury_HideReactionsInChannels", "Каналы" },
	{ "luxury_HideReactionsInGroups", "Группы" },
	{ "luxury_HideReactionsInPrivateChats", "Личные чаты" },
	{ "luxury_HideShareButton", "Скрыть «Поделиться» сбоку" },
	{ "luxury_HideSimilarChannelsTab", "Скрыть вкладку «Похожие»" },
	{ "luxury_ImproveLinkPreviews", "Улучшить превью ссылок" },
	{ "luxury_LReadMessages", "Читать локально" },
	{ "luxury_LinksContributing", "Контрибьютинг" },
	{ "luxury_LinksDocumentation", "Документация" },
	{ "luxury_LinksHeader", "Ссылки" },
	{ "luxury_LinksIssues", "Задачи" },
	{ "luxury_LinksRepository", "Репозиторий" },
	{ "luxury_LocalPremium", "Локальный Telegram Premium" },
	{ "luxury_LuxuryPreferences", "Настройки LuxuryGram" },
	{ "luxury_MarkReadAfterAction", "Читать при действии" },
	{ "luxury_MarkReadAfterActionDescription", "Автоматически отмечает сообщение прочитанным, когда вы отправляете новое или ставите реакцию." },
	{ "luxury_MaterialSwitches", "Стиль переключателей MD3" },
	{ "luxury_MessageBubbleRadius", "Радиус облачков сообщений" },
	{ "luxury_MessageDetailsPC", "Детали" },
	{ "luxury_MessageDetailsIdPC", "ID" },
	{ "luxury_MessageDetailsPackOwnerFetchingPC", "поиск…" },
	{ "luxury_MessageFieldElementAttach", "Вложения" },
	{ "luxury_MessageFieldElementCommands", "Команды" },
	{ "luxury_MessageFieldElementEmoji", "Эмодзи" },
	{ "luxury_MessageFieldElementTTL", "Автоудаление" },
	{ "luxury_MessageFieldElementVoice", "Голос" },
	{ "luxury_MessageFieldElementsHeader", "Элементы поля ввода" },
	{ "luxury_MessageFieldPopupsHeader", "Всплывашки поля ввода" },
	{ "luxury_MessageSavingOtherHeader", "Прочее" },
	{ "luxury_MessageSavingSaveForBots", "Сохранять в чатах с ботами" },
	{ "luxury_MonospaceFont", "Моноширинный шрифт" },
	{ "luxury_QuickAdminShortcuts", "Быстрые админ-кнопки" },
	{ "luxury_ReadUntilMenuText", "Прочитать сообщение" },
	{ "luxury_RegexFilterBulletinAction", "В текущий чат" },
	{ "luxury_RegexFilterBulletinText", "Фильтр добавлен в **общие фильтры**." },
	{ "luxury_RegexFilterQuickAdd", "Добавить фильтр" },
	{ "luxury_RegexFilters", "Фильтры сообщений" },
	{ "luxury_RegexFiltersAdd", "Добавить фильтр" },
	{ "luxury_RegexFiltersAddError", "Синтаксическая ошибка" },
	{ "luxury_RegexFiltersAmount#one", "{count} фильтр" },
	{ "luxury_RegexFiltersAmount#few", "{count} фильтра" },
	{ "luxury_RegexFiltersAmount#many", "{count} фильтров" },
	{ "luxury_RegexFiltersAmount#other", "{count} фильтра" },
	{ "luxury_RegexFiltersEdit", "Изменить фильтр" },
	{ "luxury_RegexFiltersEnable", "Включить фильтры" },
	{ "luxury_RegexFiltersEnableSharedInChats", "Общие фильтры в чатах" },
	{ "luxury_RegexFiltersExcluded", "Исключённые фильтры" },
	{ "luxury_RegexFiltersExcludedAmount#one", "{count} исключение" },
	{ "luxury_RegexFiltersExcludedAmount#few", "{count} исключения" },
	{ "luxury_RegexFiltersExcludedAmount#many", "{count} исключений" },
	{ "luxury_RegexFiltersExcludedAmount#other", "{count} исключения" },
	{ "luxury_RegexFiltersHeader", "Фильтры" },
	{ "luxury_RegexFiltersListEmpty", "Фильтров пока нет." },
	{ "luxury_RegexFiltersLoading", "Загрузка…" },
	{ "luxury_RegexFiltersPlaceholder", "Выражение" },
	{ "luxury_RegexFiltersShared", "Общие фильтры" },
	{ "luxury_RegisterURLScheme", "Зарегистрировать URL-схему" },
	{ "luxury_RemoveMessageTail", "Убирать хвост сообщений" },
	{ "luxury_RepeatMessage", "Повторить сообщение" },
	{ "luxury_ForwardOptions", "Параметры пересылки" },
	{ "luxury_ForwardToSaved", "Переслать в Избранное" },
	{ "luxury_ForwardVariantsMenuText", "Варианты пересылки" },
	{ "luxury_ReplaceMarksWithIcons", "Заменить на иконки" },
	{ "luxury_ResetSettings", "Сбросить настройки" },
	{ "luxury_ResetSettingsConfirmation", "Сбросить **все** настройки LuxuryGram к значениям по умолчанию?" },
	{ "luxury_ReversedExpression", "Инверсия" },
	{ "luxury_RoundConfirmation", "Круглые видео" },
	{ "luxury_SReadMessages", "Читать на сервере" },
	{ "luxury_SaveDeletedMessages", "Сохранять удалённые" },
	{ "luxury_SaveDeletedMessagesEvenWhenLocked", "Сохранять под паролем" },
	{ "luxury_SaveDeletedMessagesEvenWhenLockedDescription", "Удалённые сообщения сохраняются, даже пока приложение заперто локальным паролем; выключите, чтобы приостановить сохранение при блокировке." },
	{ "luxury_SaveMessagesHistory", "Сохранять историю правок" },
	{ "luxury_SemiTransparentDeletedMessages", "Полупрозрачные удалённые" },
	{ "luxury_SendOfflinePacketAfterOnline", "Сразу уходить в офлайн" },
	{ "luxury_SendWithoutSoundByDefault", "Отправлять без звука" },
	{ "luxury_SendWithoutSoundByDefaultAlways", "Всегда" },
	{ "luxury_SendWithoutSoundByDefaultDescription", "Исходящие сообщения по умолчанию отправляются без звука." },
	{ "luxury_SendWithoutSoundByDefaultInGhostMode", "В гост-режиме" },
	{ "luxury_SendWithoutSoundByDefaultNever", "Никогда" },
	{ "luxury_SettingsBiggerWindow", "Большее окно" },
	{ "luxury_SettingsContextMenuDescription", "Пункты «С Ctrl/Shift» появляются, если при правом клике по сообщению зажать Ctrl или Shift." },
	{ "luxury_SettingsContextMenuItemExtended", "С Ctrl/Shift" },
	{ "luxury_SettingsContextMenuItemHidden", "Скрыто" },
	{ "luxury_SettingsContextMenuItemShown", "Показано" },
	{ "luxury_SettingsContextMenuReactionsPanel", "Панель реакций" },
	{ "luxury_SettingsContextMenuTitle", "Когда показывать пункт" },
	{ "luxury_SettingsContextMenuViewsPanel", "Панель просмотров" },
	{ "luxury_SettingsDescription", "Форк Telegram Desktop с акцентом на кастомизацию и функции приватности." },
	{ "luxury_SettingsIncreaseWebviewHeight", "Увеличить высоту контента" },
	{ "luxury_SettingsIncreaseWebviewWidth", "Увеличить ширину контента" },
	{ "luxury_SettingsShowID", "Показывать ID" },
	{ "luxury_SettingsShowID_Hide", "Скрыть" },
	{ "luxury_SettingsShowLastSeenSeconds", "Показывать секунды онлайна" },
	{ "luxury_SettingsShowMessageSeconds", "Показывать секунды сообщений" },
	{ "luxury_SettingsShowMessageShot", "Снимок сообщений" },
	{ "luxury_SettingsShowMessageShotDescription", "Позволяет делать снимок выбранных сообщений." },
	{ "luxury_SettingsMiniAppsHeader", "Мини-приложения" },
	{ "luxury_SettingsSpoofWebviewAsAndroid", "Притворяться Android" },
	{ "luxury_SettingsUnlimitedRecentStickers", "Недавние стикеры без лимита" },
	{ "luxury_SettingsUnlimitedFavedStickers", "Избранные стикеры без лимита" },
	{ "luxury_SettingsFavedReorderHint", "Клик правой кнопкой по избранному стикеру — «Переместить в начало»; клик средней кнопкой перемещает сразу. Неограниченные избранные живут на этом устройстве: переставленные сохраняют порядок, а обрезанный сервером список следует серверу." },
	{ "luxury_SettingsWideMultiplier", "Множитель ширины сообщений" },
	{ "luxury_SettingsWideMultiplierDescription", "Меняйте ширину сообщений для широких мониторов." },
	{ "luxury_ShadowBanEnabled", "Сообщения от этого отправителя скрыты в ваших чатах." },
	{ "luxury_ShadowBanDisabled", "Сообщения от этого отправителя снова видны в ваших чатах." },
	{ "luxury_ShadowBanListHeader", "Скрытые отправители" },
	{ "luxury_ShadowBanListEmpty", "Скрытых отправителей пока нет. Нажмите + вверху и выберите пользователя или бота, чьи сообщения будут скрыты в ваших чатах." },
	{ "luxury_ShowOnlyAddedEmojisAndStickers", "Только мои эмодзи и стикеры" },
	{ "luxury_SimpleQuotesAndReplies", "Отключить цветные ответы" },
	{ "luxury_SingleCornerRadius", "Единый радиус углов" },
	{ "luxury_SingleCornerRadiusDescription", "У форумов будет та же форма аватаров, что у чатов." },
	{ "luxury_MenuRadius", "Радиус углов меню" },
	{ "luxury_SpyEssentialsHeader", "Главное для шпиона" },
	{ "luxury_StickerConfirmation", "Стикеры" },
	{ "luxury_StreamerModeToggle", "Режим стримера" },
	{ "luxury_SuggestGhostModeBeforeViewingStory", "Спрашивать перед историей" },
	{ "luxury_SuggestGhostModeBeforeViewingStoryDescription", "Показывает предупреждение перед открытием истории с предложением включить гост-режим." },
	{ "luxury_TrackOnlineEvenWhenLocked", "Следить под паролем" },
	{ "luxury_TrackOnlineEvenWhenLockedDescription", "История онлайна записывается, даже пока приложение заперто локальным паролем; выключите, чтобы приостановить запись при блокировке." },
	{ "luxury_TrackOnlineHistory", "Следить за онлайном" },
	{ "luxury_TranslationProvider", "Провайдер перевода" },
	{ "luxury_TrayElementsHeader", "Элементы трея" },
	{ "luxury_UnknownDC", "Неизвестно" },
	{ "luxury_DCLocationMiami", "Майами, США" },
	{ "luxury_DCLocationAmsterdam", "Амстердам, Нидерланды" },
	{ "luxury_DCLocationSingapore", "Сингапур" },
	{ "luxury_UnknownPeer", "Неизвестно (ID: {id})" },
	{ "luxury_IdValue", "ID: {id}" },
	{ "luxury_UnknownSection", "Неизвестный раздел." },
	{ "luxury_UseScheduledMessages", "Откладывать сообщения" },
	{ "luxury_UseScheduledMessagesDescription", "Автоматически откладывает исходящие примерно на 12 секунд (медиа — дольше). С этой функцией вы не появляетесь в сети. Не используйте на нестабильной сети." },
	{ "luxury_UserMessagesMenuText", "Сообщения пользователя" },
	{ "luxury_VoiceConfirmation", "Голосовые сообщения" },
	{ "luxury_WatchChatStop", "Не скачивать медиа" },
	{ "luxury_WatchChatsHeader", "Отслеживаемые чаты" },
	{ "luxury_WatchChatsEmpty", "Отслеживаемых чатов пока нет. Откройте меню LuxuryGram в чате и выберите «Скачивать медиа»." },
	{ "luxury_WatchChatsRecordingNote", "Медиа из этих чатов сохраняется, даже пока приложение заперто локальным паролем." },
	{ "luxury_DeletedHistoryEmptyTitle", "Сохранённых сообщений пока нет" },
	{ "luxury_DeletedHistoryEmptyText", "Сообщения, удалённые в этом чате, появятся здесь, когда включена настройка «Сохранять удалённые»." },
	{ "luxury_DeletedHistoryNoResultsText", "Сохранённых сообщений, содержащих «{query}», нет." },
	{ "luxury_EditsHistoryEmptyTitle", "Сохранённых версий пока нет" },
	{ "luxury_EditsHistoryEmptyText", "Изменённые версии этого сообщения появятся здесь, когда включена настройка «Сохранять историю правок»." },
	{ "luxury_ViewOnlineHistoryMenuText", "Наблюдатель" },
	{ "luxury_ClearOnlineHistoryMenuText", "Очистить данные наблюдателя" },
	{ "luxury_ClearOnlineHistoryText", "Очистить все данные наблюдателя для этого чата? Будут удалены история онлайна и события; список сообщений не затрагивается." },
	{ "luxury_WatcherClearKeptFilesAlso", "Также удалить сохранённые файлы" },
	{ "luxury_WatcherClearAction", "Очистить" },
	{ "luxury_OnlineHistoryTitle", "Наблюдатель" },
	{ "luxury_OnlineHistorySessions", "Сессии" },
	{ "luxury_OnlineHistoryEvents", "События" },
	{ "luxury_OnlineHistorySessionsEmpty", "Сессий пока нет." },
	{ "luxury_OnlineHistoryEventsEmpty", "Пока ничего нет." },
	{ "luxury_OnlineHistorySortAll", "Все" },
	{ "luxury_OnlineHistorySortSessions", "Сессии" },
	{ "luxury_OnlineHistorySortEvents", "События" },
	{ "luxury_OnlineHistorySortNewest", "Новые" },
	{ "luxury_OnlineHistorySortOldest", "Старые" },
	{ "luxury_OnlineHistorySortLongest", "Длинные" },
	{ "luxury_OnlineHistoryStart", "Начало" },
	{ "luxury_OnlineHistoryEnd", "Конец" },
	{ "luxury_OnlineHistoryOpen", "Сейчас онлайн" },
	{ "luxury_OnlineHistoryUnknown", "неизвестно" },
	{ "luxury_OnlineHistoryRefresh", "Обновить" },
	{ "luxury_OnlineHistorySettings", "Настройки" },
	{ "luxury_OnlineHistoryKindGift", "Подарок" },
	{ "luxury_OnlineHistoryKindName", "Имя" },
	{ "luxury_OnlineHistoryKindUsername", "Юзернейм" },
	{ "luxury_OnlineHistoryKindPhoto", "Фото" },
	{ "luxury_OnlineHistoryKindDeleted", "Удалено" },
	{ "luxury_OnlineHistoryKindEdited", "Изменено" },
	{ "luxury_OnlineHistoryKindReaction", "Реакция" },
	{ "luxury_WatchReactions", "Реакции: {list}" },
	{ "luxury_WatchEditWas", "Было:" },
	{ "luxury_WatchEditNow", "Стало:" },
	{ "luxury_WatcherEndUnobserved", "Выход не зафиксирован" },
	{ "luxury_WatcherTrackingHint", "Только наблюдаемые события. Пропуски — не время онлайн." },
	{ "luxury_WatcherTrackingPaused", "Запись приостановлена. Сохранённая история доступна." },
	{ "luxury_WatcherUpdatesPaused", "Пауза, пока зажата кнопка мыши" },
	{ "luxury_WatcherMenuHeader", "Наблюдатель" },
	{ "luxury_WatcherOpenSettings", "Открыть настройки LuxuryGram…" },
	{ "luxury_WatcherLockDisclosure", "История онлайна записывается, даже пока приложение заперто локальным паролем; выключите «Следить под паролем», чтобы приостановить её." },
	{ "luxury_WatcherMediaSaveFailed#one", "Не удалось сохранить {count} медиафайл из отслеживаемого чата" },
	{ "luxury_WatcherMediaSaveFailed#few", "Не удалось сохранить {count} медиафайла из отслеживаемого чата" },
	{ "luxury_WatcherMediaSaveFailed#many", "Не удалось сохранить {count} медиафайлов из отслеживаемого чата" },
	{ "luxury_WatcherMediaSaveFailed#other", "Не удалось сохранить {count} медиафайла из отслеживаемого чата" },
	{ "luxury_WatcherLoading", "Загружаем сохранённую историю…" },
	{ "luxury_WatcherExpand", "Показать полностью" },
	{ "luxury_WatcherCollapse", "Свернуть текст" },
	{ "luxury_WatcherShowMore", "Показать ещё записи" },
	{ "luxury_WatcherOpenMessage", "К сообщению" },
	{ "luxury_WatcherMessageId", "Сообщение №{id}" },
	{ "luxury_WatcherMessageText", "Текст сообщения" },
	{ "luxury_WatcherMessageEdited", "Сообщение изменено" },
	{ "luxury_WatcherMessageDeleted", "Сообщение удалено" },
	{ "luxury_WatcherCurrentText", "Текущий текст" },
	{ "luxury_WatcherNextSavedText", "Следующий сохранённый текст" },
	{ "luxury_WatcherCustomReaction", "Кастомный эмодзи №{id}" },
	{ "luxury_WatcherEmptyValue", "Пусто" },
	{ "luxury_WatcherReactionsRemoved", "Все реакции сняты" },
	{ "luxury_WatcherContextUnavailable", "Текст сообщения для этого события не был сохранён." },
	{ "luxury_WatcherRevisionUnavailable", "Сохранён только прежний текст. Следующая версия недоступна." },
	{ "luxury_WatchChat", "Скачивать медиа" },
	{ "luxury_OnlineHistoryEarlier#one", "+{count} запись раньше" },
	{ "luxury_OnlineHistoryEarlier#few", "+{count} записи раньше" },
	{ "luxury_OnlineHistoryEarlier#many", "+{count} записей раньше" },
	{ "luxury_OnlineHistoryEarlier#other", "+{count} записей раньше" },
	{ "luxury_WatchChatEnabled", "Медиа в этом чате скачивается по мере появления и сохраняется, даже если сообщение удалено." },
	{ "luxury_WatchChatDisabled", "Наблюдение остановлено. Уже сохранённые вложения остаются." },
	{ "luxury_WatchChatLimit", "Отслеживаемых чатов уже слишком много." },
	{ "luxury_WatchGiftSent", "Вы отправили подарок за {cost} пользователю {user}" },
	{ "luxury_WatchGiftReceived", "{user} отправил(а) вам подарок за {cost}" },
	{ "luxury_WatchGiftReceivedAnonymous", "Неизвестный пользователь отправил вам подарок за {cost}" },
	{ "luxury_WatchNameChanged", "Изменил(а) имя" },
	{ "luxury_WatchUsernameChanged", "Изменил(а) имя пользователя" },
	{ "luxury_WatchPhotoUpdated", "Обновил(а) фото профиля" },
	{ "luxury_BoxActionReset", "Сбросить" },
	{ "luxury_CustomizeFontTitle", "Настройка шрифта" },
	{ "luxury_NoFontsFound", "Шрифты не найдены." },
	{ "luxury_FiltersExportClipboard", "Скопировать в буфер обмена" },
	{ "luxury_FiltersExportURL", "Опубликовать на dpaste.com" },
	{ "luxury_FiltersImportClipboard", "Вставить из буфера обмена" },
	{ "luxury_FiltersImportURL", "Импортировать по ссылке" },
	{ "luxury_MessageShotAnonymous", "Анонимно" },
	{ "luxury_MessageShotCopied", "Изображение скопировано в буфер обмена." },
	{ "luxury_MessageShotCopy", "Скопировать" },
	{ "luxury_MessageShotEmpty", "Нечего сохранять: снимок слишком большой, либо сообщения пропали." },
	{ "luxury_MessageShotPreferences", "Настройки" },
	{ "luxury_MessageShotPreview", "Предпросмотр" },
	{ "luxury_MessageShotRevealSpoilers", "Раскрывать спойлеры" },
	{ "luxury_MessageShotSave", "Сохранить" },
	{ "luxury_MessageShotSaveFailed", "Не удалось сохранить изображение." },
	{ "luxury_MessageShotThemePreviewFailed", "Не удалось загрузить файл темы." },
	{ "luxury_MessageShotShowBackground", "Показывать фон" },
	{ "luxury_MessageShotShowColorfulReplies", "Показывать цветные ответы" },
	{ "luxury_MessageShotShowDate", "Показывать дату" },
	{ "luxury_MessageShotShowHeaderDecorations", "Показывать украшения шапки" },
	{ "luxury_MessageShotShowReactions", "Показывать реакции" },
	{ "luxury_MessageShotTheme", "Тема" },
	{ "luxury_MessageShotTopBarText", "Снимок" },
	{ "luxury_MessageShotThemeApply", "Применить" },
	{ "luxury_MessageShotThemeDefault", "По умолчанию" },
	{ "luxury_MessageShotThemeSelectTitle", "Выбор темы сообщения" },
	{ "luxury_MessageShotTooLarge", "Извините, снимок слишком большой." },
	{ "luxury_PluginNoAuthor", "Неизвестный автор" },
	{ "luxury_PluginNoDescription", "Описание отсутствует." },
	{ "luxury_PluginVersion", "Версия" },
	{ "luxury_PluginsNotAvailable", "Плагины пока не поддерживаются в LuxuryGram Desktop." },
	{ "luxury_PreviewReplyText", "Когда апдейт?" },
	{ "luxury_PreviewMessageText", "Тебе бы выйти на улицу и потрогать траву..." },
	{ "luxury_PreviewAvatarRowText", "Лучше поздно, чем никогда" },
	{ "luxury_PreviewAvatarAccessibleName", "Пример строки чата для углов аватара; открывает страницу релизов LuxuryGram." },
	{ "luxury_WatchShowSavedMedia", "Показать сохранённый файл" },
	{ "luxury_StickersMoveToFront", "Переместить в начало" },
	{ "luxury_DatabaseError", "Не удалось сохранить изменения в базе данных." },
	{ "luxury_RegexFiltersPatternTooLong", "Слишком длинный шаблон." },
	{ "luxury_TranslateBatchFailed", "Не удалось перевести: {text}." },
	{ "luxury_TranslateBatchOthersDone#one", "Ещё {count} сообщение переведено." },
	{ "luxury_TranslateBatchOthersDone#few", "Ещё {count} сообщения переведены." },
	{ "luxury_TranslateBatchOthersDone#many", "Ещё {count} сообщений переведено." },
	{ "luxury_TranslateBatchOthersDone#other", "Ещё {count} сообщения переведено." },
	{ "luxury_FiltersToastFailUnsupported", "Резервная копия не поддерживается или содержит слишком много записей." },
	{ "luxury_MessageShotAnonymousName", "Пользователь {index}" },
	{ "luxury_LuxuryForwardStatusPreparing", "Пересылка сообщений" },
	{ "luxury_LuxuryForwardStatusLoadingMedia", "Загрузка файлов" },
	{ "luxury_LuxuryForwardStatusForwarding", "Пересылка сообщений" },
	{ "luxury_LuxuryForwardStatusFinished", "Готово" },
	{ "luxury_LuxuryForwardStatusSentCount", "отправлено {count1} из {count2}" },
	{ "luxury_LuxuryForwardStatusChunkCount", "часть {count1} из {count2}" },
	{ "luxury_LuxuryForwardStatusSkippedCount#one", "пропущено {count}" },
	{ "luxury_LuxuryForwardStatusSkippedCount#few", "пропущено {count}" },
	{ "luxury_LuxuryForwardStatusSkippedCount#many", "пропущено {count}" },
	{ "luxury_LuxuryForwardStatusSkippedCount#other", "пропущено {count}" },
	{ "luxury_LuxuryForwardStopSure", "Остановить пересылку? Уже отправленные сообщения останутся в чате." },
	{ "luxury_RegistrationDateUnavailable", "Не удалось узнать дату создания." },
	{ "luxury_LastSeenAgo", "({elapsed} назад)" },
	{ "luxury_UpdateLuxuryGram", "Обновить LuxuryGram" },
	{ "luxury_GitHubReleases", "Релизы на GitHub" },
	{ "luxury_ContextSelectBetween", "Выделить сообщения между" },
	{ "luxury_PlayerExpand", "Открыть расширенный плеер" },
	{ "luxury_PlayerTitle", "Сейчас играет" },
	{ "luxury_PlayerSaveCover", "Сохранить обложку" },
	{ "luxury_PlayerSaveCoverFailed", "Не удалось сохранить обложку." },
	{ "luxury_PlayerOpenCover", "Открыть обложку в просмотрщике" },
	{ "luxury_SavedMusicLoadError", "Не удалось загрузить музыку" },
	{ "luxury_SavedMusicRetry", "Попробовать снова" },
	{ "luxury_StartupInterface", "Загрузка интерфейса…" },
	{ "luxury_StartupWindow", "Подготовка окна…" },
	{ "luxury_StartupAccounts", "Открытие аккаунтов…" },
	{ "luxury_StartupShowing", "Почти готово…" },
	{ "luxury_ClearDeletedMenuText", "Очистить удалённые" },
	{ "luxury_ClearDeletedMessagesActionText", "Очистить" },
	{ "luxury_ClearDeletedMessagesText", "Очистить все удалённые сообщения в этом чате?" },
	{ "luxury_ConfirmationGIF", "Отправить этот GIF?" },
	{ "luxury_ConfirmationRound", "Отправить это видеосообщение?" },
	{ "luxury_ConfirmationSticker", "Отправить этот стикер?" },
	{ "luxury_ConfirmationVoice", "Отправить это голосовое сообщение?" },
	{ "luxury_ContextCopyCallbackData", "Копировать callback-данные" },
	{ "luxury_ContextCopyID", "Копировать ID" },
	{ "luxury_CreationDateChat", "**{item1}**: дата создания **{item2}**." },
	{ "luxury_CreationDateSelfApproximately", "Ваш аккаунт создан примерно **{item}**." },
	{ "luxury_CreationDateSelfEarlier", "Ваш аккаунт создан до **{item}**." },
	{ "luxury_CreationDateSelfLater", "Ваш аккаунт создан после **{item}**." },
	{ "luxury_CreationDateUserApproximately", "**{item1}**: аккаунт создан примерно **{item2}**." },
	{ "luxury_CreationDateUserEarlier", "**{item1}**: аккаунт создан до **{item2}**." },
	{ "luxury_CreationDateUserLater", "**{item1}**: аккаунт создан после **{item2}**." },
	{ "luxury_JoinDateChat", "**{item1}**: вы присоединились **{item2}**." },
	{ "luxury_DeleteOwnMessages", "Удалить свои сообщения" },
	{ "luxury_DeleteOwnMessagesConfirmation", "Удалить все ваши сообщения в этой группе?" },
	{ "luxury_DisableColorfulCover", "Отключить адаптивный цвет" },
	{ "luxury_EnableColorfulCover", "Включить адаптивный цвет" },
	{ "luxury_DisableGhostModeTray", "Выключить гост-режим" },
	{ "luxury_DisableStreamerModeTray", "Выключить режим стримера" },
	{ "luxury_GhostModeShortcut", "Запустить в гост-режиме" },
	{ "luxury_ExpiringVideoMessageNote", "Это видеосообщение можно смотреть сколько угодно раз." },
	{ "luxury_ExpiringVoiceMessageNote", "Это голосовое сообщение можно слушать сколько угодно раз." },
	{ "luxury_OncePhotoMessageNote", "Это фото можно смотреть сколько угодно раз." },
	{ "luxury_OnceVideoMessageNote", "Это видео можно смотреть сколько угодно раз." },
	{ "luxury_FiltersQuickShadowBan", "Теневой бан" },
	{ "luxury_FiltersQuickUnshadowBan", "Снять теневой бан" },
	{ "luxury_FiltersSheetTitle", "Применить фильтры?" },
	{ "luxury_FiltersSheetNewFilters#one", "**{count}** новый фильтр" },
	{ "luxury_FiltersSheetNewFilters#few", "**{count}** новых фильтра" },
	{ "luxury_FiltersSheetNewFilters#many", "**{count}** новых фильтров" },
	{ "luxury_FiltersSheetNewFilters#other", "**{count}** нового фильтра" },
	{ "luxury_FiltersSheetRemovedFilters#one", "**{count}** удалённый фильтр" },
	{ "luxury_FiltersSheetRemovedFilters#few", "**{count}** удалённых фильтра" },
	{ "luxury_FiltersSheetRemovedFilters#many", "**{count}** удалённых фильтров" },
	{ "luxury_FiltersSheetRemovedFilters#other", "**{count}** удалённого фильтра" },
	{ "luxury_FiltersSheetUpdatedFilters#one", "**{count}** обновлённый фильтр" },
	{ "luxury_FiltersSheetUpdatedFilters#few", "**{count}** обновлённых фильтра" },
	{ "luxury_FiltersSheetUpdatedFilters#many", "**{count}** обновлённых фильтров" },
	{ "luxury_FiltersSheetUpdatedFilters#other", "**{count}** обновлённого фильтра" },
	{ "luxury_FiltersSheetNewExclusions#one", "**{count}** новое исключение" },
	{ "luxury_FiltersSheetNewExclusions#few", "**{count}** новых исключения" },
	{ "luxury_FiltersSheetNewExclusions#many", "**{count}** новых исключений" },
	{ "luxury_FiltersSheetNewExclusions#other", "**{count}** нового исключения" },
	{ "luxury_FiltersSheetRemovedExclusions#one", "**{count}** удалённое исключение" },
	{ "luxury_FiltersSheetRemovedExclusions#few", "**{count}** удалённых исключения" },
	{ "luxury_FiltersSheetRemovedExclusions#many", "**{count}** удалённых исключений" },
	{ "luxury_FiltersSheetRemovedExclusions#other", "**{count}** удалённого исключения" },
	{ "luxury_FiltersSheetDialogsToResolve#one", "**{count}** чат будет загружен" },
	{ "luxury_FiltersSheetDialogsToResolve#few", "**{count}** чата будут загружены" },
	{ "luxury_FiltersSheetDialogsToResolve#many", "**{count}** чатов будут загружены" },
	{ "luxury_FiltersSheetDialogsToResolve#other", "**{count}** чата будут загружены" },
	{ "luxury_FiltersToastFailFetch", "Не удалось загрузить фильтры." },
	{ "luxury_FiltersToastFailImport", "Не удалось импортировать фильтры." },
	{ "luxury_FiltersToastFailNoChanges", "Фильтры совпадают с текущими." },
	{ "luxury_FiltersToastFailPublish", "Не удалось опубликовать фильтры." },
	{ "luxury_FiltersToastSuccess", "Фильтры импортированы." },
	{ "luxury_HideFilteredMessagesMenuText", "Скрыть отфильтрованные" },
	{ "luxury_ShowFilteredMessagesMenuText", "Показать отфильтрованные" },
	{ "luxury_ViewFiltersMenuText", "Показать фильтры" },
	{ "luxury_ViewDeletedMenuText", "Показать удалённые" },
	{ "luxury_IDCopiedToast", "ID скопирован в буфер обмена." },
	{ "luxury_JumpToBeginning", "В начало" },
	{ "luxury_LocalPremiumNotice", "Вы используете **локальный** Telegram Premium.\nОн **не даёт** никаких преимуществ.\n**Наслаждайтесь звездой рядом с именем!**" },
	{ "luxury_MessageDetailsDatacenterPC", "Дата-центр" },
	{ "luxury_MessageDetailsDatePC", "Дата" },
	{ "luxury_MessageDetailsEditedDatePC", "Дата изменения" },
	{ "luxury_MessageDetailsFileNamePC", "Имя файла" },
	{ "luxury_MessageDetailsFileSizePC", "Размер файла" },
	{ "luxury_MessageDetailsForwardedDatePC", "Дата пересылки" },
	{ "luxury_MessageDetailsMimeTypePC", "MIME-тип" },
	{ "luxury_MessageDetailsPackOwnerNotFoundPC", "не найден" },
	{ "luxury_MessageDetailsPackOwnerPC", "Владелец набора" },
	{ "luxury_MessageDetailsResolutionPC", "Разрешение" },
	{ "luxury_MessageDetailsSharesPC", "Пересылки" },
	{ "luxury_MessageDetailsViewsPC", "Просмотры" },
	{ "luxury_MutualContactInfo", "Это взаимный контакт." },
	{ "luxury_ReadConfirmationBoxActionText", "Прочитать" },
	{ "luxury_ReadConfirmationBoxQuestion", "Прочитать все сообщения?" },
	{ "luxury_SearchByIDResults", "Найдено по ID" },
	{ "luxury_SendAsSticker", "Отправить как стикер" },
	{ "luxury_SendWithSound", "Отправить со звуком" },
	{ "luxury_SessionInfoOfficialApp", "Официальное приложение" },
	{ "luxury_SuggestGhostModeTitle", "Гост-режим" },
	{ "luxury_SuggestGhostModeStoryText", "Включить **гост-режим** перед просмотром истории?" },
	{ "luxury_SuggestGhostModeStoryActionTextYes", "Да" },
	{ "luxury_SuggestGhostModeStoryActionTextNo", "Нет" },
	{ "luxury_UnforwardableContextMenuText", "Обычная пересылка запрещена." },
	{ "luxury_UserNotFoundMessage", "Пользователь не найден." },
	{ "luxury_ChatNotFoundMessage", "Чат не найден." },
	{ "lng_luxury_plugins_ui_title", "Плагины" },
	{ "lng_luxury_plugins_ui_install_github", "Установить с GitHub" },
	{ "lng_luxury_plugins_ui_check_updates", "Проверить обновления" },
	{ "lng_luxury_plugins_ui_checking", "Проверяем обновления плагинов…" },
	{ "lng_luxury_plugins_ui_updates_found", "Доступно обновлений: {number}" },
	{ "lng_luxury_plugins_ui_updates_none", "Все плагины обновлены." },
	{ "lng_luxury_plugins_ui_about", "Плагины выполняют JavaScript внутри LuxuryGram только с разрешёнными вами правами. Устанавливайте их только от авторов, которым доверяете." },
	{ "lng_luxury_plugins_ui_installed_title", "Установленные плагины" },
	{ "lng_luxury_plugins_ui_empty", "Плагинов пока нет. Нажмите «Установить с GitHub» и вставьте ссылку GitHub вида owner/repo." },
	{ "lng_luxury_plugins_ui_unavailable", "Плагины сейчас недоступны." },
	{ "lng_luxury_plugins_ui_state_running", "Работает" },
	{ "lng_luxury_plugins_ui_state_disabled", "Выключен" },
	{ "lng_luxury_plugins_ui_state_update", "Доступно обновление: {version}" },
	{ "lng_luxury_plugins_ui_details", "Подробнее" },
	{ "lng_luxury_plugins_ui_install_title", "Установка плагина" },
	{ "lng_luxury_plugins_ui_install_about", "Вставьте ссылку GitHub вида owner/repo, owner/repo@tag или адрес репозитория." },
	{ "lng_luxury_plugins_ui_install_placeholder", "Ссылка GitHub" },
	{ "lng_luxury_plugins_ui_install", "Продолжить" },
	{ "lng_luxury_plugins_ui_install_failed", "Не удалось установить плагин: {error}" },
	{ "lng_luxury_plugins_ui_installed", "Плагин {name} установлен." },
	{ "lng_luxury_plugins_ui_updated", "Плагин {name} обновлён." },
	{ "lng_luxury_plugins_ui_consent_title", "Установить плагин?" },
	{ "lng_luxury_plugins_ui_consent_update_title", "Обновить плагин?" },
	{ "lng_luxury_plugins_ui_source", "Источник: {name}" },
	{ "lng_luxury_plugins_ui_consent_none", "Плагин не запрашивает разрешений." },
	{ "lng_luxury_plugins_ui_consent_requests", "Плагин запрашивает:" },
	{ "lng_luxury_plugins_ui_consent_new", "Новая версия дополнительно запрашивает:" },
	{ "lng_luxury_plugins_ui_consent_hosts", "Доступ в сеть к:" },
	{ "lng_luxury_plugins_ui_any_website", "любой сайт" },
	{ "lng_luxury_plugins_ui_consent_danger", "Красные разрешения дают плагину доступ к вашим сообщениям, аккаунту или данным. Разрешайте их, только если доверяете автору." },
	{ "lng_luxury_plugins_ui_consent_allow", "Разрешить и установить" },
	{ "lng_luxury_plugins_ui_granted_title", "Выданные разрешения" },
	{ "lng_luxury_plugins_ui_granted_none", "Нет разрешений." },
	{ "lng_luxury_plugins_ui_settings_title", "Настройки" },
	{ "lng_luxury_plugins_ui_setting_range", "От {from} до {to}" },
	{ "lng_luxury_plugins_ui_logs_title", "Журнал" },
	{ "lng_luxury_plugins_ui_logs_empty", "Записей пока нет." },
	{ "lng_luxury_plugins_ui_logs_copy", "Скопировать журнал" },
	{ "lng_luxury_plugins_ui_logs_clear", "Очистить журнал" },
	{ "lng_luxury_plugins_ui_update", "Обновить" },
	{ "lng_luxury_plugins_ui_reload", "Перезапустить" },
	{ "lng_luxury_plugins_ui_reloaded", "Плагин {name} перезапущен." },
	{ "lng_luxury_plugins_ui_remove", "Удалить плагин" },
	{ "lng_luxury_plugins_ui_remove_sure", "Удалить плагин {name} вместе с его настройками и данными?" },
	{ "lng_luxury_plugins_ui_perm_messages_read", "Чтение сообщений" },
	{ "lng_luxury_plugins_ui_perm_messages_read_about", "Видит все входящие сообщения этого аккаунта во всех чатах и сообщения в открытых вами чатах." },
	{ "lng_luxury_plugins_ui_perm_messages_send", "Отправка сообщений" },
	{ "lng_luxury_plugins_ui_perm_messages_send_about", "Отправляет сообщения от вашего имени." },
	{ "lng_luxury_plugins_ui_perm_messages_modify", "Изменение исходящих сообщений" },
	{ "lng_luxury_plugins_ui_perm_messages_modify_about", "Меняет текст сообщений перед отправкой." },
	{ "lng_luxury_plugins_ui_perm_ui", "Интерфейс" },
	{ "lng_luxury_plugins_ui_perm_ui_about", "Добавляет пункты меню и команды, показывает уведомления и окна." },
	{ "lng_luxury_plugins_ui_perm_network", "Сеть" },
	{ "lng_luxury_plugins_ui_perm_network_about", "Отправляет веб-запросы к указанным сайтам." },
	{ "lng_luxury_plugins_ui_perm_storage", "Хранилище" },
	{ "lng_luxury_plugins_ui_perm_storage_about", "Хранит свои данные на этом компьютере." },
	{ "lng_luxury_plugins_ui_perm_clipboard", "Буфер обмена" },
	{ "lng_luxury_plugins_ui_perm_clipboard_about", "Читает и меняет буфер обмена сразу после того, как вы запустили команду, пункт меню или горячую клавишу плагина. Скопированные пароли и коды входа можно прочитать." },
	{ "lng_luxury_plugins_ui_perm_telegram_api", "Telegram API" },
	{ "lng_luxury_plugins_ui_perm_telegram_api_about", "Выполняет запросы Telegram от вашего аккаунта." },
	{ "lng_luxury_plugins_ui_perm_telegram_updates", "Обновления Telegram" },
	{ "lng_luxury_plugins_ui_perm_telegram_updates_about", "Получает все обновления, которые ваш аккаунт получает от Telegram." },
	{ "lng_luxury_plugins_ui_state_stopped", "Включён, не запущен" },
	{ "lng_luxury_plugins_ui_consent_new_hosts", "Новая версия обращается к новым сайтам:" },
};

void applyIfRussian(not_null<Lang::Instance*> instance, const QString &id) {
	if (id != QLatin1String("ru")) {
		return;
	}
	for (const auto &[key, ru] : kRussianSettingsStrings) {
		instance->applyValue(key, ru);
	}
}

} // namespace LuxuryRussian
