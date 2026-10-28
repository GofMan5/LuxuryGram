"""Plugin JavaScript engine: the real js_engine.cpp on the vendored QuickJS-ng.

The QuickJS C sources are compiled once with gcc and cached by content hash;
they keep their asserts (no NDEBUG), so JS_FreeRuntime aborts the driver on
any leaked object and every case doubles as a teardown leak check.
js_engine.cpp is compiled with -Wall -Wextra -Werror. A driver with a fake
host runs every case in its own process; the mutant check disables the
interrupt handler and expects the CPU slice case to fail; the leak
mutant skips freeing pending promises and expects teardown to abort.
"""
from pathlib import Path
import hashlib
import os
import subprocess
import tempfile

from library_regressions import CXX, ROOT, STATIC, run, source, standalone

QUICKJS = ROOT / 'Telegram/SourceFiles/luxury/libs/quickjs'
ENGINE = 'Telegram/SourceFiles/luxury/plugins/js/js_engine.cpp'
HEADER = 'Telegram/SourceFiles/luxury/plugins/js/js_engine.h'
CC = os.environ.get('CC', str(Path(CXX).with_name('gcc.exe' if os.name == 'nt' else 'gcc')))
C_SOURCES = ('quickjs.c', 'libregexp.c', 'libunicode.c', 'dtoa.c')
C_FLAGS = (
    '-std=gnu11', '-O1', '-g0',
    '-D_GNU_SOURCE', '-DQUICKJS_NG_BUILD',
    '-DWIN32_LEAN_AND_MEAN', '-D_WIN32_WINNT=0x0601',
    '-funsigned-char',
    '-Wno-implicit-fallthrough', '-Wno-sign-compare',
    '-Wno-missing-field-initializers', '-Wno-unused-parameter',
    '-Wno-unused-but-set-variable', '-Wno-unused-result',
    '-Wno-stringop-truncation', '-Wno-array-bounds',
)
CASES = ('modules', 'events', 'async', 'interrupt', 'memory', 'stack', 'timers', 'callbacks', 'trace', 'teardown')

