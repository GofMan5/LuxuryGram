// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "luxury/data/luxury_database.h"

#include "luxury/data/entities.h"
#include "luxury/libs/sqlite/sqlite_orm.h"
#include "base/unixtime.h"

#include <QFile>

#include <array>
#include <mutex>

using namespace sqlite_orm;
auto storage = make_storage(
	"./tdata/ayudata.db",
	make_table<SchemaVersion>(
		"SchemaVersion",
		make_column("id", &SchemaVersion::id, primary_key()),
		make_column("version", &SchemaVersion::version)
	),
	make_index("idx_deleted_message_userId_dialogId_topicId_messageId",
			   column<DeletedMessage>(&DeletedMessage::userId),
			   column<DeletedMessage>(&DeletedMessage::dialogId),
			   column<DeletedMessage>(&DeletedMessage::topicId),
			   column<DeletedMessage>(&DeletedMessage::messageId)),
	make_index("idx_edited_message_userId_dialogId_messageId",
			   column<EditedMessage>(&EditedMessage::userId),
			   column<EditedMessage>(&EditedMessage::dialogId),
			   column<EditedMessage>(&EditedMessage::messageId)),
	make_table<DeletedMessage>(
		"DeletedMessage",
		make_column("fakeId", &DeletedMessage::fakeId, primary_key().autoincrement()),
		make_column("userId", &DeletedMessage::userId),
		make_column("dialogId", &DeletedMessage::dialogId),
		make_column("groupedId", &DeletedMessage::groupedId),
		make_column("peerId", &DeletedMessage::peerId),
		make_column("fromId", &DeletedMessage::fromId),
		make_column("topicId", &DeletedMessage::topicId),
		make_column("messageId", &DeletedMessage::messageId),
		make_column("date", &DeletedMessage::date),
		make_column("flags", &DeletedMessage::flags),
		make_column("editDate", &DeletedMessage::editDate),
		make_column("views", &DeletedMessage::views),
		// The default is what the existing databases were created with; without
		// it sync_schema sees a mismatch and rebuilds the whole table.
		make_column(
			"postAuthor",
			&DeletedMessage::postAuthor,
			default_value("")),
		make_column("entityCreateDate", &DeletedMessage::entityCreateDate),
		make_column("text", &DeletedMessage::text),
		make_column("textEntities", &DeletedMessage::textEntities),
		// Nothing reads or writes the rest. They are declared because the tables
		// in the wild have the columns, and sqlite_orm removes any column the
		// storage does not declare -- one ALTER TABLE DROP COLUMN each, which
		// SQLite implements by rewriting every row. See the note in entities.h.
		//
		// None of them may carry default_value: calculate_remove_add_columns
		// compares whether a default EXISTS, not what it is, so declaring one
		// where the on-disk column has none makes the shapes unequal and buys the
		// whole-table copy this is here to avoid. postAuthor above is the
		// opposite case and must keep its default for exactly the same reason.
		make_column("fwdPostAuthor", &DeletedMessage::fwdPostAuthor),
		make_column("replyFlags", &DeletedMessage::replyFlags),
		make_column("replyMessageId", &DeletedMessage::replyMessageId),
		make_column("replyPeerId", &DeletedMessage::replyPeerId),
		make_column("replyTopId", &DeletedMessage::replyTopId),
		make_column("replyForumTopic", &DeletedMessage::replyForumTopic),
		make_column("replySerialized", &DeletedMessage::replySerialized),
		make_column("mediaPath", &DeletedMessage::mediaPath),
		make_column("hqThumbPath", &DeletedMessage::hqThumbPath),
		make_column("documentType", &DeletedMessage::documentType),
		make_column("documentSerialized", &DeletedMessage::documentSerialized),
		make_column("thumbsSerialized", &DeletedMessage::thumbsSerialized),
		make_column(
			"documentAttributesSerialized",
			&DeletedMessage::documentAttributesSerialized),
		make_column("mimeType", &DeletedMessage::mimeType)
	),
	make_table<EditedMessage>(
		"EditedMessage",
		make_column("fakeId", &EditedMessage::fakeId, primary_key().autoincrement()),
		make_column("userId", &EditedMessage::userId),
		make_column("dialogId", &EditedMessage::dialogId),
		make_column("groupedId", &EditedMessage::groupedId),
		make_column("peerId", &EditedMessage::peerId),
		make_column("fromId", &EditedMessage::fromId),
		make_column("topicId", &EditedMessage::topicId),
		make_column("messageId", &EditedMessage::messageId),
		make_column("date", &EditedMessage::date),
		make_column("flags", &EditedMessage::flags),
		make_column("editDate", &EditedMessage::editDate),
		make_column("views", &EditedMessage::views),
		make_column(
			"postAuthor",
			&EditedMessage::postAuthor,
			default_value("")),
		make_column("entityCreateDate", &EditedMessage::entityCreateDate),
		make_column("text", &EditedMessage::text),
		make_column("textEntities", &EditedMessage::textEntities)
	),
	make_table<RegexFilter>(
		"RegexFilter",
		make_column("id", &RegexFilter::id, primary_key()),
		make_column("text", &RegexFilter::text),
		make_column("enabled", &RegexFilter::enabled),
		make_column("reversed", &RegexFilter::reversed),
		make_column("caseInsensitive", &RegexFilter::caseInsensitive),
		make_column("dialogId", &RegexFilter::dialogId)
	),
	make_table<RegexFilterGlobalExclusion>(
		"RegexFilterGlobalExclusion",
		make_column("fakeId", &RegexFilterGlobalExclusion::fakeId, primary_key().autoincrement()),
		make_column("dialogId", &RegexFilterGlobalExclusion::dialogId),
		make_column("filterId", &RegexFilterGlobalExclusion::filterId)
	)
);

