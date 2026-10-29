// This is the source code of LuxuryGram for Desktop.
// See LEGAL for license and copyright information.
#include "luxury/plugins/js/js_engine.h"

#include "luxury/libs/quickjs/quickjs.h"

#include <array>
#include <cstring>
#include <exception>
#include <map>
#include <utility>
#include <vector>

namespace Luxury::Plugins::Js {
namespace {

// Unhandled rejections logged one by one after an engine entry.
constexpr auto kMaxRejectionReports = std::size_t(16);

// Runs once per context. Receives the two natives and returns the entry
// points the engine calls; nothing but lux, console and the timer
// functions is left on the global object.
constexpr auto kPrelude = R"js((function (host, log) {
'use strict';
const fns = new Map();
const handlers = new Map();
const timers = new Map();
let nextFn = 1;
let info;
const register = (fn) => {
	const id = nextFn++;
	fns.set(id, fn);
	return id;
};
// Binary data crosses the JSON bridge as { $bin: base64 }.
const bytesOf = (item) => (item instanceof ArrayBuffer)
	? new Uint8Array(item)
	: ArrayBuffer.isView(item)
	? new Uint8Array(item.buffer, item.byteOffset, item.byteLength)
	: null;
const encode = (value) => JSON.stringify(value, (key, item) => {
	if (typeof item === 'function') {
		return { $fn: register(item) };
	}
	const bytes = bytesOf(item);
	return bytes ? { $bin: bytes.toBase64() } : item;
});
const decodeBin = (r) => Uint8Array.fromBase64(r.$bin).buffer;
const call = (name, ...args) => host(name, encode(args));
const errorText = (error) => {
	const stack = (typeof error.stack === 'string')
		? error.stack.replace(/\s+$/, '')
		: '';
	return stack ? String(error) + '\n' + stack : String(error);
};
const show = (value) => {
	try {
		if (typeof value === 'string') {
			return value;
		} else if (value instanceof Error) {
			return errorText(value);
		}
		const json = JSON.stringify(value);
		return (json === undefined) ? String(value) : json;
	} catch (e) {
		try {
			return String(value);
		} catch (e2) {
			return '[unprintable]';
		}
	}
};
const print = (level) => (...args) => log(level, args.map(show).join(' '));
const report = (error) => log(2, 'Error in event handler: ' + show(error));
const schedule = (repeat) => (fn, delay, ...args) => {
	if (typeof fn !== 'function') {
		throw new TypeError('timer callback must be a function');
	}
	const id = call('timer.set', {
		delay: Math.max(0, Number(delay) || 0),
		repeat,
	});
	timers.set(id, { fn, args, repeat });
	return id;
};
const cancel = (id) => {
	if (timers.delete(id)) {
		call('timer.clear', id);
	}
};
const lux = {
	get plugin() {
		if (info === undefined) {
			info = Object.freeze(call('plugin.info'));
		}
		return info;
	},
	log: print(0),
	on(event, fn) {
		if (typeof fn !== 'function') {
			throw new TypeError('lux.on: handler must be a function');
		}
		const name = String(event);
		call('events.subscribe', name);
		let list = handlers.get(name);
		if (!list) {
			handlers.set(name, list = []);
		}
		const entry = { fn };
		list.push(entry);
		return () => {
			const index = list.indexOf(entry);
			if (index >= 0) {
				list.splice(index, 1);
				call('events.unsubscribe', name);
			}
		};
	},
	messages: {
		send: (options) => call('messages.send', options),
		get: (peer, id) => call('messages.get', peer, id),
		history: (peer, options) => call('messages.history', peer, options),
		edit: (peer, id, options) => call('messages.edit', peer, id, options),
		remove: (peer, ids, options) => call('messages.delete', peer, ids, options),
		forward: (peer, ids, to) => call('messages.forward', peer, ids, to),
		markRead: (peer, maxId) => call('messages.markRead', peer, maxId),
		sendFile: (peer, file, options) => call('messages.sendFile', peer, file, options),
		downloadMedia(peer, id) {
			return Promise.resolve(call('messages.downloadMedia', peer, id)).then(decodeBin);
		},
	},
	chats: {
		get: (peer) => call('chats.get', peer),
		list: (options) => call('chats.list', options),
	},
	account: {
		me: () => call('account.me'),
	},
	ui: {
		toast: (text) => call('ui.toast', text),
		notify: (text) => call('ui.notify', text),
		alert: (options) => call('ui.alert', options),
		confirm: (options) => call('ui.confirm', options),
		prompt: (options) => call('ui.prompt', options),
		addCommand: (options) => call('ui.addCommand', options),
		addMessageMenuItem: (options) => call('ui.addMessageMenuItem', options),
		addHotkey: (options) => call('ui.addHotkey', options),
	},
	http: {
		fetch(url, options) {
			return Promise.resolve(call('http.fetch', url, options)).then((r) => ({
				status: r.status,
				headers: r.headers,
				text: () => r.body,
				json: () => JSON.parse(r.body),
				// Set instead of body with { binary: true }.
				base64: r.base64,
			}));
		},
	},
	storage: {
		get: (key) => call('storage.get', key),
		set: (key, value) => call('storage.set', key, value),
		remove: (key) => call('storage.remove', key),
		keys: () => call('storage.keys'),
	},
	settings: {
		get: (key) => call('settings.get', key),
		set: (key, value) => call('settings.set', key, value),
		onChange: (fn) => call('settings.onChange', fn),
	},
	clipboard: {
		read: () => call('clipboard.read'),
		write: (text) => call('clipboard.write', text),
	},
	api: {
		call: (method, params) => call('api.call', method, params),
	},
};
globalThis.lux = lux;
globalThis.console = {
	log: print(0),
	info: print(0),
	debug: print(0),
	warn: print(1),
	error: print(2),
};
globalThis.setTimeout = schedule(false);
globalThis.setInterval = schedule(true);
globalThis.clearTimeout = cancel;
globalThis.clearInterval = cancel;
return {
	emit(event, payload) {
		const list = handlers.get(event);
		if (!list || !list.length) {
			return null;
		}
		const entries = list.slice();
		if (event === 'beforeSend') {
			// Each handler sees what the previous ones made of the message.
			// A plain text change stays a string; the answer becomes an
			// object only to carry what a handler changed, so a reply-only
			// handler does not round-trip the entities through C++.
			let { text, entities, replyTo } = payload;
			let changed = false;
			let retagged = false;
			let replied = false;
			for (const { fn } of entries) {
				let result;
				try {
					result = fn({ ...payload, text, entities, replyTo });
				} catch (e) {
					report(e);
					continue;
				}
				if (result === false) {
					return false;
				} else if (result && typeof result.then === 'function') {
					log(1, 'beforeSend handlers run synchronously:'
						+ ' the returned promise is ignored');
					result.then(undefined, report);
					continue;
				}
				const object = (result !== null && typeof result === 'object');
				const next = object ? result.text : result;
				if (typeof next === 'string' && next !== text) {
					text = next;
					entities = [];
					changed = true;
				}
				if (object && Array.isArray(result.entities)) {
					entities = result.entities;
					changed = retagged = true;
				}
				if (object && ('replyTo' in result)) {
					replyTo = (result.replyTo === undefined) ? null : result.replyTo;
					changed = replied = true;
				}
			}
			if (!changed) {
				return null;
			} else if (!retagged && !replied) {
				return text;
			}
			const answer = { text };
			if (retagged) {
				answer.entities = entities;
			}
			if (replied) {
				answer.replyTo = replyTo;
			}
			return answer;
		}
		for (const { fn } of entries) {
			try {
				const result = (payload === undefined) ? fn() : fn(payload);
				if (result && typeof result.then === 'function') {
					result.then(undefined, report);
				}
			} catch (e) {
				report(e);
			}
		}
		return null;
	},
	call(id, args) {
		const fn = fns.get(id);
		if (!fn) {
			throw new ReferenceError('callback ' + id + ' was released');
		}
		return (args === undefined) ? fn() : fn(args);
	},
	release(id) {
		fns.delete(id);
	},
	fire(id) {
		const timer = timers.get(id);
		if (!timer) {
			return;
		}
		if (!timer.repeat) {
			timers.delete(id);
		}
		timer.fn(...timer.args);
	},
	encode,
};
}))js";