DRIVER = r'''
#include "luxury/plugins/js/js_engine.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Luxury::Plugins::Js;
using json = nlohmann::json;

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)

std::int64_t Now() {
	return std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::steady_clock::now().time_since_epoch()).count();
}

struct FakeHost final : Host {
	std::map<std::string, std::string> files;
	std::vector<std::string> reads;
	std::vector<std::string> logs;
	std::vector<std::pair<std::string, json>> calls;
	std::vector<std::uint64_t> pendingIds;
	std::int64_t nextTimer = 100;
	Engine *engine = nullptr;
	std::string reentered;

	HostResult call(std::string_view function, json args, const CallContext &context) override {
		const auto name = std::string(function);
		calls.emplace_back(name, args);
		if (name == "plugin.info") {
			return HostResult::Value({ { "id", "dev.test.plugin" }, { "version", "1.0.0" } });
		} else if (name == "timer.set") {
			return HostResult::Value(nextTimer++);
		} else if (name == "messages.send") {
			return HostResult::Error("PermissionError", "\"messages.send\" was not granted");
		} else if (name == "api.call" || name == "ui.confirm") {
			pendingIds.push_back(context.promiseId);
			return HostResult::Pending();
		} else if (name == "http.fetch") {
			return HostResult::Value({ { "status", 200 }, { "headers", json::object() }, { "body", "{\"a\":7}" } });
		} else if (name == "storage.get") {
			return HostResult::Value(args.at(0).get<std::string>() + "!");
		} else if (name == "storage.keys") {
			reentered = engine->emit("x", nullptr).error;
			return HostResult::Value(json::array());
		} else if (name == "clipboard.read") {
			throw std::runtime_error("host broke");
		}
		return HostResult::Value(nullptr);
	}
	void log(LogLevel level, std::string_view text) noexcept override {
		logs.push_back(std::to_string(int(level)) + ":" + std::string(text));
	}
	std::int64_t now() noexcept override {
		return Now();
	}
	std::optional<std::string> readModule(std::string_view path) override {
		reads.emplace_back(path);
		const auto i = files.find(std::string(path));
		if (i == files.end()) {
			return std::nullopt;
		}
		return i->second;
	}
	bool logged(std::string_view needle) const {
		return std::any_of(logs.begin(), logs.end(), [&](const std::string &line) {
			return line.find(needle) != std::string::npos;
		});
	}
	int count(std::string_view name) const {
		return int(std::count_if(calls.begin(), calls.end(), [&](const auto &call) {
			return call.first == name;
		}));
	}
	json last(std::string_view name) const {
		for (auto i = calls.rbegin(); i != calls.rend(); ++i) {
			if (i->first == name) {
				return i->second;
			}
		}
		return nullptr;
	}
};

bool Has(const std::string &text, std::string_view needle) {
	return text.find(needle) != std::string::npos;
}

void Modules() {
	CHECK(ResolveModulePath("", "main.js") == "main.js");
	CHECK(ResolveModulePath("", "src/main.js") == "src/main.js");
	CHECK(ResolveModulePath("lib/a.js", "./b.js") == "lib/b.js");
	CHECK(ResolveModulePath("lib/a.js", "../b.js") == "b.js");
	CHECK(ResolveModulePath("lib/a.js", "./x/./y/../c.js") == "lib/x/c.js");
	CHECK(!ResolveModulePath("lib/a.js", "../../b.js"));
	CHECK(!ResolveModulePath("main.js", "../b.js"));
	CHECK(!ResolveModulePath("main.js", "./x/../../b.js"));
	CHECK(!ResolveModulePath("main.js", "b.js"));
	CHECK(!ResolveModulePath("main.js", "os"));
	CHECK(!ResolveModulePath("main.js", "./a\\b.js"));
	CHECK(!ResolveModulePath("main.js", "./c:x.js"));
	CHECK(!ResolveModulePath("main.js", "./a//b.js"));
	CHECK(!ResolveModulePath("main.js", "./"));
	CHECK(!ResolveModulePath("main.js", "./."));
	CHECK(!ResolveModulePath("main.js", std::string_view("./a\0b.js", 8)));
	CHECK(!ResolveModulePath("", "/etc/passwd"));
	CHECK(!ResolveModulePath("", "../main.js"));
	CHECK(!ResolveModulePath("", ""));

	FakeHost host;
	host.files["main.js"] = R"js(import { twice } from './lib/util.js';
import { base } from './shared.js';
lux.log('twice', twice(base), lux.plugin.id);
console.warn('warned', { a: [1] });
console.error('failed', 5);
lux.log('eval', eval('1 + 2'));
lux.log('globals', typeof std, typeof os, typeof require);
)js";
	host.files["lib/util.js"] = "import { base } from '../shared.js';\nexport const twice = (x) => x * 2 + base - base;\n";
	host.files["shared.js"] = "export const base = 21;\n";
	Engine engine({}, host);
	const auto out = engine.loadMain("main.js");
	CHECK(out.ok());
	CHECK(host.logged("0:twice 42 dev.test.plugin"));
	CHECK(host.logged("1:warned {\"a\":[1]}"));
	CHECK(host.logged("2:failed 5"));
	CHECK(host.logged("0:eval 3"));
	CHECK(host.logged("0:globals undefined undefined undefined"));
	CHECK(host.count("plugin.info") == 1);
	CHECK(!engine.loadMain("main.js").ok());

	for (const auto spec : { "../secret.js", "os", "/abs.js", "./a\\\\b.js" }) {
		FakeHost escape;
		escape.files["main.js"] = std::string("import x from '") + spec + "';\nlux.log('loaded');\n";
		escape.files["../secret.js"] = escape.files["secret.js"] = escape.files["os"] = "export default 1;\n";
		Engine sandboxed({}, escape);
		const auto refused = sandboxed.loadMain("main.js");
		CHECK(!refused.ok());
		CHECK(Has(refused.error, "only relative paths inside the plugin are allowed"));
		CHECK(escape.reads == std::vector<std::string>{ "main.js" });
		CHECK(!escape.logged("loaded"));
	}

	FakeHost dynamic;
	dynamic.files["main.js"] = "await import('../secret.js');\n";
	Engine late({}, dynamic);
	const auto refused = late.loadMain("main.js");
	CHECK(Has(refused.error, "only relative paths"));
	CHECK(dynamic.reads.size() == 1);

	FakeHost missing;
	Engine none({}, missing);
	CHECK(Has(none.loadMain("main.js").error, "cannot load module \"main.js\""));
	FakeHost broken;
	broken.files["main.js"] = "let = ;\n";
	Engine syntax({}, broken);
	CHECK(Has(syntax.loadMain("main.js").error, "SyntaxError"));
}

void Events() {
	FakeHost host;
	host.files["main.js"] = R"js(lux.on('beforeSend', ({ text }) => text.toUpperCase());
lux.on('beforeSend', ({ text }) => text.includes('STOP') ? false : undefined);
lux.on('beforeSend', () => { throw new Error('bad handler'); });
lux.on('beforeSend', ({ peer, text }) => text + ' @' + peer.id);
lux.on('message', ({ message }) => lux.log('got', message.text));
lux.on('message', async () => { throw new RangeError('async handler'); });
lux.on('unload', (...args) => lux.log('unload', args.length));
const off = lux.on('ping', () => lux.log('ping'));
lux.on('unping', () => { off(); off(); });
try { lux.on('x', 5); } catch (e) { lux.log('on', e.name); }
)js";
	Engine engine({}, host);
	CHECK(engine.loadMain("main.js").ok());
	CHECK(host.logged("0:on TypeError"));
	CHECK(host.count("events.subscribe") == 9);

	auto out = engine.emit("beforeSend", { { "peer", { { "id", "7" } } }, { "text", "hi" } });
	CHECK(out.ok());
	CHECK(out.value == "HI @7");
	CHECK(host.logged("2:Error in event handler: Error: bad handler"));
	out = engine.emit("beforeSend", { { "peer", { { "id", "7" } } }, { "text", "please stop" } });
	CHECK(out.ok() && out.value == false);
	out = engine.emit("message", { { "message", { { "text", "yo" } } } });
	CHECK(out.ok() && out.value.is_null());
	CHECK(host.logged("0:got yo"));
	CHECK(host.logged("2:Error in event handler: RangeError: async handler"));
	CHECK(engine.emit("unload", nullptr).ok());
	CHECK(host.logged("0:unload 0"));
	CHECK(engine.emit("nobody", { { "a", 1 } }).value.is_null());

	CHECK(engine.emit("ping", nullptr).ok());
	CHECK(engine.emit("unping", nullptr).ok());
	CHECK(engine.emit("ping", nullptr).ok());
	CHECK(std::count(host.logs.begin(), host.logs.end(), "0:ping") == 1);
	CHECK(host.count("events.unsubscribe") == 1);
	CHECK(host.last("events.unsubscribe") == json::array({ "ping" }));
}

void Async() {
	FakeHost host;
	host.files["main.js"] = R"js(lux.api.call('help.getConfig', {}).then((r) => lux.log('config', r.dc));
lux.ui.confirm({ title: 't' }).then(() => lux.log('never'), (e) => lux.log('denied', e.name, e.message, e instanceof Error));
lux.log('sync', lux.storage.get('k'));
try { lux.messages.send({ peer: 1, text: 'x' }); } catch (e) { lux.log('perm', e.name, e instanceof Error, e.message); }
try { lux.clipboard.read(); } catch (e) { lux.log('host', e.name, e.message); }
lux.http.fetch('https://example.com', { method: 'GET' }).then((r) => lux.log('http', r.status, r.json().a, r.text()));
lux.storage.keys();
)js";
	Engine engine({}, host);
	host.engine = &engine;
	CHECK(engine.loadMain("main.js").ok());
	CHECK(host.logged("0:sync k!"));
	CHECK(host.logged("0:perm PermissionError true \"messages.send\" was not granted"));
	CHECK(host.logged("0:host InternalError host broke"));
	CHECK(host.logged("0:http 200 7 {\"a\":7}"));
	CHECK(host.last("http.fetch") == json::parse(R"(["https://example.com",{"method":"GET"}])"));
	CHECK(host.reentered == "the plugin engine is already running");
	CHECK(host.pendingIds.size() == 2);
	CHECK(!host.logged("config"));

	CHECK(engine.resolve(host.pendingIds[0], { { "dc", 2 } }).ok());
	CHECK(host.logged("0:config 2"));
	CHECK(engine.reject(host.pendingIds[1], "AbortError", "closed").ok());
	CHECK(host.logged("0:denied AbortError closed true"));
	CHECK(!host.logged("never"));
	CHECK(Has(engine.resolve(host.pendingIds[0], nullptr).error, "unknown promise"));

	FakeHost denied;
	denied.files["main.js"] = "lux.log('before');\nlux.messages.send({ peer: 1, text: 'x' });\n";
	Engine strict({}, denied);
	const auto out = strict.loadMain("main.js");
	CHECK(out.error.rfind("PermissionError: \"messages.send\" was not granted", 0) == 0);
	CHECK(Has(out.error, "main.js:2"));
}

void Interrupt() {
	FakeHost host;
	host.files["main.js"] = R"js(lux.on('spin', () => { while (true) {} });
lux.on('spinCatch', () => { try { while (true) {} } catch (e) { lux.log('caught'); } finally { lux.log('finally'); } });
lux.on('regex', () => /(a+)+$/.test('a'.repeat(40) + 'b'));
lux.on('ok', () => lux.log('alive'));
lux.on('jobs', () => { const loop = () => { Promise.resolve().then(loop); }; loop(); });
)js";
	Engine engine({}, host);
	CHECK(engine.loadMain("main.js").ok());
	for (const auto event : { "spin", "spinCatch", "regex" }) {
		const auto started = Now();
		const auto out = engine.emit(event, nullptr);
		const auto elapsed = Now() - started;
		std::printf("%s interrupted after %lld ms\n", event, (long long)elapsed);
		CHECK(out.overrun);
		CHECK(Has(out.error, "interrupted"));
		CHECK(elapsed >= 200 && elapsed < 1500);
	}
	CHECK(engine.overrunCount() == 3);
	CHECK(!host.logged("caught") && !host.logged("finally"));
	const auto out = engine.emit("ok", nullptr);
	CHECK(out.ok() && !out.overrun);
	CHECK(host.logged("0:alive"));
	CHECK(engine.overrunCount() == 0);

	const auto started = Now();
	const auto flood = engine.emit("jobs", nullptr);
	CHECK(flood.overrun && Has(flood.error, "interrupted"));
	CHECK(Now() - started < 1500);
	CHECK(engine.overrunCount() == 1);
}

void Memory() {
	FakeHost host;
	host.files["main.js"] = R"js(lux.on('bomb', () => { const a = []; for (;;) a.push('x'.repeat(1 << 16) + a.length); });
lux.on('caught', () => { try { const a = []; for (;;) a.push(new Array(1 << 16).fill(a.length)); } catch (e) { lux.log('oom', e.message); } });
lux.on('ok', () => lux.log('alive', new Array(1000).fill(1).length));
)js";
	Engine engine({ .memory = 32 << 20, .stack = 1 << 20, .sliceMs = 20000 }, host);
	CHECK(engine.loadMain("main.js").ok());
	auto out = engine.emit("bomb", nullptr);
	CHECK(out.ok() && !out.overrun);
	CHECK(host.logged("2:Error in event handler: InternalError: out of memory"));
	CHECK(engine.emit("caught", nullptr).ok());
	CHECK(host.logged("0:oom out of memory"));
	out = engine.emit("ok", nullptr);
	CHECK(out.ok());
	CHECK(host.logged("0:alive 1000"));

	FakeHost greedy;
	greedy.files["main.js"] = "const a = [];\nfor (;;) a.push('y'.repeat(1 << 16) + a.length);\n";
	Engine bounded({ .memory = 32 << 20, .stack = 1 << 20, .sliceMs = 20000 }, greedy);
	const auto bomb = bounded.loadMain("main.js");
	CHECK(!bomb.ok() && !bomb.overrun);
	CHECK(Has(bomb.error, "out of memory"));

	auto contextFailed = false;
	auto installFailed = false;
	for (auto memory = std::size_t(16 << 10); memory <= (4 << 20); memory += (8 << 10)) {
		FakeHost tiny;
		tiny.files["main.js"] = "lux.log('up');\n";
		Engine starved({ .memory = memory, .stack = 1 << 20, .sliceMs = 250 }, tiny);
		const auto loaded = starved.loadMain("main.js");
		if (loaded.ok()) {
			CHECK(tiny.logged("0:up"));
			break;
		}
		contextFailed |= Has(loaded.error, "could not create the JavaScript context");
		installFailed |= Has(loaded.error, "could not start the plugin runtime");
		CHECK(Has(loaded.error, "could not"));
		CHECK(starved.emit("ok", nullptr).error == loaded.error);
		CHECK(starved.runJobs().error == loaded.error);
		starved.releaseFunction(1);
		CHECK(tiny.reads.empty() && tiny.calls.empty());
	}
	CHECK(contextFailed && installFailed);
}

void Stack() {
	FakeHost host;
	host.files["main.js"] = R"js(lux.on('deep', () => { const f = (n) => f(n + 1) + 1; return f(0); });
lux.on('ok', () => lux.log('alive'));
)js";
	Engine engine({}, host);
	CHECK(engine.loadMain("main.js").ok());
	const auto out = engine.emit("deep", nullptr);
	CHECK(out.ok());
	CHECK(host.logged("Error in event handler: RangeError: Maximum call stack size exceeded"));
	CHECK(engine.emit("ok", nullptr).ok());
	CHECK(host.logged("0:alive"));
}

void Timers() {
	FakeHost host;
	host.files["main.js"] = R"js(let n = 0;
const id = setInterval((a, b) => { n++; lux.log('tick', n, a, b); if (n === 2) clearInterval(id); }, 50, 'x', 'y');
setTimeout(() => lux.log('once'), -5);
const gone = setTimeout(() => lux.log('cancelled'), 10);
clearTimeout(gone);
clearTimeout(gone);
setTimeout(() => { throw new Error('timer threw'); }, 0);
try { setTimeout('code'); } catch (e) { lux.log('string', e.name); }
)js";
	Engine engine({}, host);
	CHECK(engine.loadMain("main.js").ok());
	CHECK(host.logged("0:string TypeError"));
	CHECK(host.count("timer.set") == 4);
	CHECK(host.calls[0].second == json::parse(R"([{"delay":50,"repeat":true}])"));
	CHECK(host.calls[1].second == json::parse(R"([{"delay":0,"repeat":false}])"));
	CHECK(host.count("timer.clear") == 1);
	CHECK(host.last("timer.clear") == json::array({ 102 }));

	CHECK(engine.fireTimer(101).ok());
	CHECK(engine.fireTimer(101).ok());
	CHECK(std::count(host.logs.begin(), host.logs.end(), "0:once") == 1);
	CHECK(engine.fireTimer(102).ok());
	CHECK(!host.logged("cancelled"));
	CHECK(engine.fireTimer(100).ok());
	CHECK(engine.fireTimer(100).ok());
	CHECK(host.logged("0:tick 1 x y") && host.logged("0:tick 2 x y"));
	CHECK(host.count("timer.clear") == 2);
	CHECK(host.last("timer.clear") == json::array({ 100 }));
	CHECK(engine.fireTimer(100).ok());
	CHECK(!host.logged("tick 3"));
	const auto thrown = engine.fireTimer(103);
	CHECK(Has(thrown.error, "Error: timer threw"));
	CHECK(Has(thrown.error, "main.js:7"));
}

void Callbacks() {
	FakeHost host;
	host.files["main.js"] = R"js(lux.ui.addCommand({ name: 'echo', description: 'd', handler: ({ peer, args }) => { lux.log('echo', peer, args); return args.length; } });
lux.ui.addHotkey({ keys: 'Ctrl+K', handler: () => lux.log('hotkey') });
lux.ui.addMessageMenuItem({ title: 't', handler: async ({ message }) => message.id * 2 });
lux.settings.onChange((key) => ({ key, back: () => 1 }));
)js";
	Engine engine({}, host);
	CHECK(engine.loadMain("main.js").ok());
	CHECK(host.last("ui.addCommand") == json::parse(R"([{"name":"echo","description":"d","handler":{"$fn":1}}])"));
	CHECK(host.last("ui.addHotkey") == json::parse(R"([{"keys":"Ctrl+K","handler":{"$fn":2}}])"));
	CHECK(host.last("settings.onChange") == json::parse(R"([{"$fn":4}])"));

	auto out = engine.callFunction(1, { { "peer", "5" }, { "args", "abc" } });
	CHECK(out.ok() && out.value == 3);
	CHECK(host.logged("0:echo 5 abc"));
	out = engine.callFunction(2, nullptr);
	CHECK(out.ok() && out.value.is_null());
	CHECK(host.logged("0:hotkey"));
	out = engine.callFunction(3, { { "message", { { "id", 21 } } } });
	CHECK(out.ok() && out.value == 42);
	out = engine.callFunction(4, "text");
	CHECK(out.ok() && out.value == json::parse(R"({"key":"text","back":{"$fn":5}})"));

	engine.releaseFunction(1);
	out = engine.callFunction(1, { { "peer", "5" }, { "args", "abc" } });
	CHECK(!out.ok());
	CHECK(Has(out.error, "ReferenceError: callback 1 was released"));
	CHECK(Has(engine.callFunction(99, nullptr).error, "callback 99 was released"));
	engine.releaseFunction(99);
}

void Trace() {
	FakeHost host;
	host.files["main.js"] = R"js(import { explode } from './lib/util.js';
lux.on('boom', () => explode('kaboom'));
lux.on('logged', () => { try { explode('caught'); } catch (e) { console.error(e); } });
lux.ui.addCommand({ name: 'c', handler: () => explode('direct') });
)js";
	host.files["lib/util.js"] = "export function explode(why) {\n\tconst detail = why + '!';\n\tthrow new TypeError(detail);\n}\n";
	Engine engine({}, host);
	CHECK(engine.loadMain("main.js").ok());
	CHECK(engine.emit("boom", nullptr).ok());
	CHECK(host.logged("Error in event handler: TypeError: kaboom!\n    at explode (lib/util.js:3"));
	CHECK(host.logged("(main.js:2"));
	CHECK(engine.emit("logged", nullptr).ok());
	CHECK(host.logged("2:TypeError: caught!\n    at explode (lib/util.js:3"));
	const auto out = engine.callFunction(1, nullptr);
	CHECK(out.error.rfind("TypeError: direct!\n    at explode (lib/util.js:3:", 0) == 0);
	CHECK(Has(out.error, "main.js:4"));

	FakeHost top;
	top.files["main.js"] = "const a = 1;\n\nnull.x = a;\n";
	Engine failed({}, top);
	const auto error = failed.loadMain("main.js").error;
	std::printf("top-level error: %s\n", error.c_str());
	CHECK(error.rfind("TypeError: ", 0) == 0);
	CHECK(Has(error, "main.js:3"));
}

void Teardown() {
	FakeHost host;
	host.files["main.js"] = R"js(const wait = lux.api.call('x', {});
(async () => { await wait; lux.log('after'); })();
new Promise(() => {}).then(() => lux.log('never'));
setInterval(() => {}, 1000);
lux.on('x', () => {});
lux.ui.addCommand({ name: 'c', handler: () => {} });
Promise.reject(new Error('lost'));
Promise.reject(new Error('handled')).catch(() => {});
globalThis.cycle = { self: null };
cycle.self = cycle;
await new Promise(() => {});
)js";
	{
		Engine engine({}, host);
		const auto out = engine.loadMain("main.js");
		CHECK(out.ok() && out.value.is_null());
		CHECK(host.logged("2:Unhandled promise rejection: Error: lost"));
		CHECK(!host.logged("Error: handled"));
		CHECK(host.pendingIds.size() == 1);
	}
	FakeHost interrupted;
	interrupted.files["main.js"] = "lux.api.call('x', {}).then(() => {});\nwhile (true) {}\n";
	{
		Engine engine({ .memory = 64 << 20, .stack = 1 << 20, .sliceMs = 50 }, interrupted);
		CHECK(engine.loadMain("main.js").overrun);
	}
	std::printf("teardown leak-free\n");
}

int main(int argc, char **argv) {
	const std::map<std::string, void(*)()> cases = {
		{ "modules", Modules },
		{ "events", Events },
		{ "async", Async },
		{ "interrupt", Interrupt },
		{ "memory", Memory },
		{ "stack", Stack },
		{ "timers", Timers },
		{ "callbacks", Callbacks },
		{ "trace", Trace },
		{ "teardown", Teardown },
	};
	CHECK(argc == 2 && cases.count(argv[1]));
	cases.at(argv[1])();
	std::printf("%s PASS\n", argv[1]);
	return 0;
}
'''


