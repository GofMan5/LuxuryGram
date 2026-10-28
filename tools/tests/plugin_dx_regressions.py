"""Plugin author experience: setting types, field-path errors, lux.d.ts.

docs/plugins/AUTHORING.md. The client is not built here, so:
- plugin_manifest.cpp and plugin_host_logic.h are compiled with -Wall
  -Wextra -Werror and checked: select, number, color and secret settings,
  description, placeholder and locales, errors that name the field path
  and echo the bad value, and SettingDefault/SettingAccepts for each new
  type;
- docs/plugins/lux.d.ts declares exactly the lux members of the prelude
  in js_engine.cpp, no more and no less;
- docs/plugins/examples/hello-world parses with the real ParseManifest,
  lists every module it ships and has defaults its settings accept; it
  carries an exact copy of lux.d.ts that main.js references as
  ./lux.d.ts, so completion still works after the folder is copied out.
Every check is also run against a mutant it must reject.

Bounded source check; not a native acceptance.
"""
from pathlib import Path
import json
import os
import re
import subprocess
import tempfile
from library_regressions import CXX, ROOT, STATIC, run, source, standalone

SRC = 'Telegram/SourceFiles/'
PLUGINS = SRC + 'luxury/plugins/'
LOGIC = PLUGINS + 'plugin_host_logic.h'
MANIFEST = PLUGINS + 'core/plugin_manifest.cpp'
ENGINE = PLUGINS + 'js/js_engine.cpp'
DTS = 'docs/plugins/lux.d.ts'
EXAMPLE = 'docs/plugins/examples/hello-world'
CORE = ('plugin_version', 'plugin_permissions', 'plugin_manifest', 'plugin_source', 'tl_schema')

