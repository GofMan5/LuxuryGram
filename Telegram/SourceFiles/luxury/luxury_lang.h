// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

#include "rpl/lifetime.h"

#include <QtNetwork/QNetworkReply>
#include <QtXml/QDomDocument>

class LuxuryLanguage : public QObject
{
	Q_OBJECT
	Q_DISABLE_COPY(LuxuryLanguage)

public:
	static LuxuryLanguage *currentInstance();
	static void init();
	static LuxuryLanguage *instance;

	void applyLanguageJson(QJsonDocument doc);

public Q_SLOTS:
	void fetchFinished();
	void fetchError(QNetworkReply::NetworkError e);

private:
	LuxuryLanguage();
	~LuxuryLanguage() override;

	void clearReply();
	void reloadLanguage();
	void fetchLanguage(const QString &id, const QString &baseId);
	[[nodiscard]] bool currentLanguageMatches() const;
	void loadCachedLanguage();
	void saveCachedLanguage(const QByteArray &json, const QString &langId);
	[[nodiscard]] QString getCacheDir() const;
	[[nodiscard]] QString getCachePath(const QString &langId) const;

	QNetworkAccessManager networkManager;
	QNetworkReply *_chkReply = nullptr;
	bool needFallback = false;
	QString _currentLangId;
	QString _languageId;
	QString _languageBaseId;
	quint64 _requestGeneration = 0;
	rpl::lifetime _lifetime;

};