def quickjs_objects():
    digest = hashlib.sha256()
    digest.update(' '.join((CC, *C_FLAGS)).encode())
    for path in sorted(QUICKJS.glob('*.[ch]')):
        digest.update(path.name.encode() + b'\0' + path.read_bytes())
    cache = Path(tempfile.gettempdir()) / ('luxury-quickjs-' + digest.hexdigest()[:16])
    cache.mkdir(exist_ok=True)
    objects = []
    for name in C_SOURCES:
        obj = cache / (name[:-2] + '.o')
        if not obj.exists():
            partial = cache / (name[:-2] + '.tmp.o')
            run([CC, *C_FLAGS, '-c', str(QUICKJS / name), '-o', str(partial)], cache)
            partial.replace(obj)
        objects.append(obj)
    return objects


def build(root, engine_path, name):
    include = '-I' + str(ROOT / 'Telegram/SourceFiles')
    engine_obj = root / (name + '_engine.o')
    run([CXX, '-std=c++20', '-O0', '-g', '-Wall', '-Wextra', '-Werror', include, '-c', str(engine_path), '-o', str(engine_obj)], root)
    driver = root / 'plugin_js_driver.cpp'
    driver_obj = root / 'plugin_js_driver.o'
    if not driver_obj.exists():
        driver.write_text(DRIVER, encoding='utf-8')
        run([CXX, '-std=c++20', '-O0', '-g', '-Wall', include, '-c', str(driver), '-o', str(driver_obj)], root)
    exe = root / (name + ('.exe' if os.name == 'nt' else ''))
    run([CXX, str(driver_obj), str(engine_obj), *map(str, quickjs_objects()), *STATIC, '-o', str(exe)], root)
    standalone(exe)
    return exe


