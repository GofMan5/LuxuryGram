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
		make_column("fwdFlags", &DeletedMessage::fwdFlags),
		make_column("fwdFromId", &DeletedMessage::fwdFromId),
		make_column("fwdName", &DeletedMessage::fwdName),
		make_column("fwdDate", &DeletedMessage::fwdDate),
		make_column("fwdPostAuthor", &DeletedMessage::fwdPostAuthor),
		make_column(
			"postAuthor",
			&DeletedMessage::postAuthor,
			default_value("")),
		make_column("replyFlags", &DeletedMessage::replyFlags),
		make_column("replyMessageId", &DeletedMessage::replyMessageId),
		make_column("replyPeerId", &DeletedMessage::replyPeerId),
		make_column("replyTopId", &DeletedMessage::replyTopId),
		make_column("replyForumTopic", &DeletedMessage::replyForumTopic),
		make_column("replySerialized", &DeletedMessage::replySerialized),
		make_column("entityCreateDate", &DeletedMessage::entityCreateDate),
		make_column("text", &DeletedMessage::text),
		make_column("textEntities", &DeletedMessage::textEntities),
		make_column("mediaPath", &DeletedMessage::mediaPath),
		make_column("hqThumbPath", &DeletedMessage::hqThumbPath),
		make_column("documentType", &DeletedMessage::documentType),
		make_column("documentSerialized", &DeletedMessage::documentSerialized),
		make_column("thumbsSerialized", &DeletedMessage::thumbsSerialized),
		make_column("documentAttributesSerialized", &DeletedMessage::documentAttributesSerialized),
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
		make_column("fwdFlags", &EditedMessage::fwdFlags),
		make_column("fwdFromId", &EditedMessage::fwdFromId),
		make_column("fwdName", &EditedMessage::fwdName),
		make_column("fwdDate", &EditedMessage::fwdDate),
		make_column("fwdPostAuthor", &EditedMessage::fwdPostAuthor),
		make_column(
			"postAuthor",
			&EditedMessage::postAuthor,
			default_value("")),
		make_column("replyFlags", &EditedMessage::replyFlags),
		make_column("replyMessageId", &EditedMessage::replyMessageId),
		make_column("replyPeerId", &EditedMessage::replyPeerId),
		make_column("replyTopId", &EditedMessage::replyTopId),
		make_column("replyForumTopic", &EditedMessage::replyForumTopic),
		make_column("replySerialized", &EditedMessage::replySerialized),
		make_column("entityCreateDate", &EditedMessage::entityCreateDate),
		make_column("text", &EditedMessage::text),
		make_column("textEntities", &EditedMessage::textEntities),
		make_column("mediaPath", &EditedMessage::mediaPath),
		make_column("hqThumbPath", &EditedMessage::hqThumbPath),
		make_column("documentType", &EditedMessage::documentType),
		make_column("documentSerialized", &EditedMessage::documentSerialized),
		make_column("thumbsSerialized", &EditedMessage::thumbsSerialized),
		make_column("documentAttributesSerialized", &EditedMessage::documentAttributesSerialized),
		make_column("mimeType", &EditedMessage::mimeType)
	),
	make_table<DeletedDialog>(
		"DeletedDialog",
		make_column("fakeId", &DeletedDialog::fakeId, primary_key().autoincrement()),
		make_column("userId", &DeletedDialog::userId),
		make_column("dialogId", &DeletedDialog::dialogId),
		make_column("peerId", &DeletedDialog::peerId),
		make_column("folderId", &DeletedDialog::folderId),
		make_column("topMessage", &DeletedDialog::topMessage),
		make_column("lastMessageDate", &DeletedDialog::lastMessageDate),
		make_column("flags", &DeletedDialog::flags),
		make_column("entityCreateDate", &DeletedDialog::entityCreateDate)
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
	),
	make_table<SpyMessageRead>(
		"SpyMessageRead",
		make_column("fakeId", &SpyMessageRead::fakeId, primary_key().autoincrement()),
		make_column("userId", &SpyMessageRead::userId),
		make_column("dialogId", &SpyMessageRead::dialogId),
		make_column("messageId", &SpyMessageRead::messageId),
		make_column("entityCreateDate", &SpyMessageRead::entityCreateDate)
	),
	make_table<SpyMessageContentsRead>(
		"SpyMessageContentsRead",
		make_column("fakeId", &SpyMessageContentsRead::fakeId, primary_key().autoincrement()),
		make_column("userId", &SpyMessageContentsRead::userId),
		make_column("dialogId", &SpyMessageContentsRead::dialogId),
		make_column("messageId", &SpyMessageContentsRead::messageId),
		make_column("entityCreateDate", &SpyMessageContentsRead::entityCreateDate)
	)
);

namespace {

std::recursive_mutex DatabaseMutex;

} // namespace

namespace LuxuryMigrations {

void migrateToV1(decltype(storage) &storage) {
	// drop RegexFilter table as we've added primary_key()
	storage.drop_table_if_exists("RegexFilter");
	LOG(("Migration to V1 successful."));
}

}

