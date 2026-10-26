// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "luxury/luxury_lang.h"

#include "qjsondocument.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "lang/lang_instance.h"
#include "lang/lang_keys.h"
#include "luxury/luxury_settings.h"
#include "storage/localstorage.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QUrl>

namespace {

constexpr auto kMaxLanguageBytes = 4 * 1024 * 1024;
constexpr auto kFetchTimeout = 15 * 1000;

// hard-coded languages
const std::map<QString, QString> langMapping = {
	{"pt-br", "pt"},
	{"zh-hans-beta", "zh-hans"},
	{"zh-hant-beta", "zh-hant"},
	{"zh-hans-raw", "zh-hans"},
	{"zh-hant-raw", "zh-hant"},
};

} // namespace

constexpr auto postfixes = {
	"zero",
	"one",
	"two",
	"few",
	"many",
	"other"
};

LuxuryLanguage *LuxuryLanguage::instance = nullptr;

LuxuryLanguage::LuxuryLanguage()
: QObject(qApp) {
	Lang::GetInstance().idChanges() | rpl::on_next([this] {
		reloadLanguage();
	}, _lifetime);
}

LuxuryLanguage::~LuxuryLanguage() {
	_lifetime.destroy();
	clearReply();
	instance = nullptr;
}

void LuxuryLanguage::init() {
	if (!instance) instance = new LuxuryLanguage;
	instance->reloadLanguage();
}

LuxuryLanguage *LuxuryLanguage::currentInstance() {
	return instance;
}

bool LuxuryLanguage::currentLanguageMatches() const {
	const auto &language = Lang::GetInstance();
	return language.id() == _languageId
		&& language.baseId() == _languageBaseId;
}

void LuxuryLanguage::reloadLanguage() {
	clearReply();
	needFallback = false;
	const auto &language = Lang::GetInstance();
	_languageId = language.id();
	_languageBaseId = language.baseId();
	if (_languageId.isEmpty() || language.isCustom()) {
		return;
	}
	const auto id = _languageId;
	const auto baseId = _languageBaseId;
	loadCachedLanguage();
	// lang.strings is the English source of every luxury_* key, so the
	// English pack has nothing to add.
	if (language.id() == id
		&& language.baseId() == baseId
		&& id != Lang::DefaultLanguageId()) {
		fetchLanguage(id, baseId);
	}
}

QString LuxuryLanguage::getCacheDir() const {
	return cWorkingDir() + u"tdata/luxury/languages/"_q;
}

QString LuxuryLanguage::getCachePath(const QString &langId) const {
	return getCacheDir()
		+ QString::fromLatin1(QUrl::toPercentEncoding(langId))
		+ u".json"_q;
}

void LuxuryLanguage::loadCachedLanguage() {
	const auto langPackId = Lang::GetInstance().id();
	const auto langPackBaseId = Lang::GetInstance().baseId();
	auto finalLangPackId = langMapping.contains(langPackId) ? langMapping.at(langPackId) : langPackId;

	if (finalLangPackId.isEmpty()) {
		finalLangPackId = langPackBaseId;
	}
	if (finalLangPackId.isEmpty()) {
		return;
	}

	const auto cachePath = getCachePath(finalLangPackId);
	QFile file(cachePath);
	if (!file.exists()) {
		const auto basePath = getCachePath(langPackBaseId);
		if (!QFile::exists(basePath)) {
			return;
		}
		file.setFileName(basePath);
	}

	if (file.open(QIODevice::ReadOnly)) {
		if (file.size() > kMaxLanguageBytes) {
			return;
		}
		const auto data = file.read(kMaxLanguageBytes + 1);
		file.close();
		if (data.size() > kMaxLanguageBytes) {
			return;
		}

		QJsonParseError error{};
		const auto doc = QJsonDocument::fromJson(data, &error);
		if (error.error == QJsonParseError::NoError) {
			LOG(("Loading cached LuxuryGram language: %1").arg(finalLangPackId));
			applyLanguageJson(doc);
		}
	}
}

void LuxuryLanguage::saveCachedLanguage(const QByteArray &json, const QString &langId) {
	const auto cacheDir = getCacheDir();
	if (!QDir().mkpath(cacheDir)) {
		LOG(("Failed to create LuxuryGram language cache directory."));
		return;
	}

	const auto cachePath = getCachePath(langId);
	QSaveFile file(cachePath);
	if (file.open(QIODevice::WriteOnly)
		&& file.write(json) == json.size()
		&& file.commit()) {
		LOG(("Cached LuxuryGram language: %1").arg(langId));
	} else {
		LOG(("Failed to cache LuxuryGram language: %1").arg(langId));
	}
}

