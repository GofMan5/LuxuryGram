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
	LuxuryDatabase::initialize();

	// snapshot() builds the cache on whatever thread asks for it first, and the
	// first asker is a message paint. Do it here instead, on the queue the
	// database work belongs on, so nothing but the very first frame can race it.
	if (LuxurySettings::getInstance().filtersEnabled()) {
		LuxuryDatabase::async([] {
			FiltersCacheController::reloadNow();
		});
	}
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

}