bool runMigrations(decltype(storage) &storage) {
	constexpr int kLatestVersion = 1;

	const std::map<int, Fn<void(decltype(storage) &)>> migrations = {
		{1, LuxuryMigrations::migrateToV1},
	};

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

	if (currentVersion >= kLatestVersion) {
		LOG(("Database is ok"));
		return true;
	}

	LOG(("Database version: %1. Latest version: %2.").arg(currentVersion).arg(kLatestVersion));

	for (int v = currentVersion + 1; v <= kLatestVersion; ++v) {
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
	try {
		storage.sync_schema(true);

		if (!runMigrations(storage)) {
			return;
		}

		storage.sync_schema(true);
	} catch (const std::exception &ex) {
		LOG(("Database initialization failed: %1").arg(ex.what()));
		if (!moveCurrentDatabase()) {
			return;
		}

		storage.sync_schema(true);
		if (!storage.get_pointer<SchemaVersion>(1)) {
			storage.insert(SchemaVersion{1, 0});
		}
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

bool hasDeletedMessages(ID userId, ID dialogId, ID topicId) {
	const auto lock = std::lock_guard(DatabaseMutex);
	try {
		return !storage.select(
			columns(column<DeletedMessage>(&DeletedMessage::dialogId)),
			where(
				column<DeletedMessage>(&DeletedMessage::userId) == userId and
				column<DeletedMessage>(&DeletedMessage::dialogId) == dialogId and
				(column<DeletedMessage>(&DeletedMessage::topicId) == topicId or topicId == 0)
			),
			limit(1)
		).empty();
	} catch (std::exception &ex) {
		LOG(("Failed to check if dialog has deleted message: %1").arg(ex.what()));
		return false;
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

int getCount() {
	const auto lock = std::lock_guard(DatabaseMutex);
	try {
		return storage.count<RegexFilter>();
	} catch (std::exception &ex) {
		LOG(("Failed to get count: %1").arg(ex.what()));
		return 0;
	}
}

RegexFilter getById(std::vector<char> id) {
	const auto lock = std::lock_guard(DatabaseMutex);
	try {
		return storage.get<RegexFilter>(
			where(column<RegexFilter>(&RegexFilter::id) == std::move(id))
		);
	} catch (std::exception &ex) {
		LOG(("Failed to get filters by id: %1").arg(ex.what()));
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

void addRegexFilter(const RegexFilter &filter) {
	const auto lock = std::lock_guard(DatabaseMutex);
	try {
		storage.begin_transaction();
		storage.replace(filter); // we're using replace as we set std::vector<char> as primary key
		storage.commit();
	} catch (std::exception &ex) {
		try {
			storage.rollback();
		} catch (...) {
		}
		LOG(("Failed to save regex filter for some reason: %1").arg(ex.what()));
	}
}

void addRegexExclusion(const RegexFilterGlobalExclusion &exclusion) {
	const auto lock = std::lock_guard(DatabaseMutex);
	try {
		storage.begin_transaction();
		storage.insert(exclusion);
		storage.commit();
	} catch (std::exception &ex) {
		try {
			storage.rollback();
		} catch (...) {
		}
		LOG(("Failed to save regex filter exclusion for some reason: %1").arg(ex.what()));
	}
}

void updateRegexFilter(const RegexFilter &filter) {
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
	} catch (std::exception &ex) {
		LOG(("Failed to update regex filter for some reason: %1").arg(ex.what()));
	}
}

void deleteFilter(const std::vector<char> &id) {
	const auto lock = std::lock_guard(DatabaseMutex);
	try {
		storage.remove_all<RegexFilter>(
			where(column<RegexFilter>(&RegexFilter::id) == id)
		);
	} catch (std::exception &ex) {
		LOG(("Failed to delete regex filter for some reason: %1").arg(ex.what()));
	}
}

void deleteExclusionsByFilterId(const std::vector<char> &id) {
	const auto lock = std::lock_guard(DatabaseMutex);
	try {
		storage.remove_all<RegexFilterGlobalExclusion>(
			where(column<RegexFilterGlobalExclusion>(&RegexFilterGlobalExclusion::filterId) == id)
		);
	} catch (std::exception &ex) {
		LOG(("Failed to delete regex filter exclusion by filter id for some reason: %1").arg(ex.what()));
	}
}

void deleteExclusion(ID dialogId, const std::vector<char> &filterId) {
	const auto lock = std::lock_guard(DatabaseMutex);
	try {
		storage.remove_all<RegexFilterGlobalExclusion>(
			where(column<RegexFilterGlobalExclusion>(&RegexFilterGlobalExclusion::filterId) == filterId and
				column<RegexFilterGlobalExclusion>(&RegexFilterGlobalExclusion::dialogId) == dialogId
			)
		);
	} catch (std::exception &ex) {
		LOG(("Failed to delete regex filter exclusion for some reason: %1").arg(ex.what()));
	}
}

void deleteAllFilters() {
	const auto lock = std::lock_guard(DatabaseMutex);
	try {
		storage.remove_all<RegexFilter>();
	} catch (std::exception &ex) {
		LOG(("Failed to delete all regex filter for some reason: %1").arg(ex.what()));
	}
}

void deleteAllExclusions() {
	const auto lock = std::lock_guard(DatabaseMutex);
	try {
		storage.remove_all<RegexFilterGlobalExclusion>();
	} catch (std::exception &ex) {
		LOG(("Failed to delete all regex filter exclusions for some reason: %1").arg(ex.what()));
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