enum Internal {
	kEmit,
	kCall,
	kRelease,
	kFire,
	kEncode,
	kInternalCount,
};

constexpr auto kInternalNames = std::array<const char*, kInternalCount>{
	"emit",
	"call",
	"release",
	"fire",
	"encode",
};

[[nodiscard]] std::string Dump(const nlohmann::json &value) {
	return value.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

[[nodiscard]] std::optional<std::string> String(
		JSContext *ctx,
		JSValueConst value) {
	auto size = std::size_t();
	const auto data = JS_ToCStringLen(ctx, &size, value);
	if (!data) {
		return std::nullopt;
	}
	auto result = std::string(data, size);
	JS_FreeCString(ctx, data);
	return result;
}

void DropException(JSContext *ctx) {
	JS_FreeValue(ctx, JS_GetException(ctx));
}

} // namespace

HostResult HostResult::Value(nlohmann::json value) {
	auto result = HostResult();
	result.value = std::move(value);
	return result;
}

HostResult HostResult::Error(std::string name, std::string message) {
	auto result = HostResult();
	result.kind = Kind::Error;
	result.errorName = std::move(name);
	result.errorMessage = std::move(message);
	return result;
}

HostResult HostResult::Pending() {
	auto result = HostResult();
	result.kind = Kind::Pending;
	return result;
}

