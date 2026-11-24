// Test host for the GPUI history bridge DLL. Never uses a LuxuryGram profile.
#include <windows.h>

#include <QtCore/QCoreApplication>
#include <QtCore/QMetaObject>
#include <QtCore/QTimer>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <thread>

// GPUI imports TaskDialogIndirect, which requires Common Controls v6.
#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

struct HistoryRowAbi {
	std::uint64_t peer;
	std::int64_t message;
	std::uint32_t date;
	std::uint8_t outgoing;
	const char *sender;
	std::size_t sender_len;
	const char *text;
	std::size_t text_len;
};
static_assert(sizeof(HistoryRowAbi) == 56, "ABI layout must match Rust");

struct HistoryRekeyAbi {
	std::uint64_t old_peer;
	std::int64_t old_message;
	std::uint64_t new_peer;
	std::int64_t new_message;
};

using Start = int (*)();
using Apply = int (*)(const HistoryRowAbi *, std::size_t, const HistoryRekeyAbi *, std::size_t);
using Clear = int (*)();
using Count = std::intptr_t (*)();

struct FindState {
	DWORD pid = 0;
	HWND window = nullptr;
};

BOOL CALLBACK findWindow(HWND window, LPARAM param) {
	const auto state = reinterpret_cast<FindState *>(param);
	DWORD pid = 0;
	GetWindowThreadProcessId(window, &pid);
	if (pid != state->pid) return TRUE;
	wchar_t title[256] = {};
	GetWindowTextW(window, title, 256);
	if (wcsstr(title, L"LuxuryGram") != nullptr) {
		state->window = window;
		return FALSE;
	}
	return TRUE;
}

bool waitFor(const Count &count, std::intptr_t expected, HWND *window) {
	for (int attempt = 0; attempt < 100; ++attempt) {
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
		QCoreApplication::processEvents();
		if (window && !*window) {
			FindState state{ GetCurrentProcessId(), nullptr };
			EnumWindows(findWindow, reinterpret_cast<LPARAM>(&state));
			*window = state.window;
		}
		if (count() == expected) return true;
	}
	return false;
}

int wmain(int argc, wchar_t **argv) {
	if (argc != 2) {
		std::fputs("Expected the absolute path of the GPUI bridge DLL.\n", stderr);
		return 1;
	}
	const auto dll = LoadLibraryExW(argv[1], nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
	if (!dll) {
		std::fprintf(stderr, "LoadLibraryExW failed: %lu\n", GetLastError());
		return 1;
	}
	const auto start = reinterpret_cast<Start>(GetProcAddress(dll, "luxurygram_history_start"));
	const auto apply = reinterpret_cast<Apply>(GetProcAddress(dll, "luxurygram_history_apply"));
	const auto clear = reinterpret_cast<Clear>(GetProcAddress(dll, "luxurygram_history_clear"));
	const auto count = reinterpret_cast<Count>(GetProcAddress(dll, "luxurygram_history_visible_count"));
	if (!start || !apply || !clear || !count) {
		std::fputs("GetProcAddress failed\n", stderr);
		return 1;
	}
	char appName[] = "gpui_bridge_host";
	char *appArgs[] = { appName, nullptr };
	int appArgc = 1;
	QCoreApplication app(appArgc, appArgs);
	QTimer heartbeat;
	int ticks = 0;
	QObject::connect(&heartbeat, &QTimer::timeout, &app, [&] { ++ticks; });
	heartbeat.start(50);

	if (start() != 0) {
		std::fputs("start failed\n", stderr);
		return 1;
	}

	constexpr char sender[] = "Native fixture";
	constexpr char text[] = "UTF-8 from C++: \xe2\x9c\x93";
	const HistoryRowAbi rows[] = {
		{ 42, -9, 1800000000u, 0, sender, sizeof(sender) - 1, text, sizeof(text) - 1 },
		{ 42, -8, 1800000060u, 1, sender, sizeof(sender) - 1, "second", 6 },
	};
	const HistoryRekeyAbi rekeys[] = { { 42, -1, 42, -9 } };

	constexpr char invalid[] = "\xff";
	const HistoryRowAbi bad = { 42, -7, 1800000000u, 0, sender, sizeof(sender) - 1, invalid, 1 };
	const auto rejected = (apply(&bad, 1, nullptr, 0) == 2);
	const auto queued = (apply(rows, 2, rekeys, 1) == 0);

	HWND window = nullptr;
	const auto populated = waitFor(count, 2, &window);
	const auto revoked = (clear() == 0) && waitFor(count, 0, nullptr);

	int exit = 0;
	if (window) {
		PostMessageW(window, WM_CLOSE, 0, 0);
	}
	// The GPUI thread owns the window; give it time to exit before reporting.
	for (int attempt = 0; attempt < 40; ++attempt) {
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
		QCoreApplication::processEvents();
	}
	std::fprintf(stderr, "Qt ticks: %d; rejected: %d; queued: %d; populated: %d; revoked: %d; exit: %d\n",
		ticks, rejected ? 1 : 0, queued ? 1 : 0, populated ? 1 : 0, revoked ? 1 : 0, exit);
	return (ticks > 0 && rejected && queued && populated && revoked) ? 0 : 1;
}