namespace {

std::recursive_mutex DatabaseMutex;

// Long enough to outlast a WAL checkpoint by another connection, short enough
// that a stuck writer surfaces as a logged failure instead of a frozen UI.
constexpr auto kBusyTimeoutMs = 3000;

} // namespace

namespace LuxuryMigrations {

void migrateToV1(decltype(storage) &storage) {
	// drop RegexFilter table as we've added primary_key()
	storage.drop_table_if_exists("RegexFilter");
	LOG(("Migration to V1 successful."));
}

void migrateToV2(decltype(storage) &storage) {
	// These three were declared but never written to and never read from, so
	// sync_schema kept creating empty tables nobody queried. It only knows the
	// tables we still declare, hence the explicit drop.
	storage.drop_table_if_exists("DeletedDialog");
	storage.drop_table_if_exists("SpyMessageRead");
	storage.drop_table_if_exists("SpyMessageContentsRead");
	LOG(("Migration to V2 successful."));
}

}

constexpr int kLatestSchemaVersion = 2;

bool runMigrations(decltype(storage) &storage, bool freshDatabase) {
	const std::map<int, Fn<void(decltype(storage) &)>> migrations = {
		{1, LuxuryMigrations::migrateToV1},
		{2, LuxuryMigrations::migrateToV2},
	};

	if (freshDatabase) {
		// sync_schema() has just built every table at its current shape, so
		// there is nothing to migrate. Running the list anyway would let
		// migrateToV1 drop the RegexFilter table it had only just created.
		storage.insert(SchemaVersion{1, kLatestSchemaVersion});
		return true;
	}

	int currentVersion = 0;
	try {
		if (auto versionRow = storage.get_pointer<SchemaVersion>(1)) {
			currentVersion = versionRow->version;
		} else {
			storage.insert(SchemaVersion{1, 0});
		}
	} catch (...) {
		LOG(("No SchemaVersion, assuming 0"));
		storage.insert(SchemaVersion{1, 0});
	}

	if (currentVersion >= kLatestSchemaVersion) {
		LOG(("Database is ok"));
		return true;
	}

	LOG(("Database version: %1. Latest version: %2.").arg(currentVersion).arg(kLatestSchemaVersion));

	for (int v = currentVersion + 1; v <= kLatestSchemaVersion; ++v) {
		if (migrations.contains(v)) {
			try {
				LOG(("Migration for version: %1").arg(v));
				storage.begin_transaction();

				migrations.at(v)(storage);

				storage.update_all(set(c(&SchemaVersion::version) = v), where(c(&SchemaVersion::id) == 1));
				storage.commit();
				LOG(("Applied migration for version: %1.").arg(v));
			} catch (...) {
				storage.rollback();
				LOG(("Failed to apply migration for version: %1.").arg(v));
				return LuxuryDatabase::moveCurrentDatabase();
			}
		}
	}
	return true;
}