std::optional<std::string> ResolveModulePath(
		std::string_view base,
		std::string_view name) {
	constexpr auto kForbidden = std::string_view("\\:\0", 3);
	if (name.empty()
		|| name.front() == '/'
		|| name.find_first_of(kForbidden) != std::string_view::npos) {
		return std::nullopt;
	}
	const auto relative = name.starts_with("./") || name.starts_with("../");
	if (!base.empty() && !relative) {
		return std::nullopt;
	}
	auto segments = std::vector<std::string_view>();
	const auto apply = [&](std::string_view path) {
		while (true) {
			const auto slash = path.find('/');
			const auto segment = path.substr(0, slash);
			if (segment.empty()) {
				return false;
			} else if (segment == "..") {
				if (segments.empty()) {
					return false;
				}
				segments.pop_back();
			} else if (segment != ".") {
				segments.push_back(segment);
			}
			if (slash == std::string_view::npos) {
				return true;
			}
			path = path.substr(slash + 1);
		}
	};
	if (const auto slash = base.rfind('/'); slash != std::string_view::npos) {
		if (!apply(base.substr(0, slash))) {
			return std::nullopt;
		}
	}
	if (!apply(name) || segments.empty()) {
		return std::nullopt;
	}
	auto result = std::string();
	for (const auto segment : segments) {
		if (!result.empty()) {
			result += '/';
		}
		result += segment;
	}
	return result;
}

struct Engine::State {
	struct Rejection {
		JSValue promise;
		JSValue reason;
	};

	State(Limits limits, Host &host);
	~State();

	[[nodiscard]] bool usable() const {
		return ctx && startError.empty();
	}

	template <typename Body>
	[[nodiscard]] Outcome enter(Body &&body);
	[[nodiscard]] JSValue install();
	[[nodiscard]] JSValue compile(const std::string &path);
	[[nodiscard]] JSValue fromJson(const nlohmann::json &value);
	[[nodiscard]] JSValue makeError(
		std::string_view name,
		std::string_view message);
	[[nodiscard]] JSValue callInternal(
		Internal which,
		std::vector<JSValue> args);
	[[nodiscard]] std::string describe(JSValueConst value);
	[[nodiscard]] std::string takeException();
	[[nodiscard]] Outcome settle(
		std::uint64_t promiseId,
		bool fulfill,
		const nlohmann::json &value,
		std::string_view name,
		std::string_view message);
	void pump(Outcome &out);
	void finish(Outcome &out, JSValue result);
	void settleMain(Outcome &out);
	void forget(JSValueConst promise);
	void reportRejections();
	[[nodiscard]] JSValue nativeCall(JSValueConst name, JSValueConst args);

