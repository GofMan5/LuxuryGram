// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "luxury/luxury_infra.h"

#include "luxury/luxury_lang.h"
#include "luxury/luxury_settings.h"
#include "luxury/luxury_ui_settings.h"
#include "luxury/luxury_worker.h"
#include "luxury/data/luxury_database.h"
#include "luxury/features/filters/filters_cache_controller.h"
#include "luxury/ui/luxury_logo.h"
#include "features/translator/luxury_translator.h"
#include "lang/lang_instance.h"
#include "ui/chat/chat_style_radius.h"

#ifdef Q_OS_WIN
#include "luxury/utils/windows_utils.h"
#endif

namespace LuxuryInfra {

void initLang() {
	QString id = Lang::GetInstance().id();
	QString baseId = Lang::GetInstance().baseId();
	if (id.isEmpty()) {
		LOG(("Language is not loaded"));
		return;
	}
	LuxuryLanguage::init();
	LuxuryLanguage::currentInstance()->fetchLanguage(id, baseId);
}

void initUiSettings() {
	const auto &settings = LuxurySettings::getInstance();

	LuxuryUiSettings::setMonoFont(settings.monoFont());
	LuxuryUiSettings::setWideMultiplier(settings.wideMultiplier());
	LuxuryUiSettings::setMaterialSwitches(settings.materialSwitches());
	LuxuryUiSettings::setAvatarCorners(settings.avatarCorners());
	Ui::SetAppliedBubbleRadius(settings.messageBubbleRadius());
}

void initDatabase() {
	// Not on the main thread. A schema change is not a metadata edit: sqlite_orm
	// implements it as ALTER TABLE per column, and SQLite implements each of
	// those by rewriting every row. 1.0.2 took a 12 GB database through fourteen
	// of them from right here -- before style::StartManager(), before any window
	// existed -- so the process sat with no window, no error and no log line
	// after the last one. That reads as a hang, and it got killed as one.
	//
	// Nothing needs it to have finished. Every database entry point either posts
	// to this same queue, which is FIFO, or takes DatabaseMutex; initialize()
	// only has to be first in line, not done.
	//
	// filtersEnabled() is read here because the settings are written from the
	// main thread. The warm-up shares the lambda so it cannot be reordered ahead
	// of the schema it reads: snapshot() would otherwise build the cache on
	// whichever thread painted first, and the first painter is a message.
	const auto warmFilters = LuxurySettings::getInstance().filtersEnabled();
	LuxuryDatabase::async([=] {
		LuxuryDatabase::initialize();
		if (warmFilters) {
			FiltersCacheController::reloadNow();
		}
	});
	// ponytail: the three synchronous probes and addEditedMessage() still block
	// on DatabaseMutex if something reaches them while a migration is running --
	// a stall with a window up instead of a dead process, which is the trade we
	// want. Give them a "not ready yet" path if that ever shows up in practice.
}

void initWorker() {
	LuxuryWorker::initialize();
}

void initTranslator() {
	Luxury::Translator::TranslateManager::init();
}

void initIcon() {
#ifdef Q_OS_WIN
	// Rewriting the pinned taskbar shortcuts walks a directory and loads every
	// .lnk in it through COM, and this runs before the first window is shown.
	// Only pay for it when the icon on disk is not the one we want.
	if (LuxuryAssets::loadAppIco()) {
		reloadAppIconFromTaskBar();
	}
#endif
}

void init() {
	initLang();
	initDatabase();
	initUiSettings();
	initIcon();
	initWorker();
	initTranslator();
}

void finish() {
	LuxuryDatabase::shutdown();
}

}
