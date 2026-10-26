"""Message Details dates use the language's date-and-time template.

`formatDateTime` glued the date, a connector and the time together. The
connector was `lng_mediaview_date_time` with both arguments empty, kept in a
`static` from the first call. Two results: the connector stayed in the
language of the first Message Details opened in the run, and a template that
does not read "{date} <word> {time}" came out broken -- Uzbek
"{date} {time} da" gave "25.10.26  da15:30:44", Vietnamese and Kazakh
"{time} {date}" kept the date first.

Runs the real `formatDateTime` (and `getLocalizedAt` while it exists) over a
QString/tr shim with a switchable template; not a native acceptance.
"""
from pathlib import Path
import tempfile
from library_regressions import cpp, function, source

HELPERS = 'Telegram/SourceFiles/luxury/utils/telegram_helpers.cpp'

PROGRAM = r'''
#include <cstdio>
#include <string>

struct QString {
	std::string text;
	QString() = default;
	QString(const char *s) : text(s) {}
	QString(std::string s) : text(std::move(s)) {}
	bool operator==(const QString &o) const { return text == o.text; }
};
QString operator+(const QString &a, const QString &b) { return a.text + b.text; }

struct QDate {};
struct QTime {};
struct QDateTime {
	QDate date() const { return {}; }
	QTime time() const { return {}; }
};
struct QLocale {
	enum FormatType { ShortFormat };
	QString toString(QDate, FormatType) const { return "25.10.26"; }
};
const QLocale &systemLocale() {
	static const auto locale = QLocale();
	return locale;
}
QString formatTimeWithSeconds(const QTime &) { return "15:30:44"; }

struct lt_date_t {};
inline constexpr lt_date_t lt_date;
struct lt_time_t {};
inline constexpr lt_time_t lt_time;

namespace tr {
struct now_t {};
inline constexpr now_t now;
std::string language = "{date} at {time}";
struct date_time_phrase {
	QString operator()(now_t, lt_date_t, const QString &date, lt_time_t, const QString &time) const {
		auto result = language;
		result.replace(result.find("{date}"), 6, date.text);
		result.replace(result.find("{time}"), 6, time.text);
		return result;
	}
};
inline constexpr date_time_phrase lng_mediaview_date_time;
} // namespace tr

HELPER
FORMAT

int failures = 0;
void Expect(const char *language, const char *expected) {
	tr::language = language;
	const auto got = formatDateTime(QDateTime());
	if (!(got == QString(expected))) {
		std::printf("FAIL %s: '%s', expected '%s'\n", language, got.text.c_str(), expected);
		++failures;
	}
}
int main() {
	Expect("{date} at {time}", "25.10.26 at 15:30:44");
	Expect("{date} \xD0\xB2 {time}", "25.10.26 \xD0\xB2 15:30:44");
	Expect("{date} {time} da", "25.10.26 15:30:44 da");
	Expect("{time} {date}", "15:30:44 25.10.26");
	Expect("{date} at {time}", "25.10.26 at 15:30:44");
	if (failures) {
		return 1;
	}
	std::printf("Details date template PASS: Message Details dates follow the current language's template and word order\n");
}
'''


def check(root):
    text = source(HELPERS)
    helper = function(text, 'QString getLocalizedAt(') if 'QString getLocalizedAt(' in text else ''
    cpp(root, 'details_date_template', PROGRAM
        .replace('HELPER', helper)
        .replace('FORMAT', function(text, 'QString formatDateTime(')))


if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='details-date-template-') as directory:
        check(Path(directory))