MAIN = r'''
#include "luxury/plugins/plugin_host_logic.h"

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>

using namespace Luxury::Plugins;
using json = nlohmann::json;

#define EXPECT(condition) do { if (!(condition)) { \
	std::cerr << "line " << __LINE__ << ": " #condition "\n"; std::abort(); } } while (0)

namespace {

std::string Plugin(const std::string &settings, const std::string &extra = "") {
	return R"({"id":"dev.example.dx","name":"Dx","version":"1.0.0","api":1,"main":"main.js","settings":[)"
		+ settings + "]" + extra + "}";
}

void Refused(const std::string &text, const std::string &needle) {
	const auto result = ParseManifest(text);
	if (result || result.error().find(needle) == std::string::npos) {
		std::cerr << "expected error with '" << needle << "' for " << text << "\ngot: "
			<< (result ? std::string("ok") : result.error()) << "\n";
		std::abort();
	}
}

SettingDescriptor Setting(const std::string &setting) {
	const auto result = ParseManifest(Plugin(setting));
	if (!result) {
		std::cerr << "unexpected error for " << setting << ": " << result.error() << "\n";
		std::abort();
	}
	EXPECT(result.value().settings.size() == 1);
	return result.value().settings.front();
}

void Select() {
	const auto s = Setting(R"({"key":"s","title":"S","type":"select","options":["a","b"],"default":"b"})");
	EXPECT(s.type == SettingType::Select);
	EXPECT(s.defaultString == "b" && s.options.size() == 2);
	EXPECT(SettingDefault(s) == json("b"));
	EXPECT(SettingAccepts(s, json("a")));
	EXPECT(!SettingAccepts(s, json("c")));
	EXPECT(!SettingAccepts(s, json(1)));
	EXPECT(Setting(R"({"key":"s","title":"S","type":"select","options":["x","y"]})").defaultString == "x");
	EXPECT(Setting(R"({"key":"s","title":"S","type":"choice","options":["x"]})").type == SettingType::Choice);
	Refused(Plugin(R"({"key":"s","title":"S","type":"select","options":["a","b"],"default":"c"})"),
		"settings[0].default: must be one of options");
	Refused(Plugin(R"({"key":"s","title":"S","type":"select"})"), "settings[0].options: is required");
	Refused(Plugin(R"({"key":"s","title":"S","type":"select","options":[]})"), "settings[0].options: must not be empty");
	Refused(Plugin(R"({"key":"s","title":"S","type":"select","options":["a","a"]})"), "settings[0].options: duplicate \"a\"");
	Refused(Plugin(R"({"key":"s","title":"S","type":"select","options":["a",1]})"), "settings[0].options[1]: must be a string");
}

void Number() {
	const auto n = Setting(R"({"key":"n","title":"N","type":"number","min":0.5,"max":2.5,"step":0.25,"default":1.5})");
	EXPECT(n.type == SettingType::Number);
	EXPECT(n.defaultNumber == 1.5 && *n.minNumber == 0.5 && *n.maxNumber == 2.5 && *n.step == 0.25);
	EXPECT(SettingDefault(n) == json(1.5));
	EXPECT(SettingAccepts(n, json(0.5)));
	EXPECT(SettingAccepts(n, json(2)));
	EXPECT(SettingAccepts(n, json(1.3))); // The step is a hint, not a constraint.
	EXPECT(!SettingAccepts(n, json(0.4)));
	EXPECT(!SettingAccepts(n, json(2.6)));
	EXPECT(!SettingAccepts(n, json("1")));
	EXPECT(!SettingAccepts(n, json(std::numeric_limits<double>::quiet_NaN())));
	EXPECT(Setting(R"({"key":"n","title":"N","type":"number","min":3})").defaultNumber == 3.);
	EXPECT(Setting(R"({"key":"n","title":"N","type":"number"})").defaultNumber == 0.);
	const auto unbounded = Setting(R"({"key":"n","title":"N","type":"number","default":-7.5})");
	EXPECT(SettingAccepts(unbounded, json(1e9)) && SettingAccepts(unbounded, json(-1e9)));
	Refused(Plugin(R"({"key":"n","title":"N","type":"number","min":0,"max":5,"default":6})"),
		"settings[0].default: must be within min and max");
	Refused(Plugin(R"({"key":"n","title":"N","type":"number","min":0,"max":5,"default":-1})"),
		"settings[0].default: must be within min and max");
	Refused(Plugin(R"({"key":"n","title":"N","type":"number","min":5,"max":1})"), "settings[0].min: must not exceed max");
	Refused(Plugin(R"({"key":"n","title":"N","type":"number","step":0})"), "settings[0].step: must be positive");
	Refused(Plugin(R"({"key":"n","title":"N","type":"number","max":"9"})"), "settings[0].max: must be a number");
	Refused(Plugin(R"({"key":"n","title":"N","type":"number","default":true})"), "settings[0].default: must be a number");
}

void Color() {
	const auto c = Setting(R"({"key":"c","title":"C","type":"color","default":"#A0b1C2"})");
	EXPECT(c.type == SettingType::Color && c.defaultString == "#A0b1C2");
	EXPECT(SettingDefault(c) == json("#A0b1C2"));
	EXPECT(SettingAccepts(c, json("#000000")));
	EXPECT(SettingAccepts(c, json("#ffFFff")));
	EXPECT(!SettingAccepts(c, json("#00000")));
	EXPECT(!SettingAccepts(c, json("#0000000")));
	EXPECT(!SettingAccepts(c, json("000000#")));
	EXPECT(!SettingAccepts(c, json("#00000g")));
	EXPECT(!SettingAccepts(c, json("red")));
	EXPECT(!SettingAccepts(c, json(0)));
	EXPECT(IsValidColor("#123abc") && !IsValidColor("") && !IsValidColor("#12 456"));
	EXPECT(Setting(R"({"key":"c","title":"C","type":"color"})").defaultString == "#000000");
	Refused(Plugin(R"({"key":"c","title":"C","type":"color","default":"red"})"),
		"settings[0].default: must be a #RRGGBB color, got \"red\"");
	Refused(Plugin(R"({"key":"c","title":"C","type":"color","default":"#12345g"})"),
		"settings[0].default: must be a #RRGGBB color");
	Refused(Plugin(R"({"key":"c","title":"C","type":"color","default":1})"), "settings[0].default: must be a string");
}

void Secret() {
	const auto s = Setting(R"({"key":"token","title":"Token","type":"secret","placeholder":"paste it"})");
	EXPECT(s.type == SettingType::Secret && s.defaultString.empty() && s.placeholder == "paste it");
	EXPECT(SettingDefault(s) == json(""));
	EXPECT(SettingAccepts(s, json("abc")));
	EXPECT(!SettingAccepts(s, json(1)));
	Refused(Plugin(R"({"key":"token","title":"T","type":"secret","default":5})"), "settings[0].default: must be a string");
}

void Texts() {
	const auto s = Setting(R"({"key":"g","title":"Greeting","type":"string","description":"Shown","placeholder":"Hi",)"
		R"("locales":{"ru":{"name":"Приветствие","description":"Показ"},"pt-br":{"name":"Olá"}}})");
	EXPECT(s.description == "Shown" && s.placeholder == "Hi");
	EXPECT(s.locales.size() == 2);
	EXPECT(s.locales.at("ru").name == "Приветствие" && s.locales.at("ru").description == "Показ");
	EXPECT(s.locales.at("pt-br").name == "Olá" && s.locales.at("pt-br").description.empty());
	const auto top = ParseManifest(Plugin("", R"(,"locales":{"ru":{"name":"Дх"}})"));
	EXPECT(top && top.value().locales.at("ru").name == "Дх");
	Refused(Plugin("", R"(,"locales":[])"), "locales: must be an object");
	Refused(Plugin("", R"(,"locales":{"ru":{"name":1}})"), "locales.ru.name: must be a string");
	Refused(Plugin(R"({"key":"g","title":"G","type":"string","locales":{"RU":{}}})"),
		"settings[0].locales.RU: must be a language code");
	Refused(Plugin(R"({"key":"g","title":"G","type":"string","locales":{"ru-":{}}})"),
		"settings[0].locales.ru-: must be a language code");
	Refused(Plugin(R"({"key":"g","title":"G","type":"string","locales":{"ru":"x"}})"),
		"settings[0].locales.ru: must be an object");
	Refused(Plugin(R"({"key":"g","title":"G","type":"string","locales":{"ru":{"description":[]}}})"),
		"settings[0].locales.ru.description: must be a string");
	Refused(Plugin(R"({"key":"g","title":"G","type":"string","description":1})"), "settings[0].description: must be a string");
	Refused(Plugin(R"({"key":"g","title":"G","type":"string","placeholder":1})"), "settings[0].placeholder: must be a string");
	Refused(Plugin(R"({"key":"a","title":"A","type":"bool"},{"key":"g","title":"G","type":"slider"})"),
		"settings[1].type: unknown type \"slider\"");
}

void Paths() {
	Refused(R"({"id":"Bad_Id","name":"n","version":"1.0.0","api":1,"main":"m.js"})", "id: must match");
	Refused(R"({"id":"Bad_Id","name":"n","version":"1.0.0","api":1,"main":"m.js"})", "got \"Bad_Id\"");
	Refused(R"({"id":"a\"b","name":"n","version":"1.0.0","api":1,"main":"m.js"})", "got \"a\\\"b\"");
	const auto longId = std::string(100, 'x');
	Refused(R"({"id":")" + longId + R"(","name":"n","version":"1.0.0","api":1,"main":"m.js"})",
		"got \"" + std::string(64, 'x') + "\"...");
	Refused(Plugin(R"({"key":"a","title":"A","type":"bool"},{"key":"a","title":"B","type":"bool"})"),
		"settings[1].key: duplicate \"a\"");
}

void Example(const char *path) {
	auto file = std::ifstream(path, std::ios::binary);
	auto buffer = std::stringstream();
	buffer << file.rdbuf();
	const auto result = ParseManifest(buffer.str());
	if (!result) {
		std::cerr << "example: " << result.error() << "\n";
		std::abort();
	}
	const auto &m = result.value();
	EXPECT(m.id == "dev.example.hello" && m.main == "main.js");
	EXPECT(m.files == std::vector<std::string>{ "main.js" });
	EXPECT(!m.locales.empty());
	EXPECT(!m.settings.empty());
	for (const auto &setting : m.settings) {
		EXPECT(SettingAccepts(setting, SettingDefault(setting)));
	}
}

} // namespace

int main(int argc, char **argv) {
	EXPECT(argc == 2);
	Select();
	Number();
	Color();
	Secret();
	Texts();
	Paths();
	Example(argv[1]);
	std::cout << "plugin dx fixture ok\n";
	return 0;
}
'''

