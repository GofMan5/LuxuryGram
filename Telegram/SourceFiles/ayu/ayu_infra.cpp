// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/ayu_infra.h"

#include "ayu/ayu_lang.h"
#include "ayu/ayu_settings.h"
#include "ayu/ayu_ui_settings.h"
#include "ayu/ayu_worker.h"
#include "ayu/data/ayu_database.h"
#include "ayu/ui/ayu_logo.h"
#include "features/translator/ayu_translator.h"
#include "lang/lang_instance.h"
#include "ui/chat/chat_style_radius.h"
#include "utils/rc_manager.h"

#ifdef Q_OS_WIN
#include "ayu/utils/windows_utils.h"
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
}

void initWorker() {
	LuxuryWorker::initialize();
}

void initRCManager() {
	RCManager::getInstance().start();
}

void initTranslator() {
	Luxury::Translator::TranslateManager::init();
}

void initIcon() {
#ifdef Q_OS_WIN
	LuxuryAssets::loadAppIco();
	reloadAppIconFromTaskBar();
#endif
}

void init() {
	initLang();
	initDatabase();
	initUiSettings();
	initIcon();
	initWorker();
	initRCManager();
	initTranslator();
}

}
