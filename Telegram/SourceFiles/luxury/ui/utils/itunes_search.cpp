// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "luxury/ui/utils/itunes_search.h"

#include <QtCore/QBuffer>
#include <QtCore/QCache>
#include <QtCore/QEventLoop>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QRegularExpression>
#include <QtCore/QTimer>
#include <QtCore/QUrlQuery>
#include <QtGui/QImage>
#include <QtGui/QImageReader>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

#include <algorithm>
#include <atomic>
#include <mutex>

namespace Luxury::Ui::Itunes {
namespace {

constexpr auto kMaxResponseBytes = 8 * 1024 * 1024;
constexpr auto kMaxArtworkPixels = 4 * 1024 * 1024;
constexpr auto kArtworkCacheKiB = 16 * 1024;
constexpr auto kParallelFetchLimit = 3;

struct Cache {
	QCache<QString, QImage> entries{ kArtworkCacheKiB };
	std::mutex mutex;
};

Cache &cache() {
	static auto result = Cache();
	return result;
}

// A track without artwork is cached as a null image, so a recycled list row
// does not repeat the lookup on every scroll pass.
QImage rememberMissing(const QString &key) {
	auto &state = cache();
	const auto lock = std::lock_guard(state.mutex);
	state.entries.insert(key, new QImage(), 1);
	return {};
}

// Every lookup blocks a shared crl::async thread for up to two timeouts, so
// only a few may run while a long list scrolls past.
class FetchSlot final {
public:
	FetchSlot() {
		auto count = counter().load(std::memory_order_relaxed);
		while (count < kParallelFetchLimit) {
			if (counter().compare_exchange_weak(count, count + 1)) {
				_taken = true;
				break;
			}
		}
	}

	~FetchSlot() {
		if (_taken) {
			counter().fetch_sub(1);
		}
	}

	[[nodiscard]] bool taken() const {
		return _taken;
	}

private:
	[[nodiscard]] static std::atomic<int> &counter() {
		static auto result = std::atomic<int>(0);
		return result;
	}