# Each compiles and must make the fixture fail an EXPECT.
MANIFEST_MUTANTS = (
    # A select default outside its options is accepted.
    ('\tif (known == to.options.end()) {\n\t\treturn Fail(reader.field("default"), "must be one of options");',
     '\tif (known == to.options.end() && false) {\n\t\treturn Fail(reader.field("default"), "must be one of options");'),
    # A number default out of range is accepted.
    ('\tif ((to.minNumber && to.defaultNumber < *to.minNumber)\n\t\t|| (to.maxNumber && to.defaultNumber > *to.maxNumber)) {',
     '\tif (false) {'),
    # A bad color is accepted.
    ('\treturn color.size() == 7\n\t\t&& color.front() == \'#\'\n\t\t&& std::all_of(color.begin() + 1, color.end(), hex);',
     '\treturn color.size() == 7\n\t\t&& color.front() == \'#\'\n\t\t&& (std::all_of(color.begin() + 1, color.end(), hex) || true);'),
    ('\treturn color.size() == 7\n', '\treturn color.size() >= 1\n'),
    # The field path is dropped from nested errors.
    ("const auto prefix = reader.field(\"locales\") + '.' + locale;", 'const auto prefix = locale;'),
    # The id error stops echoing the value.
    ('" without \\"..\\" or a device name, got "\n\t\t\t\t+ Shown(result.id));',
     '" without \\"..\\" or a device name");'),
)