def mutant_survives(root, text, original, replacement, case):
    assert text.count(original) == 1, original
    path = root / 'js_engine_mutant.cpp'
    path.write_text(text.replace(original, replacement), encoding='utf-8')
    exe = build(root, path, 'plugin_js_mutant')
    try:
        result = subprocess.run([str(exe), case], cwd=root, capture_output=True, text=True, timeout=15)
    except subprocess.TimeoutExpired:
        return False
    return result.returncode == 0


def check(root, read=source):
    engine = read(ENGINE)
    header = read(HEADER)
    for text in (engine, header):
        assert '\r' not in text and '#include <Q' not in text and 'Q_' not in text
        assert '(void)' not in text and 'static_cast<void>' not in text
    assert 'namespace Luxury::Plugins::Js' in header
    readme = (QUICKJS / 'README.md').read_text(encoding='utf-8')
    assert 'v0.17.0' in readme and '6d46d07d04041b40f4f49eaa7fdebe44c314c699' in readme
    assert (QUICKJS / 'LICENSE').exists()
    assert not any((QUICKJS / name).exists() for name in ('qjs.c', 'qjsc.c', 'quickjs-libc.c', 'quickjs-libc.h'))

    exe = build(root, ROOT / ENGINE, 'plugin_js')
    for case in CASES:
        print(run([str(exe), case], root).strip())

    assert not mutant_survives(root, engine, '\tif (state.depth == 0) {\n\t\treturn 0;', '\tif (true) {\n\t\treturn 0;', 'interrupt'), 'interrupt mutant survived'
    assert not mutant_survives(root, engine, 'for (const auto &[id, funcs] : pending) {', 'for (const auto &[id, funcs] : decltype(pending)()) {', 'teardown'), 'leak mutant survived'
    print('Plugin JS PASS: ES modules confined to the plugin dir; beforeSend rewrite/cancel; async host calls settle later; PermissionError surfaces; CPU slice interrupts loops, regexps and job floods; memory limit without a crash; timers; $fn callbacks; file:line traces; leak-free teardown; interrupt and leak mutants caught')


if __name__ == '__main__':
    with tempfile.TemporaryDirectory() as d:
        check(Path(d))
