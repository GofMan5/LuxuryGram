# GPUI-CE production cutover

The acceptance target is the installed LuxuryGram client, not the `luxurygram-shell` preview. Keep the current client usable until its replacement passes the same scenarios. A build that still uses Qt for the engine is a migration build, not a Qt-free release.

## Integrating the live chat first

- Reuse the authenticated C++ model initially. The Qt `ChatWidget` already subscribes to [`Data::HistoryMessagesViewer`](../../Telegram/SourceFiles/data/data_history_messages.cpp#L467) in its [list source](../../Telegram/SourceFiles/history/view/history_view_chat_section.cpp#L5296). The viewer merges migrated history, reconciles pending IDs, reacts to index invalidation and requests missing pages through [`ApiWrap::requestHistory`](../../Telegram/SourceFiles/apiwrap.cpp#L3782). Do not create a parallel history RPC, database reader or protocol stack for the new renderer.
- Subscribe at the session boundary, not inside a Qt widget. Project owned values keyed by account and the full message ID (peer and message), and consume message-change notifications as well as slice changes: replacing text does not necessarily change a slice of IDs. Required regression cases: pagination with scroll anchor preserved, message edit/delete, pending ID replaced by server ID, and migrated history with equal numeric IDs from different peers.
- Clear the view and invalidate queued work on account/chat switch, passcode lock and logout. An account may remain alive after switching, so weak pointers alone do not reject stale deliveries. Do not extend the existing local automation socket to carry chat content. Validate with the [Test-session fixtures](../../Telegram/SourceFiles/test/README.md), without a real user profile or transcript in logs.

## Removing Qt, not just widgets

The [production target](../../Telegram/CMakeLists.txt#L82) links both Qt and Qt static plugins. Engine work remains even after stopping Qt painting:

| Boundary | Current production dependency to remove or replace |
| --- | --- |
| Startup and OS lifecycle | [`Sandbox`](../../Telegram/SourceFiles/core/sandbox.cpp#L117) owns `QApplication`, the event loop, the lock and local single-instance socket. Preserve single-instance and deep-link behavior. |
| Account and profile storage | [`Main::Account`](../../Telegram/SourceFiles/main/main_account.cpp#L295) and the [storage domain](../../Telegram/SourceFiles/storage/storage_domain.cpp#L133) use versioned Qt streams. Prove compatibility with disposable copies and retain encrypted, atomic writes and restart behavior. |
| Network and updates | [MTProto TCP](../../Telegram/SourceFiles/mtproto/details/mtproto_tcp_socket.h#L35), [WebSocket](../../Telegram/SourceFiles/mtproto/details/mtproto_websocket_socket.h#L59), [HTTP](../../Telegram/SourceFiles/mtproto/connection_http.h#L59), and the [updater checker](../../Telegram/SourceFiles/core/update_checker.cpp#L170) use Qt networking. Preserve proxy, reconnect, certificate and update verification behavior; see ADR-004 before introducing network/crypto dependencies. |
| Calls and media | [WebRTC bridge linkage](../../Telegram/lib_webrtc/CMakeLists.txt#L82) still pulls Qt. Validate devices, playback/capture, screen sharing and both call types. |
| Remaining UI | Replace dialogs, composer and search, rich messages and media, settings and authentication, calls, Premium/Stars/wallet and LuxuryGram-specific surfaces. Validate keyboard, screen reader, IME, localization, theming, notifications and platform integration. |

## Release evidence

On Windows with the CMake Visual Studio generator, run `python -B tools/tests/check_windows_qt_link_inputs.py out/Telegram/Telegram.vcxproj` from the repository root. Exit `1` means a recognized direct Qt input was found; exit `2` means the project could not be inspected; exit `0` clears only this preliminary check. The current Debug production project is a negative control: it reports `Qt5Cored.lib`, `Qt5Guid.lib`, `Qt5Widgetsd.lib` and the static-plugin inputs even before the executable is linked.

For each supported OS and architecture, verify the Debug production target and the actual packaged entry point, native helpers and libraries. Keep package hashes, build identity, compiler/linker inputs and linker maps. Inspect dynamic dependencies (including delay loads) and runtime module/child-process loads on a clean host. Dynamic imports alone cannot rule out Qt: the [native Qt link configuration](../../cmake/external/qt/CMakeLists.txt#L90) explicitly supplies static libraries. Require a known static Qt build to fail the check, and do not count a missing map, absent package member inventory or an untested platform as a pass.

Run end-to-end parity scenarios against disposable profiles: fresh and existing account login, lock/unlock and switch, chat read/search/send/edit/delete, attachments and calls, settings persistence, notifications, deep links, restart and update verification. The cutover requires these checks and a Qt-free installed package, not a successful preview launch.