LOGIC_MUTANTS = (
    ('\t\treturn std::isfinite(v)\n\t\t\t&& (!setting.minNumber || v >= *setting.minNumber)\n'
     '\t\t\t&& (!setting.maxNumber || v <= *setting.maxNumber);',
     '\t\treturn std::isfinite(v);'),
    ('\t\treturn value.is_string() && IsValidColor(value.get<std::string>());', '\t\treturn value.is_string();'),
    ('\tcase SettingType::Number: return setting.defaultNumber;\n', '\tcase SettingType::Number: return 0;\n'),
)


def crlf(text):
    return text.replace('\r\n', '\n')


def flags(tree):
    return ['-Wall', '-Wextra', '-Werror', '-I', str(tree), '-I', str(ROOT / SRC)]


def compile_unit(root, tree, unit, text, name):
    path = tree / 'luxury/plugins/core' / (unit + '.cpp')
    path.write_text(text, encoding='utf-8', newline='\n')
    obj = tree / (name + '.o')
    run([CXX, '-std=c++20', '-O0', '-c', *flags(tree), str(path), '-o', str(obj)], root)
    return str(obj)


def build_core(root, read):
    tree = root / 'plugin_dx'
    objects = {}
    for unit in CORE:
        header = tree / 'luxury/plugins/core' / (unit + '.h')
        header.parent.mkdir(parents=True, exist_ok=True)
        header.write_text(crlf(read(PLUGINS + 'core/' + unit + '.h')), encoding='utf-8', newline='\n')
        objects[unit] = compile_unit(root, tree, unit, crlf(read(PLUGINS + 'core/' + unit + '.cpp')), unit)
    return tree, objects


