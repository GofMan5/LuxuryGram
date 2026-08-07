// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/features/translator/implementations/yandex.h"

#include "ayu/features/translator/html_parser.h"

#include <memory>
#include <QtCore/QPointer>
#include <QtCore/QTimer>
#include <QtCore/QUrl>
#include <QtCore/QUrlQuery>
#include <QtCore/QUuid>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

namespace Luxury::Translator {

namespace {

constexpr auto kMaxTranslationResponseBytes = 4 * 1024 * 1024;

} // namespace

YandexTranslator &YandexTranslator::instance() {
	static YandexTranslator inst;
	return inst;
}

YandexTranslator::YandexTranslator(QObject *parent)
	: MultiThreadTranslator(parent)
	  , _uuid(QUuid::createUuid().toString(QUuid::WithoutBraces).replace("-", "")) {
}

QPointer<QNetworkReply> YandexTranslator::startSingleTranslation(
	const MultiThreadArgs &args
) {
	const auto &text = args.parsedData.text;
	// const auto &fromLang = args.parsedData.fromLang;
	const auto &toLang = args.parsedData.toLang;
	const auto onSuccess = args.onSuccess;
	const auto onFail = args.onFail;

	if (text.empty() || toLang.isEmpty()) {
		return nullptr;
	}

	const auto to = toLang.trimmed();

	QUrl url(QStringLiteral("https://translate.yandex.net/api/v1/tr.json/translate"));
	QUrlQuery query;
	query.addQueryItem(QStringLiteral("srv"), QStringLiteral("android"));
	query.addQueryItem(QStringLiteral("id"), _uuid + QStringLiteral("-0-0"));
	url.setQuery(query);

	QNetworkRequest req(url);
	req.setHeader(QNetworkRequest::UserAgentHeader,
				  QStringLiteral("ru.yandex.translate/21.15.4.21402814 (Xiaomi Redmi K20 Pro; Android 11)"));
	req.setHeader(QNetworkRequest::ContentTypeHeader,
				  QStringLiteral("application/x-www-form-urlencoded"));

	QUrlQuery postData;
	postData.addQueryItem(QStringLiteral("lang"), to);
	postData.addQueryItem(QStringLiteral("text"), shouldWrapInHtml() ? Html::entitiesToHtml(text) : text.text);
	const auto postDataEncoded = postData.toString(QUrl::FullyEncoded).toUtf8();

	QPointer<QNetworkReply> reply = _nam.post(req, postDataEncoded);
	QObject::connect(
		reply,
		&QNetworkReply::downloadProgress,
		reply,
		[reply](qint64 received, qint64) {
			if (reply && received > kMaxTranslationResponseBytes) {
				reply->abort();
			}
		});

	auto timer = new QTimer(reply);
	timer->setSingleShot(true);
	timer->setInterval(15000);
	QObject::connect(timer,
					 &QTimer::timeout,
					 reply,
					 [reply]
					 {
						 if (!reply) return;
						 if (reply->isRunning()) reply->abort();
					 });
	timer->start();

	QObject::connect(reply,
					 &QNetworkReply::finished,
					 [reply, onSuccess = onSuccess, onFail = onFail, timer]
					 {
						 if (!reply) return;
						 timer->stop();
						 const auto guard = std::unique_ptr<QNetworkReply, void(*)(QNetworkReply *)>(
							 reply,
							 [](QNetworkReply *r) { r->deleteLater(); });

						 if (reply->error() != QNetworkReply::NoError) {
							 if (onFail) onFail();
							 return;
						 }

							 const auto body = reply->read(
								 kMaxTranslationResponseBytes + 1);
							 if (body.size() > kMaxTranslationResponseBytes) {
								 if (onFail) onFail();
								 return;
							 }
						 bool ok = false;
						 const auto translatedText = parseJsonPath(body, QStringLiteral("text"), &ok);
						 if (!ok) {
							 if (onFail) onFail();
							 return;
						 }
						 if (onSuccess) onSuccess(shouldWrapInHtml()
													  ? Html::htmlToEntities(translatedText)
													  : TextWithEntities{translatedText});
					 });

	return reply;
}

}