void LuxuryLanguage::fetchLanguage(const QString &id, const QString &baseId) {
	if (!currentLanguageMatches()) {
		return;
	}
	if (!id.isEmpty() || !needFallback) {
		needFallback = false;
	}
	clearReply();

	auto finalLangPackId = langMapping.contains(id) ? langMapping.at(id) : id;
	_currentLangId = finalLangPackId.isEmpty() ? baseId : finalLangPackId;

	// using `jsdelivr` since China (...and maybe other?) users have some problems with GitHub
	// https://crowdin.com/project/ayugram/discussions/6
	const auto requestedId = (needFallback || finalLangPackId.isEmpty())
		? baseId
		: finalLangPackId;
	const auto encodedId = QString::fromLatin1(
		QUrl::toPercentEncoding(requestedId));
	const auto url = QUrl(qsl(
		"https://cdn.jsdelivr.net/gh/AyuGram/Languages@l10n_main/values/langs/%1/Shared.json"
	).arg(encodedId));
	auto request = QNetworkRequest(url);
	request.setTransferTimeout(kFetchTimeout);
	_chkReply = networkManager.get(request);
	const auto reply = _chkReply;
	const auto generation = _requestGeneration;
	const auto current = [this, reply, generation] {
		return _chkReply == reply
			&& _requestGeneration == generation
			&& currentLanguageMatches();
	};
	connect(
		reply,
		&QNetworkReply::errorOccurred,
		this,
		[=](QNetworkReply::NetworkError error) {
			if (current()) {
				fetchError(error);
			}
		});
	connect(
		reply,
		&QNetworkReply::finished,
		this,
		[=] {
			if (current()) {
				fetchFinished();
			}
		});
	connect(
		reply,
		&QNetworkReply::downloadProgress,
		this,
		[=](qint64 received, qint64) {
			if (current() && received > kMaxLanguageBytes) {
				reply->abort();
			}
		});
}

void LuxuryLanguage::fetchFinished() {
	if (!_chkReply || !currentLanguageMatches()) {
		return;
	}

	const auto langPackBaseId = _languageBaseId;
	const auto langPackId = _languageId;
	const auto statusCode = _chkReply->attribute(
		QNetworkRequest::HttpStatusCodeAttribute).toInt();

	if (statusCode == 404 && !langPackId.isEmpty() && !langPackBaseId.isEmpty() && !needFallback) {
		LOG(("LuxuryGram Language not found! Fallback to main language: %1...").arg(langPackBaseId));
		needFallback = true;
		clearReply();
		fetchLanguage("", langPackBaseId);
	} else {
		const auto result = _chkReply->read(kMaxLanguageBytes + 1).trimmed();
		clearReply();
		if (result.size() > kMaxLanguageBytes) {
			LOG(("Language JSON exceeds size limit."));
			return;
		}
		QJsonParseError error{};
		const auto doc = QJsonDocument::fromJson(result, &error);
		if (error.error == QJsonParseError::NoError) {
			saveCachedLanguage(result, _currentLangId);
			applyLanguageJson(doc);
		} else {
			LOG(("Incorrect language JSON File."));
		}
	}
}

void LuxuryLanguage::fetchError(QNetworkReply::NetworkError e) {
	if (!_chkReply || !currentLanguageMatches()) {
		return;
	}
	LOG(("Network error: %1").arg(e));

	if (e == QNetworkReply::NetworkError::ContentNotFoundError) {
		const auto baseId = _languageBaseId;
		const auto id = _languageId;

		if (!id.isEmpty() && !baseId.isEmpty() && !needFallback) {
			LOG(("LuxuryGram Language not found! Fallback to main language: %1...").arg(baseId));
			needFallback = true;
			clearReply();
			fetchLanguage("", baseId);
		} else {
			LOG(("LuxuryGram Language not found!"));
			clearReply();
		}
	} else {
		clearReply();
	}
}

void LuxuryLanguage::clearReply() {
	++_requestGeneration;
	const auto reply = base::take(_chkReply);
	if (!reply) {
		return;
	}
	disconnect(reply, nullptr, this, nullptr);
	reply->abort();
	reply->deleteLater();
}

void LuxuryLanguage::applyLanguageJson(QJsonDocument doc) {
	if (!doc.isObject()) {
		return;
	}
	const auto json = doc.object();
	auto &language = Lang::GetInstance();
	// An English pack cached by an older build only clears the values it
	// left in the stored language snapshot.
	const auto english = (language.id() == Lang::DefaultLanguageId());
	auto changed = false;
	for (const QString &brokenKey : json.keys()) {
		if (!json.value(brokenKey).isString()) {
			continue;
		}
		auto key = qsl("luxury_") + brokenKey;
		auto val = json.value(brokenKey).toString().replace(qsl("&amp;"), qsl("&"));

		if (key.endsWith("_Android")) {
			continue;
		}

		for (const auto &postfix : postfixes) {
			if (key.endsWith(qsl("_") + postfix)) {
				key = key.replace(qsl("_") + postfix, qsl("#") + postfix);
				break;
			}
		}

		if (key.endsWith("_PC")) {
			key = key.replace("_PC", "");
		}

		if (val.contains(qsl("%1$d")) && !val.contains(qsl("%2$d"))) {
			val = val.replace(qsl("%1$d"), qsl("{count}"));
		} else if (val.contains(qsl("%1$d")) && val.contains(qsl("%2$d"))) {
			val = val.replace(qsl("%1$d"), qsl("{count1}")).replace(qsl("%2$d"), qsl("{count2}"));
		} else if (val.contains(qsl("%1$s")) && !val.contains(qsl("%2$s"))) {
			val = val.replace(qsl("%1$s"), qsl("{item}"));
		} else if (val.contains(qsl("%1$s")) && val.contains(qsl("%2$s"))) {
			val = val.replace(qsl("%1$s"), qsl("{item1}")).replace(qsl("%2$s"), qsl("{item2}"));
		}

		const auto keyBytes = key.toUtf8();
		language.resetValue(keyBytes);
		if (!english) {
			val.replace(u"AyuGram"_q, u"LuxuryGram"_q);
			language.applyValue(keyBytes, val.toUtf8());
		}
		changed = true;
	}
	if (changed) {
		// The pack is AyuGram's community translation; strings LuxuryGram
		// translated itself must not lose to it.
		LuxuryRussian::applyIfRussian(&language, language.id());
		language.updatePluralRules();
		language.notifyUpdated();
	}
}