def fixture(root, tree, objects, logic, name):
    (tree / 'luxury/plugins/plugin_host_logic.h').write_text(logic, encoding='utf-8', newline='\n')
    main = root / (name + '.cpp')
    main.write_text(MAIN, encoding='utf-8')
    exe = root / (name + ('.exe' if os.name == 'nt' else ''))
    run([CXX, '-std=c++20', '-O0', *flags(tree), str(main), *objects.values(), *STATIC, '-o', str(exe)], root)
    standalone(exe)
    manifest = ROOT / EXAMPLE / 'plugin.json'
    result = subprocess.run([str(exe), str(manifest)], cwd=root, capture_output=True, text=True, errors='replace')
    return result.returncode, result.stdout + result.stderr


# --- lux.* drift between the prelude and lux.d.ts.

def skip_string(text, i):
    quote = text[i]
    i += 1
    while text[i] != quote:
        i += 2 if text[i] == '\\' else 1
    return i + 1


def object_keys(text, i, prefix, out):
    # text[i] is '{' of an object literal; records 'a', 'a.b', ... in out.
    assert text[i] == '{'
    i += 1
    key = re.compile(r'\s*(?:get\s+)?(\w+)\s*([:(])')
    while True:
        while text[i] in ' \t\r\n,':
            i += 1
        if text[i] == '}':
            return i + 1
        match = key.match(text, i)
        assert match, f'unexpected prelude text: {text[i:i + 40]!r}'
        name = prefix + match.group(1)
        out.add(name)
        i = match.end()
        if match.group(2) == ':':
            while text[i] in ' \t\r\n':
                i += 1
            if text[i] == '{':
                i = object_keys(text, i, name + '.', out)
                continue
        depth = 1 if match.group(2) == '(' else 0  # A method: its '(' is consumed.
        while depth or text[i] not in ',}':
            if text[i] in '\'"`':
                i = skip_string(text, i)
                continue
            if text[i] in '([{':
                depth += 1
            elif text[i] in ')]}':
                depth -= 1
            i += 1


def prelude_members(engine):
    start = engine.index('const lux = {')
    out = set()
    end = object_keys(engine, engine.index('{', start), '', out)
    assert engine[end:].startswith(';\nglobalThis.lux = lux;'), 'the lux literal must end before globalThis.lux'
    return out


def dts_members(dts):
    text = re.sub(r'/\*.*?\*/', '', dts, flags=re.S)
    text = re.sub(r'//[^\n]*', '', text)
    start = text.index('declare namespace lux {')
    i = start + len('declare namespace lux {')
    stack = ['lux']
    out = set()
    token = re.compile(r'namespace\s+(\w+)\s*\{|function\s+(\w+)|const\s+(\w+)|\{|\}')
    while stack:
        match = token.search(text, i)
        assert match, 'unterminated declare namespace lux'
        i = match.end()
        inside = all(entry is not None for entry in stack)
        path = '.'.join(stack[1:] + ['']) if inside else ''
        if match.group(1):
            if inside:
                out.add(path + match.group(1))
            stack.append(match.group(1) if inside else None)
        elif match.group(0) == '{':
            stack.append(None)
        elif match.group(0) == '}':
            stack.pop()
        elif inside:
            out.add(path + (match.group(2) or match.group(3)))
    return out


def check_drift(engine, dts):
    code, typed = prelude_members(engine), dts_members(dts)
    assert code == typed, f'lux.d.ts drift: prelude only {sorted(code - typed)}, d.ts only {sorted(typed - code)}'


DRIFT_MUTANTS = (
    (ENGINE, "\t\tkeys: () => call('storage.keys'),\n",
     "\t\tkeys: () => call('storage.keys'),\n\t\tclear: () => call('storage.clear'),\n"),
    (ENGINE, '\tclipboard: {\n', '\tclipboardx: {\n'),
    (DTS, '\t\tfunction remove(key: string): void;\n', ''),
    (DTS, '\t\tfunction addHotkey(options: LuxHotkeyOptions): void;\n',
     '\t\tfunction addHotkey(options: LuxHotkeyOptions): void;\n\t\tfunction addButton(): void;\n'),
)


# --- The example installs from a GitHub link as it is.

EXAMPLE_MUTANTS = (
    (EXAMPLE + '/main.js', '<reference path="./lux.d.ts" />', '<reference path="../../lux.d.ts" />'),
    (EXAMPLE + '/lux.d.ts', '\t\tfunction remove(key: string): void;\n', ''),
)


