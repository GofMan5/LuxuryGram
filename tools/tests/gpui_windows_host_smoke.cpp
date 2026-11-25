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

// Mirrors DialogRowAbi in history_bridge.rs.
struct DialogRowAbi {
	std::uint64_t peer;
	std::uint32_t unread;
	std::uint8_t flags;
	const char *title;
	std::size_t title_len;
	const char *subtitle;
	std::size_t subtitle_len;
	const char *time;
	std::size_t time_len;
};
static_assert(sizeof(DialogRowAbi) == 64, "ABI layout must match Rust");

using Start = int (*)();
using Apply = int (*)(const HistoryRowAbi *, std::size_t, const HistoryRekeyAbi *, std::size_t);
using Clear = int (*)();
using Count = std::intptr_t (*)();
using ExportedWindow = std::intptr_t (*)();
using SendCallback = int (*)(void *, const char *, std::size_t);
using SetSendCallback = int (*)(SendCallback, void *);
using DialogsOpen = int (*)();
using DialogsApply = int (*)(const DialogRowAbi *, std::size_t);
using DialogsClose = int (*)();
using DialogOpenCallback = int (*)(void *, std::uint64_t);
using SetDialogOpenCallback = int (*)(DialogOpenCallback, void *);

std::atomic<int> g_sentCount{ 0 };
std::string g_sentText;
std::atomic<int> g_openedCount{ 0 };
std::atomic<std::uint64_t> g_openedPeer{ 0 };

int onComposerSubmit(void *, const char *text, std::size_t len) {
	g_sentText.assign(text, len);
	++g_sentCount;
	return 1;
}