namespace LuxuryDatabase {

void async(FnMut<void()> &&work) {
	// One queue, so posted work runs in the order it was posted. DatabaseMutex
	// would keep it safe either way, but not correct: a clear and an insert racing
	// on the thread pool can swap.
	static auto queue = crl::queue();
	queue.async(std::move(work));
}

bool moveCurrentDatabase() {
	const auto lock = std::lock_guard(DatabaseMutex);
	const auto time = base::unixtime::now();
	const auto source = u"./tdata/ayudata.db"_q;
	auto target = u"./tdata/ayudata_%1.db"_q.arg(time);
	for (auto index = 2
		; QFile::exists(target)
		|| QFile::exists(target + u"-shm"_q)
		|| QFile::exists(target + u"-wal"_q)
		; ++index) {
		target = u"./tdata/ayudata_%1_%2.db"_q.arg(time).arg(index);
	}
	const auto files = std::array{
		std::pair(source + u"-shm"_q, target + u"-shm"_q),
		std::pair(source + u"-wal"_q, target + u"-wal"_q),
		std::pair(source, target),
	};
	auto moved = std::vector<std::pair<QString, QString>>();
	for (const auto &[from, to] : files) {
		if (!QFile::exists(from)) {
			continue;
		}
		if (QFile::rename(from, to)) {
			moved.emplace_back(from, to);
			continue;
		}
		LOG(("Failed to preserve database file: %1").arg(from));
		for (auto i = moved.rbegin(); i != moved.rend(); ++i) {
			if (!QFile::rename(i->second, i->first)) {
				LOG(("Failed to roll back database file: %1").arg(i->first));
			}
		}
		return false;
	}
	return true;
}

void initialize() {
	const auto lock = std::lock_guard(DatabaseMutex);
	// A missing file means a first run: sync_schema() below builds everything at
	// its current shape, so the migration list must not touch it. Check before
	// sync_schema, which creates the file.
	const auto freshDatabase = !QFile::exists(u"./tdata/ayudata.db"_q);
	// sqlite_orm opens and closes the connection around statements and only
	// re-applies synchronous/journal_mode itself, so busy_timeout has to be
	// reinstalled on every open or the first checkpoint contention becomes
	// SQLITE_BUSY instead of a short wait.
	storage.on_open = [](sqlite3 *db) {
		sqlite3_busy_timeout(db, kBusyTimeoutMs);
	};
	try {
		// Without these, sqlite defaults to a rollback journal with
		// synchronous=FULL, which costs two fsyncs per commit. Writes happen
		// on the main thread (one transaction per edited message), so a burst
		// of edits used to be a burst of stalls.
		storage.pragma.journal_mode(journal_mode::WAL);
		storage.pragma.synchronous(1); // NORMAL

		storage.sync_schema(true);

		if (!runMigrations(storage, freshDatabase)) {
			return;
		}
	} catch (const std::exception &ex) {
		LOG(("Database initialization failed: %1").arg(ex.what()));
		if (!moveCurrentDatabase()) {
			return;
		}

		storage.sync_schema(true);
		// A database sync_schema just built is at the current version, not at 0.
		// Stamping 0 here made the next launch run every migration against it,
		// and migrateToV1 drops RegexFilter -- so any filter created after a
		// recovery vanished on the following start.
		if (!storage.get_pointer<SchemaVersion>(1)) {
			storage.insert(SchemaVersion{1, kLatestSchemaVersion});
		}
	}
	// By default sqlite_orm opens the file, runs the statement and closes it
	// again -- and closing the last connection to a WAL database checkpoints it
	// and deletes the -wal file. Every query paid for that, including the ones on
	// the main thread. Every entry point below is serialized by DatabaseMutex, so
	// holding one connection for the process lifetime is safe. Not any earlier
	// than here: moveCurrentDatabase() renames the file, which an open handle
	// would block on Windows.
	try {
		storage.open_forever();
	} catch (const std::exception &ex) {
		LOG(("Failed to hold the database connection open: %1").arg(ex.what()));
	}
}

void addEditedMessage(const EditedMessage &message) {
	const auto lock = std::lock_guard(DatabaseMutex);
	try {
		storage.begin_transaction();
		storage.insert(message);
		storage.commit();
	} catch (std::exception &ex) {
		try {
			storage.rollback();
		} catch (...) {
		}
		LOG(("Failed to save edited message: %1").arg(ex.what()));
	}
}

std::vector<EditedMessage> getEditedMessages(ID userId, ID dialogId, ID messageId, ID minId, ID maxId, int totalLimit) {
	const auto lock = std::lock_guard(DatabaseMutex);
	try {
		return storage.get_all<EditedMessage>(
			where(
				column<EditedMessage>(&EditedMessage::userId) == userId and
				column<EditedMessage>(&EditedMessage::dialogId) == dialogId and
				column<EditedMessage>(&EditedMessage::messageId) == messageId and
				(column<EditedMessage>(&EditedMessage::fakeId) > minId or minId == 0) and
				(column<EditedMessage>(&EditedMessage::fakeId) < maxId or maxId == 0)
			),
			order_by(column<EditedMessage>(&EditedMessage::fakeId)).desc(),
			limit(totalLimit)
		);
	} catch (const std::exception &ex) {
		LOG(("Failed to load edited messages: %1").arg(ex.what()));
		return {};
	}
}

bool hasRevisions(ID userId, ID dialogId, ID messageId) {
	const auto lock = std::lock_guard(DatabaseMutex);
	try {
		return !storage.select(
			columns(column<EditedMessage>(&EditedMessage::messageId)),
			where(
				column<EditedMessage>(&EditedMessage::userId) == userId and
				column<EditedMessage>(&EditedMessage::dialogId) == dialogId and
				column<EditedMessage>(&EditedMessage::messageId) == messageId
			),
			limit(1)
		).empty();
	} catch (std::exception &ex) {
		LOG(("Failed to check if message has revisions: %1").arg(ex.what()));
		return false;
	}
}

void addDeletedMessage(DeletedMessage message) {
	auto messages = std::vector<DeletedMessage>();
	messages.push_back(std::move(message));
	addDeletedMessages(std::move(messages));
}

void addDeletedMessages(std::vector<DeletedMessage> &&messages) {
	if (messages.empty()) {
		return;
	}
	const auto lock = std::lock_guard(DatabaseMutex);
	try {
		storage.begin_transaction();
		for (const auto &message : messages) {
			storage.insert(message);
		}
		storage.commit();
	} catch (std::exception &ex) {
		try {
			storage.rollback();
		} catch (...) {
		}
		LOG(("Failed to save deleted message: %1").arg(ex.what()));
	}
}

std::vector<DeletedMessage> getDeletedMessages(ID userId, ID dialogId, ID topicId, ID minId, ID maxId, int totalLimit, const std::string &searchQuery) {
	const auto lock = std::lock_guard(DatabaseMutex);
	try {
		if (searchQuery.empty()) {
			return storage.get_all<DeletedMessage>(
				where(
					column<DeletedMessage>(&DeletedMessage::userId) == userId and
					column<DeletedMessage>(&DeletedMessage::dialogId) == dialogId and
					(column<DeletedMessage>(&DeletedMessage::topicId) == topicId or topicId == 0) and
					(column<DeletedMessage>(&DeletedMessage::messageId) > minId or minId == 0) and
					(column<DeletedMessage>(&DeletedMessage::messageId) < maxId or maxId == 0)
				),
				order_by(column<DeletedMessage>(&DeletedMessage::messageId)).desc(),
				limit(totalLimit)
			);
		}

		std::string escaped;
		escaped.reserve(searchQuery.size());
		for (const auto c : searchQuery) {
			if (c == '%' || c == '_' || c == '\\') {
				escaped += '\\';
			}
			escaped += c;
		}
		const auto pattern = "%" + escaped + "%";
		return storage.get_all<DeletedMessage>(
			where(
				column<DeletedMessage>(&DeletedMessage::userId) == userId and
				column<DeletedMessage>(&DeletedMessage::dialogId) == dialogId and
				(column<DeletedMessage>(&DeletedMessage::topicId) == topicId or topicId == 0) and
				(column<DeletedMessage>(&DeletedMessage::messageId) > minId or minId == 0) and
				(column<DeletedMessage>(&DeletedMessage::messageId) < maxId or maxId == 0) and
				like(column<DeletedMessage>(&DeletedMessage::text), pattern, "\\")
			),
			order_by(column<DeletedMessage>(&DeletedMessage::messageId)).desc(),
			limit(totalLimit)
		);
	} catch (const std::exception &ex) {
		LOG(("Failed to load deleted messages: %1").arg(ex.what()));
		return {};
	}
}

void removeDeletedMessage(ID userId, ID dialogId, ID messageId) {
	const auto lock = std::lock_guard(DatabaseMutex);
	try {
		storage.remove_all<DeletedMessage>(
			where(
				column<DeletedMessage>(&DeletedMessage::userId) == userId and
				column<DeletedMessage>(&DeletedMessage::dialogId) == dialogId and
				column<DeletedMessage>(&DeletedMessage::messageId) == messageId
			)
		);
	} catch (std::exception &ex) {
		LOG(("Failed to remove deleted message: %1").arg(ex.what()));
	}
}

void clearDeletedMessages(ID userId, ID dialogId, ID topicId) {
	const auto lock = std::lock_guard(DatabaseMutex);
	try {
		storage.remove_all<DeletedMessage>(
			where(
				column<DeletedMessage>(&DeletedMessage::userId) == userId and
				column<DeletedMessage>(&DeletedMessage::dialogId) == dialogId and
				(column<DeletedMessage>(&DeletedMessage::topicId) == topicId or topicId == 0)
			)
		);
	} catch (const std::exception &ex) {
		LOG(("Failed to clear deleted messages: %1").arg(ex.what()));
	}
}

template<typename T>
std::vector<T> getAllT() {
	const auto lock = std::lock_guard(DatabaseMutex);
	try {
		return storage.get_all<T>();
	} catch (std::exception &ex) {
		LOG(("Failed to get all: %1").arg(ex.what()));
		return {};
	}
}

std::vector<RegexFilter> getAllRegexFilters() {
	return getAllT<RegexFilter>();
}

std::vector<RegexFilterGlobalExclusion> getAllFiltersExclusions() {
	return getAllT<RegexFilterGlobalExclusion>();
}

std::vector<RegexFilter> getExcludedByDialogId(ID dialogId) {
	const auto lock = std::lock_guard(DatabaseMutex);
	try {
		return storage.get_all<RegexFilter>(
			where(in(&RegexFilter::id,
					 storage.select(columns(&RegexFilterGlobalExclusion::filterId),
									where(is_equal(&RegexFilterGlobalExclusion::dialogId, dialogId))
					 )
			))
		);
	} catch (std::exception &ex) {
		LOG(("Failed to get excluded by dialog id: %1").arg(ex.what()));
		return {};
	}
}

std::vector<RegexFilter> getShared() {
	const auto lock = std::lock_guard(DatabaseMutex);
	try {
		return storage.get_all<RegexFilter>(
			where(is_null(column<RegexFilter>(&RegexFilter::dialogId)))
		);
	} catch (std::exception &ex) {
		LOG(("Failed to get shared filters: %1").arg(ex.what()));
		return {};
	}
}

std::vector<RegexFilter> getByDialogId(ID dialogId) {
	const auto lock = std::lock_guard(DatabaseMutex);
	try {
		return storage.get_all<RegexFilter>(
			where(column<RegexFilter>(&RegexFilter::dialogId) == dialogId)
		);
	} catch (std::exception &ex) {
		LOG(("Failed to get filters by dialog id: %1").arg(ex.what()));
		return {};
	}
}

bool applyFilterChanges(
		const std::vector<RegexFilter> &newFilters,
		const std::vector<std::vector<char>> &removeFiltersById,
		const std::vector<RegexFilter> &filterOverrides,
		const std::vector<RegexFilterGlobalExclusion> &newExclusions,
		const std::vector<RegexFilterGlobalExclusion> &removeExclusions) {
	const auto lock = std::lock_guard(DatabaseMutex);
	try {
		storage.begin_transaction();
		for (const auto &filter : newFilters) {
			storage.replace(filter);
		}
		for (const auto &id : removeFiltersById) {
			storage.remove_all<RegexFilterGlobalExclusion>(
				where(column<RegexFilterGlobalExclusion>(
					&RegexFilterGlobalExclusion::filterId) == id));
			storage.remove_all<RegexFilter>(
				where(column<RegexFilter>(&RegexFilter::id) == id));
		}
		for (const auto &filter : filterOverrides) {
			storage.update_all(
				set(
					c(&RegexFilter::text) = filter.text,
					c(&RegexFilter::enabled) = filter.enabled,
					c(&RegexFilter::reversed) = filter.reversed,
					c(&RegexFilter::caseInsensitive) = filter.caseInsensitive,
					c(&RegexFilter::dialogId) = filter.dialogId),
				where(c(&RegexFilter::id) == filter.id));
		}
		for (const auto &exclusion : newExclusions) {
			storage.insert(exclusion);
		}
		for (const auto &exclusion : removeExclusions) {
			storage.remove_all<RegexFilterGlobalExclusion>(
				where(
					column<RegexFilterGlobalExclusion>(
						&RegexFilterGlobalExclusion::filterId)
						== exclusion.filterId
					&& column<RegexFilterGlobalExclusion>(
						&RegexFilterGlobalExclusion::dialogId)
						== exclusion.dialogId));
		}
		storage.commit();
		return true;
	} catch (const std::exception &ex) {
		try {
			storage.rollback();
		} catch (...) {
		}
		LOG(("Failed to apply regex filter changes: %1").arg(ex.what()));
		return false;
	}
}

bool addRegexFilter(const RegexFilter &filter) {
	const auto lock = std::lock_guard(DatabaseMutex);
	try {
		storage.begin_transaction();
		storage.replace(filter); // we're using replace as we set std::vector<char> as primary key
		storage.commit();
		return true;
	} catch (std::exception &ex) {
		try {
			storage.rollback();
		} catch (...) {
		}
		LOG(("Failed to save regex filter for some reason: %1").arg(ex.what()));
		return false;
	}
}

bool addRegexExclusion(const RegexFilterGlobalExclusion &exclusion) {
	const auto lock = std::lock_guard(DatabaseMutex);
	try {
		storage.begin_transaction();
		storage.insert(exclusion);
		storage.commit();
		return true;
	} catch (std::exception &ex) {
		try {
			storage.rollback();
		} catch (...) {
		}
		LOG(("Failed to save regex filter exclusion for some reason: %1").arg(ex.what()));
		return false;
	}
}

bool updateRegexFilter(const RegexFilter &filter) {
	const auto lock = std::lock_guard(DatabaseMutex);
	try {
		storage.update_all(
			set(
				c(&RegexFilter::text) = filter.text,
				c(&RegexFilter::enabled) = filter.enabled,
				c(&RegexFilter::reversed) = filter.reversed,
				c(&RegexFilter::caseInsensitive) = filter.caseInsensitive,
				c(&RegexFilter::dialogId) = filter.dialogId
			),
			where(c(&RegexFilter::id) == filter.id)
		);
		return true;
	} catch (std::exception &ex) {
		LOG(("Failed to update regex filter for some reason: %1").arg(ex.what()));
		return false;
	}
}

bool deleteFilter(const std::vector<char> &id) {
	const auto lock = std::lock_guard(DatabaseMutex);
	try {
		storage.remove_all<RegexFilter>(
			where(column<RegexFilter>(&RegexFilter::id) == id)
		);
		return true;
	} catch (std::exception &ex) {
		LOG(("Failed to delete regex filter for some reason: %1").arg(ex.what()));
		return false;
	}
}

bool deleteExclusionsByFilterId(const std::vector<char> &id) {
	const auto lock = std::lock_guard(DatabaseMutex);
	try {
		storage.remove_all<RegexFilterGlobalExclusion>(
			where(column<RegexFilterGlobalExclusion>(&RegexFilterGlobalExclusion::filterId) == id)
		);
		return true;
	} catch (std::exception &ex) {
		LOG(("Failed to delete regex filter exclusion by filter id for some reason: %1").arg(ex.what()));
		return false;
	}
}

bool deleteExclusion(ID dialogId, const std::vector<char> &filterId) {
	const auto lock = std::lock_guard(DatabaseMutex);
	try {
		storage.remove_all<RegexFilterGlobalExclusion>(
			where(column<RegexFilterGlobalExclusion>(&RegexFilterGlobalExclusion::filterId) == filterId and
				column<RegexFilterGlobalExclusion>(&RegexFilterGlobalExclusion::dialogId) == dialogId
			)
		);
		return true;
	} catch (std::exception &ex) {
		LOG(("Failed to delete regex filter exclusion for some reason: %1").arg(ex.what()));
		return false;
	}
}

bool deleteAllFilters() {
	const auto lock = std::lock_guard(DatabaseMutex);
	try {
		storage.remove_all<RegexFilter>();
		return true;
	} catch (std::exception &ex) {
		LOG(("Failed to delete all regex filter for some reason: %1").arg(ex.what()));
		return false;
	}
}

bool deleteAllExclusions() {
	const auto lock = std::lock_guard(DatabaseMutex);
	try {
		storage.remove_all<RegexFilterGlobalExclusion>();
		return true;
	} catch (std::exception &ex) {
		LOG(("Failed to delete all regex filter exclusions for some reason: %1").arg(ex.what()));
		return false;
	}
}

bool hasFilters() {
	const auto lock = std::lock_guard(DatabaseMutex);
	try {
		return !storage.select(
			columns(column<RegexFilter>(&RegexFilter::id)),
			limit(1)
		).empty();
	} catch (std::exception &ex) {
		LOG(("Failed to check if there's any filters: %1").arg(ex.what()));
		return false;
	}
}

bool hasPerDialogFilters() {
	const auto lock = std::lock_guard(DatabaseMutex);
	try {
		return
			!storage.select(
				columns(column<RegexFilter>(&RegexFilter::id)),
				where(is_not_null(column<RegexFilter>(&RegexFilter::dialogId))),
				limit(1)
			).empty() ||
			!storage.select(
				columns(column<RegexFilterGlobalExclusion>(&RegexFilterGlobalExclusion::fakeId)),
				limit(1)
			).empty();
	} catch (std::exception &ex) {
		LOG(("Failed to check if there's any filters: %1").arg(ex.what()));
		return false;
	}
}

}
