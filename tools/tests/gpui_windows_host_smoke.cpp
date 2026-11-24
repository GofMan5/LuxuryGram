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
using ExportedWindow = std::intptr_t (*)();
using SendCallback = int (*)(void *, const char *, std::size_t);
using SetSendCallback = int (*)(SendCallback, void *);

std::atomic<int> g_sentCount{ 0 };
std::string g_sentText;

int onComposerSubmit(void *, const char *text, std::size_t len) {
	g_sentText.assign(text, len);
	++g_sentCount;
	return 1;
}

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
	const auto exportedWindow = reinterpret_cast<ExportedWindow>(GetProcAddress(dll, "luxurygram_history_window"));
	const auto setSendCallback = reinterpret_cast<SetSendCallback>(GetProcAddress(dll, "luxurygram_history_set_send_callback"));
	if (!start || !apply || !clear || !count || !exportedWindow || !setSendCallback) {
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

	// The exported handle must be the same window the title lookup found, so a
	// host can place the feed over its own chat rectangle without guessing.
	const auto handle = reinterpret_cast<HWND>(exportedWindow());
	const auto sameWindow = (handle != nullptr) && (handle == window);
	// Cross-thread SetWindowPos: the Qt thread moves the GPUI-owned window.
	const auto moved = sameWindow
		&& SetWindowPos(handle, nullptr, 120, 140, 700, 480, SWP_NOZORDER | SWP_NOACTIVATE);
	RECT placed = {};
	if (moved) {
		GetWindowRect(handle, &placed);
	}
	const auto geometryOk = moved
		&& (placed.left == 120)
		&& (placed.top == 140)
		&& (placed.right - placed.left == 700)
		&& (placed.bottom - placed.top == 480);
	// Hiding and showing from the host thread must reach the GPUI window.
	// ShowWindow reports the *previous* visibility, so only the resulting
	// state is checked, never its return value.
	const auto hidden = sameWindow && (ShowWindow(handle, SW_HIDE), !IsWindowVisible(handle));
	const auto shown = hidden
		&& (ShowWindow(handle, SW_SHOWNOACTIVATE), IsWindowVisible(handle));

	// Composer: a null callback is refused (1), a real one accepted (0). Then
	// type into the GPUI window from the host thread: 'h', 'i', Enter. The
	// callback runs on the GPUI thread and must receive exactly "hi" once.
	const auto nullRefused = (setSendCallback(nullptr, nullptr) == 1);
	const auto callbackSet = (setSendCallback(&onComposerSubmit, nullptr) == 0);
	bool composed = false;
	if (callbackSet && shown) {
		// The composer appears only once a callback exists: the feed learns
		// of it through its command channel and needs a frame to render the
		// focused field, so give it that before typing.
		std::this_thread::sleep_for(std::chrono::milliseconds(300));
		// GPUI derives `key_char` from WM_KEYDOWN via ToUnicode and routes keys
		// along its own focus path. The host posts the keys asynchronously, as a
		// real keyboard would: GPUI does not run its focus dispatch for a
		// synchronous cross-thread SendMessage. Two letter keys are sent, so
		// the text the callback receives has two characters in the host's
		// current keyboard layout; the exact letters are layout-dependent.
		const auto keyDown = [&](WPARAM vk) {
			const auto scan = MapVirtualKeyW(UINT(vk), MAPVK_VK_TO_VSC);
			PostMessageW(handle, WM_KEYDOWN, vk, LPARAM(scan << 16) | 1);
		};
		keyDown('H');
		keyDown('I');
		keyDown(VK_RETURN);
		for (int attempt = 0; attempt < 40 && g_sentCount.load() == 0; ++attempt) {
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
			QCoreApplication::processEvents();
		}
		const auto text = QString::fromUtf8(g_sentText.c_str());
		composed = (g_sentCount.load() == 1) && (text.size() == 2);
		setSendCallback(nullptr, nullptr);
	}

	int exit = 0;
	if (window) {
		PostMessageW(window, WM_CLOSE, 0, 0);
	}
	// The GPUI thread owns the window; give it time to exit before reporting.
	for (int attempt = 0; attempt < 40; ++attempt) {
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
		QCoreApplication::processEvents();
	}
	const auto clearedHandle = (exportedWindow() == 0);
	std::fprintf(stderr,
		"Qt ticks: %d; rejected: %d; queued: %d; populated: %d; revoked: %d; "
		"same window: %d; geometry: %d; hide/show: %d; null callback refused: %d; "
		"callback set: %d; composed: %d; handle cleared: %d; exit: %d\n",
		ticks, rejected ? 1 : 0, queued ? 1 : 0, populated ? 1 : 0, revoked ? 1 : 0,
		sameWindow ? 1 : 0, geometryOk ? 1 : 0, shown ? 1 : 0, nullRefused ? 1 : 0,
		callbackSet ? 1 : 0, composed ? 1 : 0, clearedHandle ? 1 : 0, exit);
	return (ticks > 0 && rejected && queued && populated && revoked
		&& sameWindow && geometryOk && shown && nullRefused && callbackSet
		&& composed && clearedHandle) ? 0 : 1;
}