int onDialogOpen(void *, std::uint64_t peer) {
	g_openedPeer.store(peer);
	++g_openedCount;
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
	const auto dialogsOpen = reinterpret_cast<DialogsOpen>(GetProcAddress(dll, "luxurygram_dialogs_open"));
	const auto dialogsApply = reinterpret_cast<DialogsApply>(GetProcAddress(dll, "luxurygram_dialogs_apply"));
	const auto dialogsClose = reinterpret_cast<DialogsClose>(GetProcAddress(dll, "luxurygram_dialogs_close"));
	const auto dialogsCount = reinterpret_cast<Count>(GetProcAddress(dll, "luxurygram_dialogs_count"));
	const auto dialogsWindow = reinterpret_cast<ExportedWindow>(GetProcAddress(dll, "luxurygram_dialogs_window"));
	const auto setDialogOpenCallback = reinterpret_cast<SetDialogOpenCallback>(GetProcAddress(dll, "luxurygram_dialogs_set_open_callback"));
	if (!start || !apply || !clear || !count || !exportedWindow || !setSendCallback
		|| !dialogsOpen || !dialogsApply || !dialogsClose || !dialogsCount
		|| !dialogsWindow || !setDialogOpenCallback) {
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
		// focused field, and the platform text input is installed during the
		// paint after that. Two forced redraws make both current before typing.
		std::this_thread::sleep_for(std::chrono::milliseconds(300));
		for (int i = 0; i < 2; ++i) {
			RedrawWindow(handle, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}
		// GPUI routes keys along its own focus path, and delivers typed text
		// through the platform text input: WM_KEYDOWN names the key and
		// WM_CHAR carries the character, as TranslateMessage produces after a
		// real key press. The host posts both asynchronously, as a keyboard
		// would: GPUI does not run its focus dispatch for a synchronous
		// cross-thread SendMessage. Two letter keys are sent, so the text the
		// callback receives has two characters in the host's current keyboard
		// layout; the exact letters are layout-dependent.
		const auto scanOf = [&](WPARAM vk) {
			return MapVirtualKeyW(UINT(vk), MAPVK_VK_TO_VSC);
		};
		const auto keyDown = [&](WPARAM vk) {
			PostMessageW(handle, WM_KEYDOWN, vk, LPARAM(scanOf(vk) << 16) | 1);
		};
		const auto charOf = [&](WPARAM vk) {
			// ToUnicode with no keyboard state gives the unshifted US-layout
			// letter for these virtual keys, which is what the test types.
			const UINT vkCode = UINT(vk);
			std::wstring buffer(8, L'\0');
			const int written = ToUnicode(vkCode, scanOf(vk), nullptr, buffer.data(), 8, 0x4);
			const auto count = written > 0 ? written : 0;
			for (int i = 0; i < count; ++i) {
				PostMessageW(handle, WM_CHAR, WPARAM(buffer[i]), 0);
			}
		};
		const auto pause = [] {
			std::this_thread::sleep_for(std::chrono::milliseconds(150));
		};
		keyDown('H');
		charOf('H');
		pause();
		keyDown('I');
		charOf('I');
		pause();
		keyDown(VK_RETURN);
		for (int attempt = 0; attempt < 40 && g_sentCount.load() == 0; ++attempt) {
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
			QCoreApplication::processEvents();
		}
		const auto text = QString::fromUtf8(g_sentText.c_str());
		composed = (g_sentCount.load() == 1) && (text.size() == 2);
		setSendCallback(nullptr, nullptr);
	}

	// Chat list: open it, apply two rows, check its own window appears next
	// to the feed's, then open the first row with Enter and see the callback
	// receive that row's peer id. Closing it clears the handle and the rows.
	constexpr char title1[] = "Design team";
	constexpr char subtitle1[] = "See you tomorrow";
	constexpr char time1[] = "09:30";
	const DialogRowAbi dialogs[] = {
		{ 1001, 3, 0x1, title1, sizeof(title1) - 1, subtitle1, sizeof(subtitle1) - 1, time1, sizeof(time1) - 1 },
		{ 1002, 0, 0x2, "Alice", 5, "photo", 5, time1, sizeof(time1) - 1 },
	};
	const auto nullOpenRefused = (setDialogOpenCallback(nullptr, nullptr) == 1);
	const auto openCallbackSet = (setDialogOpenCallback(&onDialogOpen, nullptr) == 0);
	const auto dialogsQueued = (dialogsOpen() == 0) && (dialogsApply(dialogs, 2) == 0);
	std::intptr_t dialogsCountValue = 0;
	HWND dialogsWnd = nullptr;
	for (int attempt = 0; attempt < 40; ++attempt) {
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
		QCoreApplication::processEvents();
		dialogsCountValue = dialogsCount();
		dialogsWnd = reinterpret_cast<HWND>(dialogsWindow());
		if (dialogsCountValue == 2 && dialogsWnd != nullptr) break;
	}
	const auto separateWindow = (dialogsWnd != nullptr) && (dialogsWnd != handle);
	bool dialogsOpened = false;
	if (separateWindow) {
		// The list window takes focus when it opens; two forced redraws make
		// its focused frame current before the key, as for the composer.
		std::this_thread::sleep_for(std::chrono::milliseconds(300));
		for (int i = 0; i < 2; ++i) {
			RedrawWindow(dialogsWnd, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}
		PostMessageW(dialogsWnd, WM_KEYDOWN, VK_RETURN, LPARAM(MapVirtualKeyW(VK_RETURN, MAPVK_VK_TO_VSC) << 16) | 1);
		for (int attempt = 0; attempt < 40 && g_openedCount.load() == 0; ++attempt) {
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
			QCoreApplication::processEvents();
		}
		dialogsOpened = (g_openedCount.load() == 1) && (g_openedPeer.load() == 1001);
	}
	const auto dialogsCleared = (dialogsClose() == 0) && waitFor(dialogsCount, 0, nullptr)
		&& (dialogsWindow() == 0);

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
		"callback set: %d; composed: %d; null open refused: %d; open callback set: %d; "
		"dialogs queued: %d; dialogs listed: %d; separate window: %d; dialogs opened: %d; "
		"dialogs cleared: %d; handle cleared: %d; exit: %d\n",
		ticks, rejected ? 1 : 0, queued ? 1 : 0, populated ? 1 : 0, revoked ? 1 : 0,
		sameWindow ? 1 : 0, geometryOk ? 1 : 0, shown ? 1 : 0, nullRefused ? 1 : 0,
		callbackSet ? 1 : 0, composed ? 1 : 0, nullOpenRefused ? 1 : 0,
		openCallbackSet ? 1 : 0, dialogsQueued ? 1 : 0,
		dialogsCountValue == 2 ? 1 : 0, separateWindow ? 1 : 0, dialogsOpened ? 1 : 0,
		dialogsCleared ? 1 : 0, clearedHandle ? 1 : 0, exit);
	return (ticks > 0 && rejected && queued && populated && revoked
		&& sameWindow && geometryOk && shown && nullRefused && callbackSet
		&& composed && nullOpenRefused && openCallbackSet && dialogsQueued
		&& dialogsCountValue == 2 && separateWindow && dialogsOpened && dialogsCleared
		&& clearedHandle) ? 0 : 1;
}