	bool _taken = false;

};

QString translitSafe(const QString &s) {
	// ponytail: NFKD plus common exceptions; extend only for a real artist match.
	static const QHash<QChar, QString> trMap = {
		{ QChar(u'\u00F8'), QStringLiteral("o") },
		{ QChar(u'\u00E6'), QStringLiteral("ae") },
		{ QChar(u'\u0153'), QStringLiteral("oe") },
		{ QChar(u'\u00DF'), QStringLiteral("ss") },
		{ QChar(u'\u0142'), QStringLiteral("l") },
		{ QChar(u'\u0111'), QStringLiteral("d") },
		{ QChar(u'\u00F0'), QStringLiteral("d") },
		{ QChar(u'\u00FE'), QStringLiteral("th") },
		{ QChar(u'\u0127'), QStringLiteral("h") },
		{ QChar(u'\u0131'), QStringLiteral("i") },
		{ QChar(u'\u0167'), QStringLiteral("t") },
		{ QChar(u'\u0192'), QStringLiteral("f") },
		{ QChar(u'\u014B'), QStringLiteral("n") },
		{ QChar(u'\u017F'), QStringLiteral("s") },
	};

	static const QHash<QChar, QString> ruMap = []
	{
		QHash<QChar, QString> m;
		m.reserve(33);
		m.insert(QChar(u'а'), QStringLiteral("a"));
		m.insert(QChar(u'б'), QStringLiteral("b"));
		m.insert(QChar(u'в'), QStringLiteral("v"));
		m.insert(QChar(u'г'), QStringLiteral("g"));
		m.insert(QChar(u'д'), QStringLiteral("d"));
		m.insert(QChar(u'е'), QStringLiteral("e"));
		m.insert(QChar(u'ё'), QStringLiteral("yo"));
		m.insert(QChar(u'ж'), QStringLiteral("zh"));
		m.insert(QChar(u'з'), QStringLiteral("z"));
		m.insert(QChar(u'и'), QStringLiteral("i"));
		m.insert(QChar(u'й'), QStringLiteral("i"));
		m.insert(QChar(u'к'), QStringLiteral("k"));
		m.insert(QChar(u'л'), QStringLiteral("l"));
		m.insert(QChar(u'м'), QStringLiteral("m"));
		m.insert(QChar(u'н'), QStringLiteral("n"));
		m.insert(QChar(u'о'), QStringLiteral("o"));
		m.insert(QChar(u'п'), QStringLiteral("p"));
		m.insert(QChar(u'р'), QStringLiteral("r"));
		m.insert(QChar(u'с'), QStringLiteral("s"));
		m.insert(QChar(u'т'), QStringLiteral("t"));
		m.insert(QChar(u'у'), QStringLiteral("u"));
		m.insert(QChar(u'ф'), QStringLiteral("f"));
		m.insert(QChar(u'х'), QStringLiteral("h"));
		m.insert(QChar(u'ц'), QStringLiteral("ts"));
		m.insert(QChar(u'ч'), QStringLiteral("ch"));
		m.insert(QChar(u'ш'), QStringLiteral("sh"));
		m.insert(QChar(u'щ'), QStringLiteral("sch"));
		m.insert(QChar(u'ы'), QStringLiteral("i"));
		m.insert(QChar(u'ь'), QStringLiteral(""));
		m.insert(QChar(u'ъ'), QStringLiteral(""));
		m.insert(QChar(u'э'), QStringLiteral("e"));
		m.insert(QChar(u'ю'), QStringLiteral("yu"));
		m.insert(QChar(u'я'), QStringLiteral("ya"));
		return m;
	}();

	if (s.isEmpty()) {
		return s;
	}

	QString out;
	out.reserve(s.size() * 2);

	for (const auto ch : s) {
		if (const auto tr = ruMap.constFind(ch); tr != ruMap.cend()) {
			out += tr.value();
			continue;
		}
		const auto decomposed = QString(ch).normalized(
			QString::NormalizationForm_KD);
		for (const auto part : decomposed) {
			const auto category = part.category();
			if (category == QChar::Mark_NonSpacing
				|| category == QChar::Mark_SpacingCombining
				|| category == QChar::Mark_Enclosing) {
				continue;
			}
			const auto latin = trMap.constFind(part);
			if (latin != trMap.cend()) {
				out += latin.value();
			} else {
				out += part;
			}
		}
	}

	return out;
}

QString normalized(const QString &s) {
	return translitSafe(s.trimmed().toCaseFolded());
}

QStringList splitArtists(const QString &artists) {
	static const QRegularExpression splitter(QString::fromUtf8(R"((?i)\s*(?:,|&|feat\.?|ft\.?)\s*)"));
	auto list = artists.split(splitter, Qt::SkipEmptyParts);
	for (auto &entry : list) {
		entry = entry.trimmed();
	}
	return list;
}

bool hasCommonArtist(const QStringList &baseArtists, const QString &itunesArtists) {
	const auto itunesList = splitArtists(itunesArtists);
	for (const auto &base : baseArtists) {
		const auto b = normalized(base);
		for (const auto &it : itunesList) {
			const auto i = normalized(it);
			if (i == b) return true;
		}
	}
	return false;
}

std::unique_ptr<QNetworkReply, void(*)(QNetworkReply *)> execWithTimeout(
	QNetworkAccessManager &nam,
	const QNetworkRequest &req,
	int timeoutMs) {
	QNetworkReply *reply = nam.get(req);
	QEventLoop loop;
	QTimer timer;
	timer.setSingleShot(true);
	QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
	QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
	QObject::connect(
		reply,
		&QNetworkReply::downloadProgress,
		[=](qint64 received, qint64) {
			if (received > kMaxResponseBytes) {
				reply->abort();
			}
		});
	timer.start(timeoutMs);
	loop.exec();
	if (timer.isActive()) {
		timer.stop();
	} else {
		reply->abort();
	}
	return {reply, [](QNetworkReply *r) { if (r) r->deleteLater(); }};
}

QByteArray getBytesWithTimeout(const QUrl &url, int timeoutMs) {
	QNetworkAccessManager nam;
	QNetworkRequest req(url);
	req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
	auto replyPtr = execWithTimeout(nam, req, timeoutMs);
	QNetworkReply *reply = replyPtr.get();
	if (!reply) return {};
	if (reply->error() != QNetworkReply::NoError) return {};
	const auto result = reply->read(kMaxResponseBytes + 1);
	return (result.size() <= kMaxResponseBytes) ? result : QByteArray();
}

struct ItunesTrack
{
	QString trackName;
	QString artistName;
	QString artworkUrl100;
};

QList<ItunesTrack> parseTracks(const QByteArray &json) {
	QList<ItunesTrack> out;
	auto doc = QJsonDocument::fromJson(json);
	if (!doc.isObject()) return out;
	auto root = doc.object();
	auto results = root.value(QString::fromUtf8("results")).toArray();
	out.reserve(results.size());
	for (const auto &v : results) {
		const auto o = v.toObject();
		ItunesTrack t;
		t.trackName = o.value(QString::fromUtf8("trackName")).toString();
		t.artistName = o.value(QString::fromUtf8("artistName")).toString();
		t.artworkUrl100 = o.value(QString::fromUtf8("artworkUrl100")).toString();
		out.push_back(std::move(t));
	}
	return out;
}

QString pickArtworkUrl(const QList<ItunesTrack> &tracks,
					   const QString &targetTitle,
					   const QStringList &baseArtists) {
	if (tracks.isEmpty()) return {};
	for (const auto &t : tracks) {
		if (!t.trackName.compare(targetTitle, Qt::CaseInsensitive) && hasCommonArtist(baseArtists, t.artistName)) {
			return t.artworkUrl100;
		}
	}
	return tracks.first().artworkUrl100;
}

QUrl buildItunesUrl(const QString &performer, const QString &title) {
	QUrl url(QString::fromUtf8("https://itunes.apple.com/search"));
	QUrlQuery query;
	query.addQueryItem(QString::fromUtf8("term"), title + QString::fromUtf8(" - ") + performer);
	query.addQueryItem(QString::fromUtf8("entity"), QString::fromUtf8("song"));
	query.addQueryItem(QString::fromUtf8("limit"), QString::fromUtf8("5"));
	url.setQuery(query);
	return url;
}

int ArtworkSize(int sizeHint) {
	return (sizeHint >= 600) ? 600 : (sizeHint >= 300) ? 300 : 100;
}

QString upgradeArtworkSize(QString url, int size) {
	if (url.isEmpty()) return url;
	url.replace(
		QString::fromUtf8("100x100"),
		u"%1x%1"_q.arg(size));
	return url;
}

} // namespace

QImage FetchCover(const QString &performer, const QString &title, int sizeHintPx, int timeoutMs) {
	const auto perf = performer.trimmed();
	const auto titl = title.trimmed();
	if (perf.isEmpty() && titl.isEmpty()) return {};

	const auto artworkSize = ArtworkSize(sizeHintPx);
	const auto key = perf.toCaseFolded()
		+ u'\n'
		+ titl.toCaseFolded()
		+ u'\n'
		+ QString::number(artworkSize);
	{
		auto &state = cache();
		const auto lock = std::lock_guard(state.mutex);
		if (const auto entry = state.entries.object(key)) {
			return *entry;
		}
	}

	const auto slot = FetchSlot();
	if (!slot.taken()) {
		// Not remembered as missing: a later refresh of the row retries.
		return {};
	}

	const auto url = buildItunesUrl(perf, titl);
	const auto json = getBytesWithTimeout(url, timeoutMs);
	if (json.isEmpty()) return rememberMissing(key);
	const auto tracks = parseTracks(json);
	if (tracks.isEmpty()) return rememberMissing(key);

	const auto baseArtists = splitArtists(perf);
	auto artwork = pickArtworkUrl(tracks, titl, baseArtists);
	artwork = upgradeArtworkSize(std::move(artwork), artworkSize);
	if (artwork.isEmpty()) return rememberMissing(key);

	const auto imgBytes = getBytesWithTimeout(QUrl(artwork), timeoutMs);
	if (imgBytes.isEmpty()) return rememberMissing(key);

	auto buffer = QBuffer();
	buffer.setData(imgBytes);
	if (!buffer.open(QIODevice::ReadOnly)) return rememberMissing(key);
	auto reader = QImageReader(&buffer);
	const auto size = reader.size();
	if (!size.isValid()
		|| (qint64(size.width()) * size.height()) > kMaxArtworkPixels) {
		return rememberMissing(key);
	}
	reader.setAutoTransform(true);
	const auto image = reader.read();
	if (image.isNull()) return rememberMissing(key);

	{
		auto &state = cache();
		const auto lock = std::lock_guard(state.mutex);
		const auto cost = std::max<qsizetype>(
			1,
			(image.sizeInBytes() + 1023) / 1024);
		state.entries.insert(key, new QImage(image), int(cost));
	}
	return image;
}

}
