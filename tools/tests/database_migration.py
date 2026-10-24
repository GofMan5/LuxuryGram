"""Compile actual migration/schema guard with bundled SQLite/ORM on disposable DBs.
Run: python tools/tests/database_migration.py [--cc gcc --cxx g++]
Not a Qt client/profile, native initialization, WAL/crash or UI test.
"""
from pathlib import Path
import argparse
import os
import re
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = root / 'Telegram/SourceFiles/luxury/data/luxury_database.cpp'
text = source.read_text('utf-8-sig')
table = text[text.index('make_table<DeletedMessage>('):text.index('make_table<RegexFilter>(')].rstrip().removesuffix(',')
functions = text[text.index('std::set<std::string> tableColumns('):text.index('void initialize()')]
functions = re.sub(r'LOG\(\(.*?\)\);', '', functions, flags=re.S)
# Guard both initialize paths; the historical audit receipt remains unchanged.
initialize = text[text.index('void initialize()'):text.index('void addEditedMessage(')]
assert initialize.count('syncSchemaWithoutLoss(storage);') == 2
assert 'storage.sync_schema(true);' not in initialize
assert initialize.index('if (needRecovery && ConvergeFailed)') < initialize.index('moveCurrentDatabase()')

cpp = r'''
#include <array>
#include <iostream>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#include "Telegram/SourceFiles/luxury/data/entities.h"
#include "Telegram/SourceFiles/luxury/libs/sqlite/sqlite_orm.h"
using namespace sqlite_orm;
bool ConvergeFailed = false;
FUNCTIONS
void require(bool value, const char *message) {
 if (!value) throw std::runtime_error(message);
}
void exec(sqlite3 *db, const std::string &query) {
 sqlite_orm::internal::perform_void_exec(db, query);
}
std::string legacy() {
 return R"SQL(CREATE TABLE DeletedMessage (
 fakeId INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL,
 userId INTEGER NOT NULL,dialogId INTEGER NOT NULL,groupedId INTEGER NOT NULL,
 peerId INTEGER NOT NULL,fromId INTEGER NOT NULL,topicId INTEGER NOT NULL,
 messageId INTEGER NOT NULL,date INTEGER NOT NULL,flags INTEGER NOT NULL,
 editDate INTEGER NOT NULL,views INTEGER NOT NULL,postAuthor TEXT NOT NULL DEFAULT '',
 entityCreateDate INTEGER NOT NULL,text TEXT NOT NULL,textEntities BLOB NOT NULL))SQL";
}
void insert(sqlite3 *db, bool full) {
 exec(db, "INSERT INTO DeletedMessage(fakeId,userId,dialogId,groupedId,peerId,fromId,topicId,messageId,date,flags,editDate,views,postAuthor,entityCreateDate,text,textEntities"
  + std::string(full ? ",fwdPostAuthor,replyFlags,replyMessageId,replyPeerId,replyTopId,replyForumTopic,replySerialized,mediaPath,hqThumbPath,documentType,documentSerialized,thumbsSerialized,documentAttributesSerialized,mimeType" : "")
  + ") VALUES(7,11,33,0,33,33,0,99,1,0,0,0,'author-A',1,'saved-A',x'0011'"
  + std::string(full ? ",'forward-A',9,22,44,55,1,x'12','media-A','thumb-A',2,x'34',x'56',x'78','type-A'" : "") + ")");
}
std::set<std::string> schemaNames(sqlite3 *db) {
 auto statement = statement_finalizer{sqlite_orm::internal::prepare_stmt(db, "PRAGMA table_xinfo(DeletedMessage)")};
 auto result = std::set<std::string>();
 while (sqlite3_step(statement.get()) == SQLITE_ROW) {
  result.emplace(reinterpret_cast<const char*>(sqlite3_column_text(statement.get(), 1)));
 }
 return result;
}
int denyDrop(void*, int action, const char*, const char*, const char*, const char*) {
 return action == SQLITE_DROP_TABLE ? SQLITE_DENY : SQLITE_OK;
}
std::set<std::string> namesOf(sqlite3 *db, const std::string &table) {
 auto statement = statement_finalizer{sqlite_orm::internal::prepare_stmt(db, "PRAGMA table_xinfo(" + table + ")")};
 auto result = std::set<std::string>();
 while (sqlite3_step(statement.get()) == SQLITE_ROW) {
  result.emplace(reinterpret_cast<const char*>(sqlite3_column_text(statement.get(), 1)));
 }
 return result;
}
// The original AyuGram shape, still on disk for an EditedMessage table no
// release managed to convert: 33 columns, no postAuthor.
std::string ayugram(const std::string &table) {
 return "CREATE TABLE " + table + R"SQL( (
 fakeId INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL,
 userId INTEGER NOT NULL,dialogId INTEGER NOT NULL,groupedId INTEGER NOT NULL,
 peerId INTEGER NOT NULL,fromId INTEGER NOT NULL,topicId INTEGER NOT NULL,
 messageId INTEGER NOT NULL,date INTEGER NOT NULL,flags INTEGER NOT NULL,
 editDate INTEGER NOT NULL,views INTEGER NOT NULL,fwdFlags INTEGER NOT NULL,
 fwdFromId INTEGER NOT NULL,fwdName TEXT NOT NULL,fwdDate INTEGER NOT NULL,
 fwdPostAuthor TEXT NOT NULL,replyFlags INTEGER NOT NULL,replyMessageId INTEGER NOT NULL,
 replyPeerId INTEGER NOT NULL,replyTopId INTEGER NOT NULL,replyForumTopic INTEGER NOT NULL,
 replySerialized BLOB NOT NULL,entityCreateDate INTEGER NOT NULL,text TEXT NOT NULL,
 textEntities BLOB NOT NULL,mediaPath TEXT NOT NULL,hqThumbPath TEXT NOT NULL,
 documentType INTEGER NOT NULL,documentSerialized BLOB NOT NULL,thumbsSerialized BLOB NOT NULL,
 documentAttributesSerialized BLOB NOT NULL,mimeType TEXT NOT NULL))SQL";
}
void insertAyugram(sqlite3 *db, const std::string &table) {
 exec(db, "INSERT INTO " + table + " VALUES(7,11,33,0,33,33,0,99,1,0,0,0,"
  "1,44,'fwd-name',5,'fwd-author',9,22,44,55,1,x'12',1,'saved-A',x'0011',"
  "'media-A','thumb-A',2,x'34',x'56',x'78','type-A')");
}
void ayugramShapes(const std::string &directory) {
 for (int scenario = 0; scenario != 4; ++scenario) {
  ConvergeFailed = false;
  const auto path = directory + "/ayugram" + std::to_string(scenario) + ".db";
  auto database = make_storage(path, TABLE_HERE);
  database.open_forever();
  sqlite3 *db = nullptr;
  require(sqlite3_open(path.c_str(), &db) == SQLITE_OK, "Open AyuGram fixture");
  // 0: both tables untouched since AyuGram; 1: EditedMessage only, next to a
  // current DeletedMessage (the shape seen in a real profile); 2: postAuthor
  // already added in place; 3: a column nobody knows, which must stay put.
  if (scenario == 0) {
   exec(db, ayugram("DeletedMessage"));
   insertAyugram(db, "DeletedMessage");
  } else {
   database.sync_schema(true);
   exec(db, "DROP TABLE EditedMessage");
  }
  exec(db, ayugram("EditedMessage"));
  insertAyugram(db, "EditedMessage");
  if (scenario == 2) {
   exec(db, "ALTER TABLE EditedMessage ADD COLUMN postAuthor TEXT NOT NULL DEFAULT ''");
   exec(db, "UPDATE EditedMessage SET postAuthor='author-E'");
  } else if (scenario == 3) {
   exec(db, "ALTER TABLE EditedMessage ADD COLUMN futureField TEXT NOT NULL DEFAULT 'keep'");
  }
  const auto before = namesOf(db, "EditedMessage");
  const auto blocked = (scenario == 3);
  auto threw = false;
  try {
   convergeMessageTables(db);
   syncSchemaWithoutLoss(database);
  } catch (const std::exception &) { threw = true; }
  require(threw == blocked && ConvergeFailed == blocked, "AyuGram shape: wrong fail-closed result");
  if (blocked) {
   require(namesOf(db, "EditedMessage") == before, "AyuGram shape: unknown column touched");
   exec(db, "SELECT futureField FROM EditedMessage WHERE futureField='keep' AND text='saved-A'");
   sqlite3_close(db);
   std::cout << "ayugram " << scenario << " safely blocked\n";
   continue;
  }
  require(namesOf(db, "EditedMessage").size() == 16, "AyuGram EditedMessage not converged");
  require(!namesOf(db, "EditedMessage").contains("fwdFlags"), "AyuGram EditedMessage kept legacy");
  const auto edited = database.get<EditedMessage>(7);
  require(edited.text == "saved-A" && edited.userId == 11 && edited.messageId == 99
   && edited.textEntities == std::vector<char>({0, 17}), "AyuGram edit history lost");
  require(edited.postAuthor == (scenario == 2 ? "author-E" : ""), "AyuGram postAuthor wrong");
  auto added = EditedMessage();
  added.text = "edit-after-upgrade";
  require(database.insert(added) == 8, "AyuGram EditedMessage refuses new edits");
  if (scenario == 0) {
   require(namesOf(db, "DeletedMessage").size() == 30, "AyuGram DeletedMessage not converged");
   const auto deleted = database.get<DeletedMessage>(7);
   require(deleted.text == "saved-A" && deleted.postAuthor.empty()
    && deleted.fwdPostAuthor == "fwd-author" && deleted.mediaPath == "media-A"
    && deleted.replyFlags == 9 && deleted.documentSerialized == std::vector<char>({52}),
    "AyuGram deleted message lost");
  }
  convergeMessageTables(db);
  syncSchemaWithoutLoss(database);
  require(database.count<EditedMessage>() == 2, "AyuGram repeat lost rows");
  sqlite3_close(db);
  std::cout << "ayugram " << scenario << " preserved\n";
 }
}
int main(int argc, char **argv) {
 require(argc == 2, "Need disposable directory");
 for (int scenario = 0; scenario != 18; ++scenario) {
  ConvergeFailed = false;
  const auto path = std::string(argv[1]) + "/case" + std::to_string(scenario) + ".db";
  auto database = make_storage(path, TABLE_HERE);
  database.open_forever();
  sqlite3 *db = nullptr;
  require(sqlite3_open(path.c_str(), &db) == SQLITE_OK, "Open fixture");
  const auto full = scenario == 2 || scenario == 8 || scenario == 9 || scenario == 17;
  const auto fresh = scenario == 3;
  if (full) database.sync_schema(true);
  else if (!fresh) {
    auto schema = legacy();
    if (scenario == 15) {
     schema.insert(schema.size() - 1,
      ", futureField TEXT GENERATED ALWAYS AS (text || '-stored') STORED");
    }
    exec(db, schema);
   }
  if (!fresh) insert(db, full);
  if (scenario == 4) {
   exec(db, "ALTER TABLE DeletedMessage ADD COLUMN mediaPath TEXT NOT NULL DEFAULT 'media-A'");
  } else if (scenario == 5) {
   exec(db, "ALTER TABLE DeletedMessage ADD COLUMN replyFlags INTEGER NOT NULL DEFAULT 9");
  } else if (scenario == 6 || scenario == 9) {
   exec(db, "ALTER TABLE DeletedMessage ADD COLUMN futureField TEXT NOT NULL DEFAULT 'preserve-me'");
  } else if (scenario == 7) {
   exec(db, "ALTER TABLE DeletedMessage DROP COLUMN text");
  } else if (scenario == 8) {
   // A same-name column with a incompatible declared type must not be recreated.
   exec(db, "ALTER TABLE DeletedMessage RENAME COLUMN views TO oldViews");
   exec(db, "ALTER TABLE DeletedMessage ADD COLUMN views TEXT NOT NULL DEFAULT '0'");
   exec(db, "ALTER TABLE DeletedMessage DROP COLUMN oldViews");
  } else if (scenario == 11) {
   exec(db, "CREATE TABLE DeletedMessage_luxury_new(marker TEXT)");
   exec(db, "INSERT INTO DeletedMessage_luxury_new VALUES('unrelated')");
  } else if (scenario == 12 || scenario == 13) {
   exec(db, "UPDATE sqlite_sequence SET seq=100 WHERE name='DeletedMessage'");
   if (scenario == 13) exec(db, "DELETE FROM DeletedMessage");
  }
  if (scenario == 14 || scenario == 17) {
    exec(db, "ALTER TABLE DeletedMessage ADD COLUMN futureField TEXT GENERATED ALWAYS AS (text || '-virtual') VIRTUAL");
   } else if (scenario == 16) {
    exec(db, "ALTER TABLE DeletedMessage ADD COLUMN mediaPath TEXT GENERATED ALWAYS AS (text) VIRTUAL");
   }
   const auto before = schemaNames(db);
  if (scenario == 1) {
   int setting = 1;
   require(sqlite3_db_config(db, SQLITE_DBCONFIG_DQS_DML, 0, &setting) == SQLITE_OK && setting == 0, "Disable DQS");
  }
  if (scenario == 10) sqlite3_set_authorizer(db, denyDrop, nullptr);
  const auto blocked = (scenario >= 6 && scenario <= 11) || scenario >= 14;
  auto threw = false;
  try {
   convergeMessageTables(db);
   syncSchemaWithoutLoss(database);
  } catch (const std::exception &) { threw = true; }
  if (scenario == 10) sqlite3_set_authorizer(db, nullptr, nullptr);
  require(threw == blocked && ConvergeFailed == blocked, "Wrong fail-closed result");
  const auto after = schemaNames(db);
  require(blocked ? before == after : after.size() == 30, "Shape lost or incomplete");
  const auto count = database.count<DeletedMessage>();
  require(count == (fresh || scenario == 13 ? 0 : 1), "Saved rows lost");
  if (!blocked && !fresh && scenario != 13) {
   const auto row = database.get<DeletedMessage>(7);
   require(row.text == "saved-A" && row.userId == 11 && row.messageId == 99
    && row.postAuthor == "author-A" && row.textEntities == std::vector<char>({0, 17}), "Saved payload changed");
   require(row.mediaPath == (full || scenario == 4 ? "media-A" : "/"), "Partial media field lost");
   require(row.replyFlags == (full || scenario == 5 ? 9 : 0), "Partial reply field lost");
   if (full) require(row.fwdPostAuthor == "forward-A" && row.documentSerialized == std::vector<char>({52}), "Full metadata changed");
   convergeMessageTables(db);
   syncSchemaWithoutLoss(database);
   require(database.count<DeletedMessage>() == count, "Repeated migration lost rows");
  }
  if (scenario == 12 || scenario == 13) {
   auto row = DeletedMessage();
   row.text = "new";
   require(database.insert(row) == 101, "Autoincrement high-water mark lost");
  }
  if (scenario == 11) exec(db, "SELECT marker FROM DeletedMessage_luxury_new WHERE marker='unrelated'");
  sqlite3_close(db);
  std::cout << "case " << scenario << (blocked ? " safely blocked" : " preserved") << "\n";
 }
 ayugramShapes(argv[1]);
 std::cout << "PASS 22 production migration controls\n";
}
'''.replace('FUNCTIONS', functions).replace('TABLE_HERE', table)
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--cc', default=os.environ.get('CC') or shutil.which('gcc') or r'C:\Strawberry\c\bin\gcc.EXE')
parser.add_argument('--cxx', default=os.environ.get('CXX') or shutil.which('g++') or r'C:\Strawberry\c\bin\g++.EXE')
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix='luxury-db-migration-') as directory:
    tmp = Path(directory)
    fixture = tmp / 'controls.cpp'
    fixture.write_text(cpp, encoding='utf-8')
    obj = tmp / 'sqlite.o'
    exe = tmp / ('controls.exe' if os.name == 'nt' else 'controls')
    for command in (
        [args.cc, '-O0', '-c', str(root / 'Telegram/SourceFiles/luxury/libs/sqlite/sqlite3.c'), '-o', str(obj)],
        [args.cxx, '-std=c++20', '-O0', '-g', '-D_DEBUG', '-I', str(root), str(fixture), str(obj), '-o', str(exe)],
        [str(exe), str(tmp)],
    ):
        subprocess.run(command, check=True)
print('Database migration: PASS (actual extracted C++, synthetic DBs; not native Qt initialization)')