	static int Interrupt(JSRuntime *rt, void *opaque);
	static char *Normalize(
		JSContext *ctx,
		const char *base,
		const char *name,
		void *opaque);
	static JSModuleDef *Load(JSContext *ctx, const char *name, void *opaque);
	static void Track(
		JSContext *ctx,
		JSValueConst promise,
		JSValueConst reason,
		bool handled,
		void *opaque);
	static JSValue Native(
		JSContext *ctx,
		JSValueConst,
		int,
		JSValueConst *argv,
		int magic);

	Limits limits;
	Host &host;
	JSRuntime *rt = nullptr;
	JSContext *ctx = nullptr;
	std::string startError;
	std::array<JSValue, kInternalCount> internals;
	std::map<std::uint64_t, std::array<JSValue, 2>> pending;
	std::vector<Rejection> rejections;
	std::uint64_t nextPromiseId = 0;
	std::int64_t deadline = 0;
	int depth = 0;
	int overruns = 0;
	bool overrun = false;
	bool loaded = false;
	bool loading = false;

	// The main module's promise while its top-level await is pending: a
	// later rejection becomes the error of the entry that settles it.
	JSValue main = JS_UNDEFINED;
};

Engine::State::State(Limits limits, Host &host)
: limits(limits)
, host(host) {
	internals.fill(JS_UNDEFINED);
	rt = JS_NewRuntime();
	if (!rt) {
		startError = "could not create the JavaScript runtime";
		return;
	}
	JS_SetMemoryLimit(rt, limits.memory);
	JS_SetMaxStackSize(rt, limits.stack);
	JS_SetInterruptHandler(rt, Interrupt, this);
	JS_SetModuleLoaderFunc(rt, Normalize, Load, this);
	JS_SetHostPromiseRejectionTracker(rt, Track, this);
	ctx = JS_NewContext(rt);
	if (!ctx) {
		startError = "could not create the JavaScript context";
		return;
	}
	JS_SetContextOpaque(ctx, this);
	const auto out = enter([&] { return install(); });
	if (!out.ok()) {
		startError = "could not start the plugin runtime: " + out.error;
	}
}

Engine::State::~State() {
	if (ctx) {
		JS_SetHostPromiseRejectionTracker(rt, nullptr, nullptr);
		JS_FreeValue(ctx, main);
		for (const auto &value : internals) {
			JS_FreeValue(ctx, value);
		}
		for (const auto &[id, funcs] : pending) {
			JS_FreeValue(ctx, funcs[0]);
			JS_FreeValue(ctx, funcs[1]);
		}
		for (const auto &rejection : rejections) {
			JS_FreeValue(ctx, rejection.promise);
			JS_FreeValue(ctx, rejection.reason);
		}
		JS_FreeContext(ctx);
	}
	if (rt) {
		JS_FreeRuntime(rt);
	}
}

template <typename Body>
Outcome Engine::State::enter(Body &&body) {
	auto out = Outcome();
	if (!usable()) {
		out.error = startError;
		return out;
	} else if (depth > 0) {
		out.error = "the plugin engine is already running";
		return out;
	}
	++depth;
	overrun = false;
	JS_UpdateStackTop(rt);
	deadline = host.now() + limits.sliceMs;
	const auto result = body();
	if (JS_IsException(result)) {
		out.error = takeException();
	} else {
		pump(out);
		if (overrun) {
			JS_FreeValue(ctx, result);
		} else {
			finish(out, result);
		}
	}
	loading = false;
	if (!overrun) {
		settleMain(out);
	}
	reportRejections();
	--depth;
	out.overrun = overrun;
	overruns = overrun ? (overruns + 1) : 0;
	return out;
}

