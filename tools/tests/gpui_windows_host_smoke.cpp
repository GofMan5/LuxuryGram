// Test host for the opt-in GPUI DLL. Never uses a LuxuryGram profile.
#include <windows.h>

#include <QtCore/QCoreApplication>
#include <QtCore/QMetaObject>
#include <QtCore/QTimer>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <thread>

// GPUI imports TaskDialogIndirect, which requires Common Controls v6.
#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

using RunGpui = int (*)(
	std::uint64_t, std::int64_t, std::uint64_t,
	const char *, std::size_t, const char *, std::size_t);

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
	if (!run) {
		std::fprintf(stderr, "GetProcAddress failed: %lu\n", GetLastError());
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

	std::thread ui([&] {
		constexpr char sender[] = "Native fixture";
		constexpr char text[] = "UTF-8 from C++: \xe2\x9c\x93";
		constexpr char invalid[] = "\xff";
		const auto rejected = run(42, -9, 1800000000,
			sender, sizeof(sender) - 1, invalid, sizeof(invalid) - 1);
		const auto result = (rejected == 2)
			? run(42, -9, 1800000000,
				sender, sizeof(sender) - 1, text, sizeof(text) - 1)
			: 1;
		if (!QMetaObject::invokeMethod(&app, [&app, result] {
			app.exit(result);
		}, Qt::QueuedConnection)) {
			std::abort();
		}
	});
	const auto result = app.exec();
	ui.join();
	std::fprintf(stderr, "Qt timer ticks: %d; GPUI exit: %d\n", ticks, result);
	return (ticks > 0) ? result : 1;
}