def check_example(read):
    manifest = json.loads(read(EXAMPLE + '/plugin.json'))
    main = read(EXAMPLE + '/' + manifest['main'])
    assert main.startswith('/// <reference path="./lux.d.ts" />'), 'the example must reference its own lux.d.ts'
    assert crlf(read(EXAMPLE + '/lux.d.ts')) == crlf(read(DTS)), f'{EXAMPLE}/lux.d.ts differs from {DTS}'
    folder = ROOT / EXAMPLE
    shipped = sorted(p.relative_to(folder).as_posix() for p in folder.rglob('*.js'))
    assert sorted(manifest['files']) == shipped, f'files {manifest["files"]} != modules {shipped}'
    assert manifest['main'] in manifest['files']
    members = prelude_members(crlf(read(ENGINE)))
    for member in re.findall(r"(?<![\w/.])lux\.(\w+(?:\.\w+)?)", main):
        assert member in members or member.startswith('plugin.'), f'the example calls a missing lux.{member}'


def check(root, read=source):
    engine, dts = crlf(read(ENGINE)), crlf(read(DTS))
    check_drift(engine, dts)
    files = {ENGINE: engine, DTS: dts}
    for path, before, after in DRIFT_MUTANTS:
        assert files[path].count(before) == 1, f'drift mutant anchor moved: {before!r}'
        mutated = dict(files)
        mutated[path] = files[path].replace(before, after)
        try:
            check_drift(mutated[ENGINE], mutated[DTS])
        except AssertionError:
            continue
        raise AssertionError(f'the drift check accepts the mutant: {after!r}')
    check_example(read)
    for path, before, after in EXAMPLE_MUTANTS:
        mutated = lambda p: read(p).replace(before, after) if p == path else read(p)
        assert read(path).count(before) == 1, f'example mutant anchor moved: {before!r}'
        try:
            check_example(mutated)
        except AssertionError:
            continue
        raise AssertionError(f'the example check accepts the mutant: {after!r}')

    tree, objects = build_core(root, read)
    logic = crlf(read(LOGIC))
    code, out = fixture(root, tree, objects, logic, 'plugin_dx')
    assert code == 0 and out.strip() == 'plugin dx fixture ok', out

    manifest = crlf(read(MANIFEST))
    for index, (before, after) in enumerate(MANIFEST_MUTANTS):
        assert manifest.count(before) == 1, f'manifest mutant anchor moved: {before!r}'
        mutated = dict(objects)
        try:
            mutated['plugin_manifest'] = compile_unit(
                root, tree, 'plugin_manifest', manifest.replace(before, after), f'plugin_manifest_m{index}')
            code, out = fixture(root, tree, mutated, logic, f'plugin_dx_m{index}')
        except AssertionError as error:
            # Each one compiles: a mutant only rejected by -Werror proves nothing.
            raise AssertionError(f'manifest mutant {index} does not compile: {error}')
        if code == 0:
            raise AssertionError(f'the dx fixture accepts the manifest mutant: {after!r}')
    compile_unit(root, tree, 'plugin_manifest', manifest, 'plugin_manifest')

    for index, (before, after) in enumerate(LOGIC_MUTANTS):
        assert logic.count(before) == 1, f'logic mutant anchor moved: {before!r}'
        try:
            code, out = fixture(root, tree, objects, logic.replace(before, after), f'plugin_dx_l{index}')
        except AssertionError as error:
            raise AssertionError(f'logic mutant {index} does not compile: {error}')
        if code == 0:
            raise AssertionError(f'the dx fixture accepts the logic mutant: {after!r}')
    print('Plugin dx PASS: select, number, color and secret settings, description, placeholder, '
          'locales, field-path errors with the bad value, SettingDefault/SettingAccepts, '
          'lux.d.ts matches the prelude, the hello-world example parses and ships its modules')


if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='luxury-plugin-dx-') as directory:
        check(Path(directory))