JSValue Engine::State::install() {
	const auto factory = JS_Eval(
		ctx,
		kPrelude,
		std::strlen(kPrelude),
		"<lux>",
		JS_EVAL_TYPE_GLOBAL | JS_EVAL_FLAG_STRICT);
	if (JS_IsException(factory)) {
		return factory;
	}
	auto natives = std::array<JSValue, 2>{
		JS_NewCFunctionMagic(ctx, Native, "host", 2, JS_CFUNC_generic_magic, 0),
		JS_NewCFunctionMagic(ctx, Native, "log", 2, JS_CFUNC_generic_magic, 1),
	};
	auto object = JS_EXCEPTION;
	if (!JS_IsException(natives[0]) && !JS_IsException(natives[1])) {
		object = JS_Call(ctx, factory, JS_UNDEFINED, 2, natives.data());
	}
	JS_FreeValue(ctx, factory);
	JS_FreeValue(ctx, natives[0]);
	JS_FreeValue(ctx, natives[1]);
	if (JS_IsException(object)) {
		return object;
	}
	for (auto i = 0; i != kInternalCount; ++i) {
		internals[i] = JS_GetPropertyStr(ctx, object, kInternalNames[i]);
		if (JS_IsException(internals[i])) {
			internals[i] = JS_UNDEFINED;
			JS_FreeValue(ctx, object);
			return JS_EXCEPTION;
		}
	}
	JS_FreeValue(ctx, object);
	return JS_UNDEFINED;
}

