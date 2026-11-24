// Test host for the opt-in GPUI DLL. Never uses a LuxuryGram profile.
#include <windows.h>

#include <QtCore/QCoreApplication>
#include <QtCore/QMetaObject>
#include <QtCore/QTimer>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <thread>

// GPUI imports TaskDialogIndirect, which requires Common Controls v6.
#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

using RunGpui = int (*)(
	std::uint64_t, std::int64_t, std::uint64_t,
	const char *, std::size_t, const char *, std::size_t);
using PushHistory = int (*)(
	std::uint64_t, std::int64_t, std::uint64_t,
	const char *, std::size_t, const char *, std::size_t);

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
	if (wcsstr(title, L"Native history test") != nullptr) {
		state->window = window;
		return FALSE;
	}
	return TRUE;
}

int wmain(int argc, wchar_t **argv) {
	if (argc != 2) {
		std::fputs("Expected the absolute path of the GPUI smoke DLL.\n", stderr);
		return 1;
	}
	const auto dll = LoadLibraryExW(argv[1], nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
	if (!dll) {
		std::fprintf(stderr, "LoadLibraryExW failed: %lu\n", GetLastError());
		return 1;
	}
	const auto run = reinterpret_cast<RunGpui>(
		GetProcAddress(dll, "luxurygram_gpui_windows_host_smoke_v2"));
	const auto push = reinterpret_cast<PushHistory>(
		GetProcAddress(dll, "luxurygram_push_history"));
	if (!run || !push) {
		std::fprintf(stderr, "GetProcAddress failed\n");
		return 1;
	}
	char appName[] = "gpui_windows_host_smoke";
	char *appArgs[] = { appName, nullptr };
	int appArgc = 1;
	QCoreApplication app(appArgc, appArgs);
	QTimer heartbeat;
	int ticks = 0;
	QObject::connect(&heartbeat, &QTimer::timeout, &app, [&] { ++ticks; });
	heartbeat.start(50);

	std::atomic<int> gpuiResult{1};
	std::thread ui([&] {
		constexpr char sender[] = "Native fixture";
		constexpr char text[] = "UTF-8 from C++: \xe2\x9c\x93";
		const auto result = run(42, -9, 1800000000,
			sender, sizeof(sender) - 1, text, sizeof(text) - 1);
		gpuiResult.store(result);
		if (!QMetaObject::invokeMethod(&app, [&app] { app.exit(); }, Qt::QueuedConnection)) {
			std::abort();
		}
	});

	constexpr char sender[] = "Native fixture";
	constexpr char pushed[] = "Pushed from Qt thread";
	bool pushedOk = false;
	HWND window = nullptr;
	for (int attempt = 0; attempt < 40; ++attempt) {
		FindState state{ GetCurrentProcessId(), nullptr };
		EnumWindows(findWindow, reinterpret_cast<LPARAM>(&state));
		if (state.window) {
			window = state.window;
			pushedOk = (push(43, -8, 1800000060,
				sender, sizeof(sender) - 1, pushed, sizeof(pushed) - 1) == 0);
			break;
		}
		QCoreApplication::processEvents();
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}
	if (window) {
		std::this_thread::sleep_for(std::chrono::milliseconds(300));
		PostMessageW(window, WM_CLOSE, 0, 0);
	}

	const auto result = app.exec();
	ui.join();
	std::fprintf(stderr, "Qt timer ticks: %d; pushed: %d; GPUI exit: %d\n",
		ticks, pushedOk ? 1 : 0, gpuiResult.load());
	return (ticks > 0 && pushedOk && gpuiResult.load() == 0) ? result : 1;
}
