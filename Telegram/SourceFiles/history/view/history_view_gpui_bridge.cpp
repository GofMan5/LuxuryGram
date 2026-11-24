/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_gpui_bridge.h"

#include "history/history.h"

#include "data/data_messages.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#ifdef Q_OS_WIN
#include "base/platform/win/base_windows_safe_library.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#endif // Q_OS_WIN

namespace HistoryView {
namespace {

#ifdef Q_OS_WIN

// Mirrors `HistoryRowAbi` in rust/apps/luxurygram-shell/src/history_bridge.rs.
struct RowAbi {
	uint64_t peer;
	int64_t message;
	uint32_t date;
	uint8_t outgoing;
	const char *sender;
	size_t sender_len;
	const char *text;
	size_t text_len;
};

// Mirrors `HistoryRekeyAbi`.
struct RekeyAbi {
	uint64_t old_peer;
	int64_t old_message;
	uint64_t new_peer;
	int64_t new_message;
};

constexpr auto kDllName = "luxurygram_shell.dll";
constexpr auto kWindowCount = 50;

using Start = int (*)();
using Apply = int (*)(const RowAbi *, size_t, const RekeyAbi *, size_t);
using Clear = int (*)();

Start StartFn = nullptr;
Apply ApplyFn = nullptr;
Clear ClearFn = nullptr;

// The DLL is optional: the client without it behaves exactly as before.
bool LoadBridge() {
	static const auto loaded = [] {
		const auto library = base::Platform::SafeLoadLibrary(
			QDir(QCoreApplication::applicationDirPath())
				.filePath(QString::fromLatin1(kDllName))
				.toStdWString()
				.c_str());
		return base::Platform::LoadMethod(
				library,
				"luxurygram_history_start",
				StartFn)
			&& base::Platform::LoadMethod(
				library,
				"luxurygram_history_apply",
				ApplyFn)
			&& base::Platform::LoadMethod(
				library,
				"luxurygram_history_clear",
				ClearFn);
	}();
	return loaded;
}

#else // Q_OS_WIN

bool LoadBridge() {
	return false;
}

#endif // Q_OS_WIN

} // namespace

bool TryStartGpuiHistory() {
	if (!LoadBridge()) {
		return false;
	}
#ifdef Q_OS_WIN
	static const auto started = (StartFn() == 0);
	return started;
#else
	return false;
#endif
}

void ShowGpuiHistory(
		[[maybe_unused]] not_null<History*> history,
		[[maybe_unused]] rpl::lifetime &lifetime) {
	if (!TryStartGpuiHistory()) {
		return;
	}
#ifdef Q_OS_WIN
	const auto around = Data::MessagePosition();
	Data::HistoryTextViewer(history, around, kWindowCount, kWindowCount)
	| rpl::on_next([](std::optional<Data::HistoryTextSlice> slice) {
		if (!slice) {
			ClearFn();
			return;
		}
		std::vector<RowAbi> rows;
		rows.reserve(slice->rows.size());
		for (const auto &row : slice->rows) {
			rows.push_back({
				.peer = row.id.peer.value,
				.message = row.id.msg.bare,
				.date = uint32_t(row.date),
				.outgoing = row.outgoing ? uint8_t(1) : uint8_t(0),
				.sender = row.sender.c_str(),
				.sender_len = row.sender.size(),
				.text = row.text.c_str(),
				.text_len = row.text.size(),
			});
		}
		std::vector<RekeyAbi> rekeys;
		rekeys.reserve(slice->rekeys.size());
		for (const auto &rekey : slice->rekeys) {
			rekeys.push_back({
				.old_peer = rekey.oldId.peer.value,
				.old_message = rekey.oldId.msg.bare,
				.new_peer = rekey.newId.peer.value,
				.new_message = rekey.newId.msg.bare,
			});
		}
	}, lifetime);
#endif // Q_OS_WIN
}

} // namespace HistoryView
