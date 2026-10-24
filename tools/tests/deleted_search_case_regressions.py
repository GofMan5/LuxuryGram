"""Deleted-message search matches regardless of letter case, Cyrillic included.

Compiles the real storage declaration, the search UDF registration from
initialize() and the real getDeletedMessages() with the bundled SQLite and
sqlite_orm on a disposable database. QString is a Win32 lowercase double; the
Qt Unicode fold itself and the native search box are not exercised.
Run: python tools/tests/deleted_search_case_regressions.py
"""
from pathlib import Path
import re
import tempfile

from library_regressions import CXX, ROOT, function, run, source

DATABASE = 'Telegram/SourceFiles/luxury/data/luxury_database.cpp'
CC = str(Path(CXX).with_name('gcc.exe'))


def fixture(text):
    storage = text[text.index('auto storage = make_storage('):text.index('namespace {')]
    anonymous = text[text.index('constexpr auto kBusyTimeoutMs'):text.index('} // namespace', text.index('constexpr auto kBusyTimeoutMs'))]
    initialize = function(text, 'void initialize()')
    registration = '\n'.join(re.findall(r'storage\.create_scalar_function<[^;]*;', initialize))
    search = function(text, 'std::vector<DeletedMessage> getDeletedMessages(')
    search = re.sub(r'LOG\(\(.*?\)\);', '', search, flags=re.S)
    return r'''#include <array>
#include <cassert>
#include <cstdio>
#include <mutex>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#include "Telegram/SourceFiles/luxury/data/entities.h"
#include "Telegram/SourceFiles/luxury/libs/sqlite/sqlite_orm.h"
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
class SchemaVersion { public: int id = 0; int version = 0; };
namespace base::functors {}
namespace func = base::functors;
namespace Qt { enum CaseSensitivity { CaseInsensitive, CaseSensitive }; }
struct QString {
	std::wstring value;
	static QString fromStdString(const std::string &text) {
		QString result;
		result.value.resize(MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0));
		MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), result.value.data(), int(result.value.size()));
		return result;
	}
	bool contains(const QString &other, Qt::CaseSensitivity cs) const {
		auto haystack = value;
		auto needle = other.value;
		if (cs == Qt::CaseInsensitive) {
			CharLowerBuffW(haystack.data(), DWORD(haystack.size()));
			CharLowerBuffW(needle.data(), DWORD(needle.size()));
		}
		return haystack.find(needle) != std::wstring::npos;
	}
};
using namespace sqlite_orm;
''' + storage + '\nnamespace {\nstd::recursive_mutex DatabaseMutex;\n' + anonymous + r'''} // namespace
''' + search + r'''
std::vector<ID> ids(const std::vector<DeletedMessage> &rows) {
	auto result = std::vector<ID>();
	for (const auto &row : rows) {
		result.push_back(row.messageId);
	}
	return result;
}
void add(ID dialogId, ID messageId, const char *text) {
	auto row = DeletedMessage();
	row.userId = 1;
	row.dialogId = dialogId;
	row.messageId = messageId;
	row.text = text;
	storage.insert(row);
}
int main() {
''' + registration + r'''
	storage.sync_schema(true);
	add(2, 10, "\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82, \xD0\xBC\xD0\xB8\xD1\x80");
	add(2, 11, "hello WORLD");
	add(2, 12, "50% off_now");
	add(3, 13, "\xD0\xBF\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82");
	const auto lower = "\xD0\xBF\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82";
	const auto upper = "\xD0\x9F\xD0\xA0\xD0\x98\xD0\x92\xD0\x95\xD0\xA2";
	assert(ids(getDeletedMessages(1, 2, 0, 0, 0, 50, lower)) == std::vector<ID>{ 10 });
	storage.open_forever();
	assert(ids(getDeletedMessages(1, 2, 0, 0, 0, 50, upper)) == std::vector<ID>{ 10 });
	assert(ids(getDeletedMessages(1, 2, 0, 0, 0, 50, "world")) == std::vector<ID>{ 11 });
	assert(ids(getDeletedMessages(1, 2, 0, 0, 0, 50, "%")) == std::vector<ID>{ 12 });
	assert(ids(getDeletedMessages(1, 2, 0, 0, 0, 50, "_")) == std::vector<ID>{ 12 });
	assert(ids(getDeletedMessages(1, 2, 0, 0, 0, 50, "O")) == (std::vector<ID>{ 12, 11 }));
	assert(ids(getDeletedMessages(1, 2, 0, 0, 12, 50, "O")) == std::vector<ID>{ 11 });
	assert(ids(getDeletedMessages(1, 2, 0, 0, 0, 1, "O")) == std::vector<ID>{ 12 });
	assert(ids(getDeletedMessages(1, 2, 0, 0, 0, 50, "")) == (std::vector<ID>{ 12, 11, 10 }));
	assert(getDeletedMessages(1, 2, 0, 0, 0, 50, "absent").empty());
	std::puts("deleted search PASS: Cyrillic/Latin any case, literal %/_, dialog/paging/limit kept; real storage+query with bundled SQLite, QString double");
}
'''


def check(text=None):
    text = text if text is not None else source(DATABASE)
    with tempfile.TemporaryDirectory(prefix='luxury-deleted-search-') as directory:
        tmp = Path(directory)
        (tmp / 'tdata').mkdir()
        path = tmp / 'search.cpp'
        path.write_text(fixture(text), encoding='utf-8')
        obj = tmp / 'sqlite.o'
        exe = tmp / 'search.exe'
        run([CC, '-O0', '-c', str(ROOT / 'Telegram/SourceFiles/luxury/libs/sqlite/sqlite3.c'), '-o', str(obj)], tmp)
        run([CXX, '-std=c++20', '-O0', '-g', '-D_DEBUG', '-I', str(ROOT), str(path), str(obj), '-o', str(exe)], tmp)
        print(run([str(exe)], tmp).strip())


if __name__ == '__main__':
    check()
