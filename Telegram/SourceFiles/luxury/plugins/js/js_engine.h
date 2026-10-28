// This is the source code of LuxuryGram for Desktop.
// See LEGAL for license and copyright information.
#pragma once

#include "luxury/libs/json.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace Luxury::Plugins::Js {

struct Limits {
	std::size_t memory = std::size_t(64) * 1024 * 1024;
	std::size_t stack = std::size_t(1024) * 1024;
	std::int64_t sliceMs = 250;
};

enum class LogLevel {
	Info,
	Warn,
	Error,
};

// What a host function answers. Pending means the host settles the promise
// later with Engine::resolve / Engine::reject and CallContext::promiseId.
struct HostResult {
	enum class Kind {
		Value,
		Error,
		Pending,
	};
	Kind kind = Kind::Value;
	nlohmann::json value;
	std::string errorName;
	std::string errorMessage;

	[[nodiscard]] static HostResult Value(nlohmann::json value);
	[[nodiscard]] static HostResult Error(
		std::string name,
		std::string message);
	[[nodiscard]] static HostResult Pending();
};

struct CallContext {
	std::uint64_t promiseId = 0;
};

// Everything the engine needs from the client. Called from inside a JS
// entry: a Host must not destroy the Engine or call into it from these
// callbacks (such calls are refused with an error Outcome, except
// releaseFunction). An exception thrown by call or readModule becomes a
// JS InternalError.
class Host {
public:
	virtual ~Host() = default;

	// args is the JSON array of the JS arguments; a function inside it
	// arrives as {"$fn": id} and stays callable until releaseFunction(id).
	// Names: every lux.* API by its dotted path ("messages.send",
	// "ui.addCommand", "storage.get", "api.call", ...), plus
	// "plugin.info" for lux.plugin, "events.subscribe" /
	// "events.unsubscribe" ([event]) once per lux.on and per unsubscribe,
	// "timer.set" ([{delay, repeat}] -> timer id) and "timer.clear" ([id]).
	// "http.fetch" answers {status, headers, body}.
	[[nodiscard]] virtual HostResult call(
		std::string_view function,
		nlohmann::json args,
		const CallContext &context) = 0;
	virtual void log(LogLevel level, std::string_view text) noexcept = 0;
	// Monotonic milliseconds.
	[[nodiscard]] virtual std::int64_t now() noexcept = 0;
	// path is normalized and relative to the plugin code directory.
	[[nodiscard]] virtual std::optional<std::string> readModule(
		std::string_view path) = 0;

};

// Result of one entry into JS. error is the exception text with its stack
// (file:line:column); overrun is set when the time slice was exceeded.
struct Outcome {
	nlohmann::json value;
	std::string error;
	bool overrun = false;

	[[nodiscard]] bool ok() const {
		return error.empty();
	}
};

// Resolves an import specifier against the importing module. Imports take
// "./" and "../" specifiers only; the main module (empty base) takes a
// plain relative path. Anything leaving the plugin root is refused.
[[nodiscard]] std::optional<std::string> ResolveModulePath(
	std::string_view base,
	std::string_view name);

// One QuickJS runtime and context per plugin, single threaded. Handler
// errors inside emit are logged and do not stop the other handlers.
class Engine {
public:
	Engine(Limits limits, Host &host);
	~Engine();

	Engine(const Engine &) = delete;
	Engine &operator=(const Engine &) = delete;

	[[nodiscard]] Outcome loadMain(std::string_view path);
	// beforeSend answers the rewritten text, false to cancel, or null when
	// unchanged; other events answer null.
	[[nodiscard]] Outcome emit(
		std::string_view event,
		const nlohmann::json &payload);
	// args is passed as the single argument; null calls without arguments.
	[[nodiscard]] Outcome callFunction(
		std::int64_t id,
		const nlohmann::json &args);
	void releaseFunction(std::int64_t id);
	[[nodiscard]] Outcome fireTimer(std::int64_t id);
	[[nodiscard]] Outcome resolve(
		std::uint64_t promiseId,
		const nlohmann::json &value);
	[[nodiscard]] Outcome reject(
		std::uint64_t promiseId,
		std::string_view name,
		std::string_view message);
	[[nodiscard]] Outcome runJobs();

	// Entries in a row that exceeded the time slice; an entry that
	// finishes in time resets it.
	[[nodiscard]] int overrunCount() const;

private:
	struct State;
	std::unique_ptr<State> _state;

};

} // namespace Luxury::Plugins::Js