JSValue Engine::State::compile(const std::string &path) {
	auto source = std::optional<std::string>();
	try {
		source = host.readModule(path);
	} catch (const std::exception &e) {
		return JS_ThrowInternalError(ctx, "%s", e.what());
	} catch (...) {
		return JS_ThrowInternalError(ctx, "%s", "module read failed");
	}
	if (!source) {
		return JS_ThrowReferenceError(
			ctx,
			"cannot load module \"%s\"",
			path.c_str());
	}
	return JS_Eval(
		ctx,
		source->c_str(),
		source->size(),
		path.c_str(),
		JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
}

JSValue Engine::State::fromJson(const nlohmann::json &value) {
	const auto text = Dump(value);
	return JS_ParseJSON(ctx, text.c_str(), text.size(), "<host>");
}

JSValue Engine::State::makeError(
		std::string_view name,
		std::string_view message) {
	const auto error = JS_NewError(ctx);
	if (JS_IsException(error)) {
		return error;
	}
	const auto define = [&](const char *key, std::string_view text) {
		const auto value = JS_NewStringLen(ctx, text.data(), text.size());
		return !JS_IsException(value)
			&& JS_DefinePropertyValueStr(
				ctx,
				error,
				key,
				value,
				JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE) >= 0;
	};
	if (!define("message", message)
		|| !define("name", name.empty() ? std::string_view("Error") : name)) {
		JS_FreeValue(ctx, error);
		return JS_EXCEPTION;
	}
	return error;
}

JSValue Engine::State::callInternal(Internal which, std::vector<JSValue> args) {
	for (const auto &arg : args) {
		if (JS_IsException(arg)) {
			for (const auto &other : args) {
				JS_FreeValue(ctx, other);
			}
			return JS_EXCEPTION;
		}
	}
	const auto result = JS_Call(
		ctx,
		internals[which],
		JS_UNDEFINED,
		int(args.size()),
		args.data());
	for (const auto &arg : args) {
		JS_FreeValue(ctx, arg);
	}
	return result;
}

std::string Engine::State::describe(JSValueConst value) {
	auto text = String(ctx, value);
	if (!text) {
		DropException(ctx);
		text = "[unprintable exception]";
	}
	if (JS_IsError(value)) {
		const auto stack = JS_GetPropertyStr(ctx, value, "stack");
		if (JS_IsException(stack)) {
			DropException(ctx);
		} else if (JS_IsString(stack)) {
			if (auto frames = String(ctx, stack)) {
				while (!frames->empty() && (frames->back() == '\n'
					|| frames->back() == ' ')) {
					frames->pop_back();
				}
				if (!frames->empty()) {
					*text += '\n' + *frames;
				}
			} else {
				DropException(ctx);
			}
		}
		JS_FreeValue(ctx, stack);
	}
	return *text;
}

std::string Engine::State::takeException() {
	const auto exception = JS_GetException(ctx);
	if (JS_IsUninitialized(exception)) {
		return "unknown error";
	}
	auto result = describe(exception);
	JS_FreeValue(ctx, exception);
	return result;
}

void Engine::State::pump(Outcome &out) {
	while (!overrun && JS_IsJobPending(rt)) {
		auto job = static_cast<JSContext*>(nullptr);
		if (JS_ExecutePendingJob(rt, &job) >= 0) {
			continue;
		}
		auto text = takeException();
		if (out.error.empty() || overrun) {
			out.error = std::move(text);
		} else {
			host.log(LogLevel::Error, text);
		}
	}
}

void Engine::State::finish(Outcome &out, JSValue result) {
	if (JS_IsPromise(result)) {
		const auto state = JS_PromiseState(ctx, result);
		if (state == JS_PROMISE_PENDING) {
			if (loading) {
				main = result;
			} else {
				JS_FreeValue(ctx, result);
			}
			return;
		}
		const auto settled = JS_PromiseResult(ctx, result);
		if (state == JS_PROMISE_REJECTED) {
			forget(result);
			out.error = describe(settled);
			JS_FreeValue(ctx, settled);
			JS_FreeValue(ctx, result);
			return;
		}
		JS_FreeValue(ctx, result);
		result = settled;
	}
	if (JS_IsUndefined(result)) {
		return;
	}
	const auto text = callInternal(kEncode, { result });
	if (JS_IsException(text)) {
		out.error = takeException();
		return;
	}
	if (JS_IsString(text)) {
		if (const auto json = String(ctx, text)) {
			out.value = nlohmann::json::parse(*json, nullptr, false);
			if (out.value.is_discarded()) {
				out.value = nullptr;
				out.error = "the result is not valid JSON";
			}
		} else {
			out.error = takeException();
		}
	}
	JS_FreeValue(ctx, text);
}

void Engine::State::settleMain(Outcome &out) {
	if (JS_IsUndefined(main)
		|| JS_PromiseState(ctx, main) == JS_PROMISE_PENDING) {
		return;
	} else if (JS_PromiseState(ctx, main) == JS_PROMISE_REJECTED) {
		forget(main);
		const auto reason = JS_PromiseResult(ctx, main);
		auto text = describe(reason);
		JS_FreeValue(ctx, reason);
		if (out.error.empty()) {
			out.error = std::move(text);
		} else {
			host.log(LogLevel::Error, text);
		}
	}
	JS_FreeValue(ctx, main);
	main = JS_UNDEFINED;
}

void Engine::State::forget(JSValueConst promise) {
	for (auto i = rejections.begin(); i != rejections.end(); ++i) {
		if (JS_VALUE_GET_PTR(i->promise) == JS_VALUE_GET_PTR(promise)) {
			JS_FreeValue(ctx, i->promise);
			JS_FreeValue(ctx, i->reason);
			rejections.erase(i);
			return;
		}
	}
}

void Engine::State::reportRejections() {
	auto list = std::move(rejections);
	rejections.clear();
	auto reported = std::size_t();
	for (const auto &rejection : list) {
		if (reported++ < kMaxRejectionReports) {
			host.log(
				LogLevel::Error,
				"Unhandled promise rejection: " + describe(rejection.reason));
		}
		JS_FreeValue(ctx, rejection.promise);
		JS_FreeValue(ctx, rejection.reason);
	}
	if (list.size() > kMaxRejectionReports) {
		host.log(
			LogLevel::Error,
			"...and "
				+ std::to_string(list.size() - kMaxRejectionReports)
				+ " more unhandled promise rejections");
	}
}

Outcome Engine::State::settle(
		std::uint64_t promiseId,
		bool fulfill,
		const nlohmann::json &value,
		std::string_view name,
		std::string_view message) {
	return enter([&] {
		const auto i = pending.find(promiseId);
		if (i == pending.end()) {
			const auto text = "unknown promise " + std::to_string(promiseId);
			return JS_ThrowReferenceError(ctx, "%s", text.c_str());
		}
		const auto funcs = i->second;
		pending.erase(i);
		auto arg = fulfill ? fromJson(value) : makeError(name, message);
		auto result = JS_EXCEPTION;
		if (!JS_IsException(arg)) {
			result = JS_Call(ctx, funcs[fulfill ? 0 : 1], JS_UNDEFINED, 1, &arg);
			JS_FreeValue(ctx, arg);
		}
		JS_FreeValue(ctx, funcs[0]);
		JS_FreeValue(ctx, funcs[1]);
		if (JS_IsException(result)) {
			return result;
		}
		JS_FreeValue(ctx, result);
		return JS_UNDEFINED;
	});
}

JSValue Engine::State::nativeCall(JSValueConst nameValue, JSValueConst argsValue) {
	const auto name = String(ctx, nameValue);
	if (!name) {
		return JS_EXCEPTION;
	}
	const auto text = String(ctx, argsValue);
	if (!text) {
		return JS_EXCEPTION;
	}
	auto args = nlohmann::json::parse(*text, nullptr, false);
	if (args.is_discarded()) {
		return JS_ThrowTypeError(ctx, "%s", "arguments are not valid JSON");
	}
	const auto promiseId = ++nextPromiseId;
	auto result = HostResult();
	try {
		result = host.call(*name, std::move(args), CallContext{ promiseId });
	} catch (const std::exception &e) {
		return JS_ThrowInternalError(ctx, "%s", e.what());
	} catch (...) {
		return JS_ThrowInternalError(ctx, "%s", "host call failed");
	}
	if (result.kind == HostResult::Kind::Value) {
		return fromJson(result.value);
	} else if (result.kind == HostResult::Kind::Error) {
		const auto error = makeError(result.errorName, result.errorMessage);
		return JS_IsException(error) ? error : JS_Throw(ctx, error);
	}
	auto funcs = std::array<JSValue, 2>{ JS_UNDEFINED, JS_UNDEFINED };
	const auto promise = JS_NewPromiseCapability(ctx, funcs.data());
	if (!JS_IsException(promise)) {
		pending.emplace(promiseId, funcs);
	}
	return promise;
}

int Engine::State::Interrupt(JSRuntime *, void *opaque) {
	auto &state = *static_cast<State*>(opaque);
	if (state.depth == 0) {
		return 0;
	} else if (!state.overrun && state.host.now() <= state.deadline) {
		return 0;
	}
	state.overrun = true;
	return 1;
}

char *Engine::State::Normalize(
		JSContext *ctx,
		const char *base,
		const char *name,
		void *) {
	const auto resolved = ResolveModulePath(base, name);
	if (!resolved) {
		JS_ThrowReferenceError(
			ctx,
			"cannot import \"%s\" from \"%s\": only relative paths"
			" inside the plugin are allowed",
			name,
			base);
		return nullptr;
	}
	return js_strdup(ctx, resolved->c_str());
}

JSModuleDef *Engine::State::Load(
		JSContext *ctx,
		const char *name,
		void *opaque) {
	auto &state = *static_cast<State*>(opaque);
	const auto compiled = state.compile(name);
	if (JS_IsException(compiled)) {
		return nullptr;
	}
	// The module is referenced by the context, the value is not needed.
	const auto module = static_cast<JSModuleDef*>(JS_VALUE_GET_PTR(compiled));
	JS_FreeValue(ctx, compiled);
	return module;
}

void Engine::State::Track(
		JSContext *ctx,
		JSValueConst promise,
		JSValueConst reason,
		bool handled,
		void *opaque) {
	auto &state = *static_cast<State*>(opaque);
	if (handled) {
		state.forget(promise);
	} else {
		state.rejections.push_back({
			JS_DupValue(ctx, promise),
			JS_DupValue(ctx, reason),
		});
	}
}

JSValue Engine::State::Native(
		JSContext *ctx,
		JSValueConst,
		int,
		JSValueConst *argv,
		int magic) {
	auto &state = *static_cast<State*>(JS_GetContextOpaque(ctx));
	if (magic == 0) {
		return state.nativeCall(argv[0], argv[1]);
	}
	auto level = 0;
	if (JS_ToInt32(ctx, &level, argv[0]) < 0) {
		return JS_EXCEPTION;
	}
	const auto text = String(ctx, argv[1]);
	if (!text) {
		return JS_EXCEPTION;
	}
	state.host.log(
		(level >= 2
			? LogLevel::Error
			: (level == 1) ? LogLevel::Warn : LogLevel::Info),
		*text);
	return JS_UNDEFINED;
}

Engine::Engine(Limits limits, Host &host)
: _state(std::make_unique<State>(limits, host)) {
}

Engine::~Engine() = default;

Outcome Engine::loadMain(std::string_view path) {
	auto &state = *_state;
	return state.enter([&] {
		const auto ctx = state.ctx;
		if (state.loaded) {
			return JS_ThrowTypeError(ctx, "%s", "the main module is loaded");
		}
		const auto resolved = ResolveModulePath({}, path);
		if (!resolved) {
			const auto text = std::string(path);
			return JS_ThrowReferenceError(
				ctx,
				"invalid main module path \"%s\"",
				text.c_str());
		}
		state.loaded = true;
		state.loading = true;
		const auto compiled = state.compile(*resolved);
		return JS_IsException(compiled)
			? compiled
			: JS_EvalFunction(ctx, compiled);
	});
}

Outcome Engine::emit(std::string_view event, const nlohmann::json &payload) {
	auto &state = *_state;
	return state.enter([&] {
		const auto ctx = state.ctx;
		return state.callInternal(kEmit, {
			JS_NewStringLen(ctx, event.data(), event.size()),
			payload.is_null() ? JS_UNDEFINED : state.fromJson(payload),
		});
	});
}

Outcome Engine::callFunction(std::int64_t id, const nlohmann::json &args) {
	auto &state = *_state;
	return state.enter([&] {
		return state.callInternal(kCall, {
			JS_NewInt64(state.ctx, id),
			args.is_null() ? JS_UNDEFINED : state.fromJson(args),
		});
	});
}

void Engine::releaseFunction(std::int64_t id) {
	auto &state = *_state;
	if (!state.usable()) {
		return;
	}
	const auto release = [&] {
		return state.callInternal(kRelease, { JS_NewInt64(state.ctx, id) });
	};
	if (state.depth > 0) {
		const auto result = release();
		if (JS_IsException(result)) {
			DropException(state.ctx);
		}
		JS_FreeValue(state.ctx, result);
		return;
	}
	const auto out = state.enter(release);
	if (!out.ok()) {
		state.host.log(LogLevel::Error, out.error);
	}
}

Outcome Engine::fireTimer(std::int64_t id) {
	auto &state = *_state;
	return state.enter([&] {
		return state.callInternal(kFire, { JS_NewInt64(state.ctx, id) });
	});
}

Outcome Engine::resolve(std::uint64_t promiseId, const nlohmann::json &value) {
	return _state->settle(promiseId, true, value, {}, {});
}

Outcome Engine::reject(
		std::uint64_t promiseId,
		std::string_view name,
		std::string_view message) {
	return _state->settle(promiseId, false, nullptr, name, message);
}

Outcome Engine::runJobs() {
	return _state->enter([] { return JS_UNDEFINED; });
}

int Engine::overrunCount() const {
	return _state->overruns;
}

} // namespace Luxury::Plugins::Js
