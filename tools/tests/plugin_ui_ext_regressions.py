"""Plugin UI extension points: forms, links, menu items and message labels.

plugin_ui_logic.h is compiled with g++ and checked by a MAIN with asserts:
- IsUiExtFunction routes exactly the nine ui.* extension functions;
- ParseFormSchema is strict: an unknown field type or member, a repeated
  key, a bad default or too many fields is a TypeError naming the path;
- IsPluginOpenableUrl takes http(s) links only;
- DecorationText makes one trimmed line of at most 32 code points.
Each logic rule is also broken by a compile-time mutant the MAIN must reject.

The Qt side is not built here, so it is checked statically, each check
against a mutant it must reject:
- message layout and paint read the label cache and never enter an engine;
- ui.openUrl goes through HiddenUrlClickHandler (the confirmation box),
  never QDesktopServices or the plain UrlClickHandler, refuses a link the
  client would turn into a tg:// one (https://t.me/x?start=y), and opens
  only right after a user gesture, since the box can be turned off;
- unload removes every menu item, header button and label, and an open
  form settles null when its box closes;
- the dispatch reaches uiExt before the generic ui. branch, forms are
  refused while passcode-locked, the service chat is never shown, and menu
  clicks reach the plugin asynchronously;
- the client hooks are wired at their call sites: the compose menu for the
  field's own chat, chatOpened once per chat of the main window.

Bounded source check; not a native acceptance.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile
from library_regressions import CXX, ROOT, STATIC, function, run, source, standalone


SRC = 'Telegram/SourceFiles/'
PLUGINS = SRC + 'luxury/plugins/'
LOGIC = PLUGINS + 'plugin_ui_logic.h'
EXT = PLUGINS + 'plugin_host_ext_ui.cpp'
HOOKS = PLUGINS + 'plugin_hooks_ui.cpp'
HOST = PLUGINS + 'plugin_host_api.cpp'
MESSAGE = SRC + 'history/view/history_view_message.cpp'
PEER_MENU = SRC + 'window/window_peer_menu.cpp'
TOP_BAR = SRC + 'history/view/history_view_top_bar_widget.cpp'
AUTOCOMPLETE = SRC + 'chat_helpers/field_autocomplete.cpp'
SESSION = SRC + 'window/window_session_controller.cpp'
FILES = (EXT, HOOKS, HOST, MESSAGE, PEER_MENU, TOP_BAR, AUTOCOMPLETE, SESSION)

MAIN = r'''
#include "luxury/plugins/plugin_ui_logic.h"

#include <cstdio>
#include <cstdlib>

using namespace Luxury::Plugins;
using json = nlohmann::json;

#define CHECK(x) do { if (!(x)) { std::printf("FAIL %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)

static bool Rejected(const char *text, const char *part) {
	auto error = std::string();
	const auto result = ParseFormSchema(json::parse(text), error);
	if (result) {
		std::printf("accepted: %s\n", text);
		return false;
	} else if (error.find(part) == std::string::npos) {
		std::printf("error \"%s\" lacks \"%s\"\n", error.c_str(), part);
		return false;
	}
	return true;
}

int main() {
	for (const auto name : {
			"ui.addChatMenuItem", "ui.addHeaderButton", "ui.addComposeMenuItem",
			"ui.showForm", "ui.currentChat", "ui.openChat", "ui.openUrl",
			"ui.decorateMessage", "ui.refreshMessage" }) {
		CHECK(IsUiExtFunction(name));
	}
	CHECK(!IsUiExtFunction("ui.toast"));
	CHECK(!IsUiExtFunction("ui.addCommand"));
	CHECK(!IsUiExtFunction("ui.prompt"));
	CHECK(!IsUiExtFunction(""));

	auto error = std::string();
	const auto form = ParseFormSchema(json::parse(R"({
		"title": "Settings",
		"fields": [
			{ "type": "text", "key": "name", "label": "Name", "placeholder": "you" },
			{ "type": "toggle", "key": "on", "default": true },
			{ "type": "select", "key": "mode", "options": ["a", "b"] },
			{ "type": "select", "key": "pick", "options": ["a", "b"], "default": "b" }
		]
	})"), error);
	CHECK(form && form->size() == 4);
	CHECK((*form)[0].type == "text" && (*form)[0].value == "" && (*form)[0].placeholder == "you");
	CHECK((*form)[0].label == "Name");
	CHECK((*form)[1].type == "toggle" && (*form)[1].value == true);
	CHECK((*form)[2].value == "a" && (*form)[2].options.size() == 2);
	CHECK((*form)[3].value == "b");
	CHECK(ParseFormSchema(json::parse(R"({"fields":[{"type":"toggle","key":"t"}]})"), error)->front().value == false);

	CHECK(Rejected(R"({"fields":[{"type":"slider","key":"x"}]})", "fields[0].type: unknown field type slider"));
	CHECK(Rejected(R"({"fields":[{"type":"text","key":"a"},{"type":"Text","key":"b"}]})", "fields[1].type: unknown field type"));
	CHECK(Rejected(R"({"fields":[{"key":"x"}]})", "fields[0].type"));
	CHECK(Rejected(R"({"fields":[{"type":1,"key":"x"}]})", "fields[0].type"));
	CHECK(Rejected(R"([])", "object"));
	CHECK(Rejected(R"({})", "fields"));
	CHECK(Rejected(R"({"fields":[]})", "fields"));
	CHECK(Rejected(R"({"fields":[{"type":"text","key":"x"}],"extra":1})", "unknown schema member extra"));
	CHECK(Rejected(R"({"fields":[{"type":"text","key":"x","colour":1}]})", "fields[0]: unknown member colour"));
	CHECK(Rejected(R"({"fields":[{"type":"text","key":"x","options":["a"]}]})", "unknown member options"));
	CHECK(Rejected(R"({"fields":[{"type":"text"}]})", "fields[0].key is missing"));
	CHECK(Rejected(R"({"fields":[{"type":"text","key":"bad key"}]})", "fields[0].key"));
	CHECK(Rejected(R"({"fields":[{"type":"text","key":"x"},{"type":"toggle","key":"x"}]})", "fields[1].key repeats x"));
	CHECK(Rejected(R"({"fields":[{"type":"toggle","key":"x","default":"yes"}]})", "fields[0].default must be a boolean"));
	CHECK(Rejected(R"({"fields":[{"type":"text","key":"x","default":5}]})", "fields[0].default must be a string"));
	CHECK(Rejected(R"({"fields":[{"type":"select","key":"x"}]})", "fields[0].options is missing"));
	CHECK(Rejected(R"({"fields":[{"type":"select","key":"x","options":[]}]})", "fields[0].options"));
	CHECK(Rejected(R"({"fields":[{"type":"select","key":"x","options":[1]}]})", "fields[0].options"));
	CHECK(Rejected(R"({"fields":[{"type":"select","key":"x","options":["a"],"default":"z"}]})", "not one of the options"));
	CHECK(Rejected(R"({"title":5,"fields":[{"type":"text","key":"x"}]})", "title"));
	{
		auto many = json::object();
		many["fields"] = json::array();
		for (auto i = 0; i != int(kMaxFormFields) + 1; ++i) {
			many["fields"].push_back({ { "type", "text" }, { "key", "k" + std::to_string(i) } });
		}
		CHECK(!ParseFormSchema(many, error));
		many["fields"].erase(many["fields"].begin());
		CHECK(ParseFormSchema(many, error));
	}

	CHECK(IsPluginOpenableUrl("https://example.com"));
	CHECK(IsPluginOpenableUrl("http://example.com/path?q=1#x"));
	CHECK(IsPluginOpenableUrl("HTTPS://Example.com"));
	CHECK(!IsPluginOpenableUrl("tg://resolve?domain=x"));
	CHECK(!IsPluginOpenableUrl("javascript:alert(1)"));
	CHECK(!IsPluginOpenableUrl("file:///C:/Windows"));
	CHECK(!IsPluginOpenableUrl("ftp://example.com"));
	CHECK(!IsPluginOpenableUrl("https://"));
	CHECK(!IsPluginOpenableUrl("https:///path"));
	CHECK(!IsPluginOpenableUrl("https://@evil.com"));
	CHECK(!IsPluginOpenableUrl("https://exa mple.com"));
	CHECK(!IsPluginOpenableUrl("https://example.com\n"));
	CHECK(!IsPluginOpenableUrl("https://" + std::string(kMaxPluginUrl, 'a')));

	CHECK(DecorationText("  hello \n world\t") == "hello   world");
	CHECK(DecorationText(" \n\t ").empty());
	CHECK(DecorationText("").empty());
	CHECK(DecorationText(std::string(40, 'x')) == std::string(32, 'x'));
	{
		auto wide = std::string();
		for (auto i = 0; i != 40; ++i) {
			wide += "\xC3\xA9";
		}
		CHECK(DecorationText(wide).size() == 64);
	}

	std::printf("plugin_ui_logic MAIN OK\n");
	return 0;
}
'''

LOGIC_MUTANTS = (
    # An unknown field type is accepted.
    ('''		if (field.type != "text"
			&& field.type != "toggle"
			&& field.type != "select") {''', '''		if (field.type.empty()) {'''),
    # An unknown field member is ignored.
    ('''				return fail(where + ": unknown member " + name);''', '''				continue;'''),
    ('''			return fail("unknown schema member " + name);''', '''			continue;'''),
    ('''			if (other.key == field.key) {''', '''			if (other.key == field.key && false) {'''),
    ('''			} else if (!field.value.is_boolean()) {''', '''			} else if (false) {'''),
    ('''			if (!known) {''', '''			if (!known && false) {'''),
    ('''		|| fields->size() > kMaxFormFields) {''', '''		|| fields->size() > kMaxFormFields + 1) {'''),
    ('''		: std::size_t(0);''', '''		: std::size_t(1);'''),
    ('''	return !host.empty() && host.front() != '@' && host.front() != ':';''', '''	return !host.empty() || true;'''),
    ('''		if (code <= 0x20 || code == 0x7f) {''', '''		if (code < 0x20) {'''),
    ('''		if (lead && ++chars > kMaxDecorationChars) {''', '''		if (lead && ++chars > kMaxDecorationChars + 8) {'''),
    ('''		const auto lead = (static_cast<unsigned char>(text[i]) & 0xC0) != 0x80;''', '''		const auto lead = (static_cast<unsigned char>(text[i]) != 0);'''),
    ('''		"ui.showForm",
''', '''		"ui.showFormX",
'''),
)


def build(root, logic, name):
    tree = root / 'plugin_ui_ext'
    header = tree / 'luxury/plugins/plugin_ui_logic.h'
    header.parent.mkdir(parents=True, exist_ok=True)
    header.write_text(logic, encoding='utf-8', newline='\n')
    main = root / (name + '.cpp')
    main.write_text(MAIN, encoding='utf-8')
    exe = root / (name + ('.exe' if os.name == 'nt' else ''))
    run([CXX, '-std=c++20', '-O0', '-Wall', '-Wextra', '-Werror', '-I', str(tree), '-I', str(ROOT / SRC), str(main), *STATIC, '-o', str(exe)], root)
    standalone(exe)
    result = subprocess.run([str(exe)], cwd=root, capture_output=True, text=True, errors='replace')
    return result.returncode, result.stdout + result.stderr


# --- Static checks of the Qt side.

ENGINE = re.compile(r'\brun\(|callFunction|\bJs::|_engine|callLater|emitLater|loadMain|fireTimer')


def check_paint(files):
    # refreshRightBadge runs from initDimensions (layout); its label must come
    # from the cache, and the cache read must never reach an engine.
    badge = function(files[MESSAGE], 'void Message::refreshRightBadge() {')
    assert 'Luxury::Plugins::MessageDecoration(item)' in badge, 'the badge does not read the plugin label'
    calls = set(re.findall(r'Luxury::Plugins::(\w+)', files[MESSAGE]))
    assert calls == {'MessageDecoration'}, f'history_view_message.cpp calls plugin hooks {sorted(calls)}'
    for name, body in (
            ('refreshRightBadge', badge),
            ('MessageDecoration', function(files[HOOKS], 'QString MessageDecoration(')),
            ('Instance::decoration', function(files[EXT], 'QString Instance::decoration('))):
        found = ENGINE.search(body)
        assert not found, f'{name} enters the engine: {found.group(0)}'


def open_url_branch(text):
    start = text.index('} else if (function == "ui.openUrl") {')
    return text[start:text.index('return Result::Value(nullptr);', start)]


def check_open_url(files):
    text = files[EXT]
    branch = open_url_branch(text)
    assert 'IsPluginOpenableUrl(' in branch, 'ui.openUrl does not check the scheme'
    assert 'HiddenUrlClickHandler::Open(' in branch, 'ui.openUrl bypasses HiddenUrlClickHandler'
    assert '.forceExternalUrlConfirmation = true' in branch, 'ui.openUrl skips the confirmation'
    assert 'ToastAllowed(now, _lastOpenUrl)' in branch, 'ui.openUrl is not rate limited'
    opened = branch.index('HiddenUrlClickHandler::Open(')
    # https://t.me/x?start=y becomes tg://resolve?...&start=y, which the
    # handler runs with no box: a bot /start sent on the plugin's behalf.
    local = branch.find('if (Core::TryConvertUrlToLocal(url) != url) {')
    assert 0 <= local < opened, 'ui.openUrl opens t.me links as tg:// ones'
    assert 'Telegram links are not allowed' in branch[local:opened], 'ui.openUrl lets t.me links through'
    gesture = branch.find('if (!GestureActive(now, _gestureUntil)) {')
    assert 0 <= gesture < opened, 'ui.openUrl opens links without a user gesture'
    assert '"NotAllowedError"' in branch[gesture:opened], 'ui.openUrl outside a gesture is not refused'
    for path in (EXT, HOOKS):
        code = re.sub(r'//[^\n]*', '', files[path])
        for banned in ('QDesktopServices', 'File::OpenUrl', 'openUrl('):
            assert banned not in code, f'{path} opens links with {banned}'
        assert not re.search(r'(?<!Hidden)UrlClickHandler::Open', code), \
            f'{path} opens links without the confirmation'


def check_unload(files):
    clear = function(files[EXT], 'void Instance::clearUiExt() {')
    assert 'base::take(*list)' in clear, 'clearUiExt keeps the items'
    lists = re.search(r'for \(const auto list : \{([^}]*)\}\)', clear)
    assert lists, 'clearUiExt does not walk the item lists'
    taken = {name.strip() for name in lists.group(1).split(',')}
    assert taken == {'&_chatMenuItems', '&_headerButtons', '&_composeMenuItems'}, f'clearUiExt clears {sorted(taken)}'
    assert '_engine->releaseFunction(item.fn);' in clear, 'clearUiExt leaks the handlers'
    assert 'base::take(_decorations)' in clear, 'clearUiExt keeps the labels'
    assert 'NotifyHeaderButtons();' in clear, 'the header button outlives the plugin'
    unload = function(files[HOST], 'void Instance::unload() {')
    assert 'clearUiExt();' in unload, 'unload keeps the UI items'
    assert '_dialog.get()' in unload, 'unload leaves the dialog open'
    form = function(files[EXT], 'Js::HostResult Instance::showForm(')
    assert form.index('ParseFormSchema(') < form.index('_dialog = box.data();'), 'the form is shown unchecked'
    assert '} else if (_dialog) {' in form, 'a second form opens'
    assert re.search(r'boxClosing\(\) \| rpl::on_next\(\[=\] \{\s*settle\(nullptr\);', form), \
        'a closed form never settles'
    assert 'strong->_dialog = nullptr;' in form, 'a settled form blocks the next one'


def check_dispatch(files):
    call = function(files[HOST], 'Js::HostResult Instance::call(')
    ext = call.index('} else if (IsUiExtFunction(function)) {\n\t\treturn uiExt(function, args, context.promiseId);')
    assert ext < call.index('} else if (function.substr(0, 3) == "ui.") {'), 'uiExt is shadowed by the ui. branch'
    ui = function(files[EXT], 'Js::HostResult Instance::uiExt(')
    locked = ui.index('Core::App().passcodeLocked()')
    assert locked < ui.index('return showForm(options, promiseId);'), 'a form shows while passcode-locked'
    assert locked < ui.index('function == "ui.openUrl"'), 'a link opens while passcode-locked'
    assert locked < ui.index('function == "ui.openChat"'), 'a chat opens while passcode-locked'
    assert 'event == "chatOpened") {\n\t\treturn Permission::Ui;' in files[HOST], 'chatOpened needs no permission'


def check_hidden(files):
    shown = function(files[HOOKS], 'bool Shown(PeerData *peer) {')
    assert 'peer->id != PeerData::kServiceNotificationsId' in shown, 'plugins see the service chat'
    assert 'domain().active().maybeSession()' in shown, 'plugins see another account'
    resolve = function(files[HOST], 'PeerData *ResolvePeer(')
    assert 'peer->id == PeerData::kServiceNotificationsId' in resolve, 'ResolvePeer returns the service chat'
    ui = function(files[EXT], 'Js::HostResult Instance::uiExt(')
    current = ui[ui.index('if (function == "ui.currentChat") {'):ui.index('} else if (!controller) {')]
    assert 'peer->id != PeerData::kServiceNotificationsId' in current, 'currentChat returns the service chat'
    hooks = files[HOOKS]
    for name in ('bool HasItems(', 'void FillHeaderMenu(', 'void ChatOpened(', 'PluginCommands(\n', 'QString MessageDecoration('):
        assert 'Shown(' in function(hooks, name), f'{name} skips the Shown check'


def check_clicks(files):
    items = function(files[HOOKS], 'void FillItems(')
    assert 'target->callLater(fn, {' in items, 'a menu click does not reach the plugin asynchronously'
    assert 'callFunction' not in items, 'a menu click enters the engine synchronously'
    assert 'handler.fn == fn' in items, 'a click runs a removed item'
    assert 'Find(running, pluginId)' in items, 'a click runs in a stopped plugin'


def check_wiring(files):
    assert files[PEER_MENU].count('Luxury::Plugins::FillChatMenu(_peer, _addAction);') == 2, 'chat menu items are not wired'
    top = files[TOP_BAR]
    for call in ('FillHeaderMenu(', 'HasHeaderButtons(', 'HeaderButtonsChanges('):
        assert 'Luxury::Plugins::' + call in top, f'the top bar misses {call}'
    # Every TopBarWidget (sections, topics, other windows) sets a chat.
    assert 'ChatOpened(' not in top, 'chatOpened fires from every top bar'
    session = files[SESSION]
    assert session.count('Luxury::Plugins::ChatOpened(') == 1, 'chatOpened is not wired'
    start = session.index('\tif (_isPrimary) {\n\t\tactiveChatChanges(')
    block = session[start:session.index('}, lifetime());', start)]
    assert 'Luxury::Plugins::ChatOpened(peer);' in block, 'chatOpened fires outside the main window'
    assert 'rpl::distinct_until_changed(' in block, 'chatOpened repeats for the same chat'
    field = files[AUTOCOMPLETE]
    assert 'Luxury::Plugins::FillComposeMenu(menu, peer);' in field, 'compose menu items are not wired'
    assert 'field->property("luxuryPluginsPeer")' in field, 'the compose menu ignores the field chat'
    compose = function(files[HOOKS], 'void FillComposeMenu(')
    for other in ('activeWindow', 'activeChatCurrent', 'sessionController'):
        assert other not in compose, f'the compose menu reads the chat from {other}'
    assert '!field->property("luxuryPluginsMenu").toBool()' in field, 'the compose hook stacks up per chat'
    assert 'Luxury::Plugins::PluginCommands(peer)' in field, 'plugin commands are not listed'


CHECKS = (check_paint, check_open_url, check_unload, check_dispatch, check_hidden, check_clicks, check_wiring)

STATIC_MUTANTS = (
    # Decorate calls the engine in layout or paint.
    (check_paint, HOOKS, '''			auto label = instance->decoration(dialogId, item->id.bare);''',
        '''				instance->callLater(0, {});
			auto label = instance->decoration(dialogId, item->id.bare);'''),
    (check_paint, MESSAGE, '''		tagText.text = Luxury::Plugins::MessageDecoration(item);''',
        '''		tagText.text = Luxury::Plugins::MessageDecoration(item);
		Js::Engine::Ping();'''),
    (check_paint, EXT, '''	const auto i = _decorations.find({ dialogId, msgId });''',
        '''	run([&] { return 0; });
	const auto i = _decorations.find({ dialogId, msgId });'''),
    (check_paint, MESSAGE, '''		tagText.text = Luxury::Plugins::MessageDecoration(item);''', '''		tagText.text = QString();'''),
    # openUrl bypasses UrlClickHandler.
    (check_open_url, EXT, '''		HiddenUrlClickHandler::Open(''', '''		QDesktopServices::openUrl('''),
    (check_open_url, EXT, '''		HiddenUrlClickHandler::Open(''', '''		UrlClickHandler::Open('''),
    (check_open_url, EXT, '''				.forceExternalUrlConfirmation = true,''', '''				.forceExternalUrlConfirmation = false,'''),
    (check_open_url, EXT, '''			|| !IsPluginOpenableUrl(options.get_ref<const std::string&>())) {''', '''			) {'''),
    # openUrl lets https://t.me/x?start=y through, or opens without a gesture.
    (check_open_url, EXT, '''		if (Core::TryConvertUrlToLocal(url) != url) {''', '''		if (false) {'''),
    (check_open_url, EXT, '''		if (!GestureActive(now, _gestureUntil)) {''', '''		if (false) {'''),
    # Menu items are not removed on unload.
    (check_unload, HOST, '''	clearUiExt();
''', ''),
    (check_unload, EXT, '''&_chatMenuItems, &_headerButtons, &_composeMenuItems''', '''&_chatMenuItems, &_composeMenuItems'''),
    (check_unload, EXT, '''		for (const auto &item : base::take(*list)) {''', '''		for (const auto &item : *list) {'''),
    (check_unload, EXT, '''	const auto decorations = base::take(_decorations);''', '''	const auto decorations = _decorations;'''),
    (check_unload, EXT, '''			settle(nullptr);
''', ''),
    (check_unload, EXT, '''	} else if (_dialog) {''', '''	} else if (false) {'''),
    (check_dispatch, HOST, '''	} else if (IsUiExtFunction(function)) {
		return uiExt(function, args, context.promiseId);
''', ''),
    (check_dispatch, EXT, '''	} else if (Core::IsAppLaunched() && Core::App().passcodeLocked()) {''', '''	} else if (Core::IsAppLaunched() && false) {'''),
    (check_hidden, HOOKS, '''		&& (peer->id != PeerData::kServiceNotificationsId)
''', ''),
    (check_hidden, HOST, '''	return (peer && peer->id == PeerData::kServiceNotificationsId)''', '''	return (peer && false)'''),
    (check_hidden, EXT, '''		return Result::Value((peer
				&& peer->id != PeerData::kServiceNotificationsId)''', '''		return Result::Value((peer)'''),
    (check_hidden, HOOKS, '''	if (!manager || !Shown(peer)) {
		return result;''', '''	if (!manager) {
		return result;'''),
    (check_clicks, HOOKS, '''					target->callLater(fn, {''', '''					target->callFunction(fn, {'''),
    (check_clicks, HOOKS, '''						return (handler.fn == fn);''', '''						return true;'''),
    (check_wiring, PEER_MENU, '''	Luxury::Plugins::FillChatMenu(_peer, _addAction);
	addDeleteChat();''', '''	addDeleteChat();'''),
    (check_wiring, SESSION, '''	if (_isPrimary) {
		activeChatChanges(''', '''	if (true) {
		activeChatChanges('''),
    (check_wiring, SESSION, '''		}) | rpl::distinct_until_changed(
		) | rpl::on_next([](PeerData *peer) {''', '''		}) | rpl::on_next([](PeerData *peer) {'''),
    (check_wiring, TOP_BAR, '''	const auto peerChanged = ''', '''	Luxury::Plugins::ChatOpened(nullptr);
	const auto peerChanged = '''),
    (check_wiring, HOOKS, '''	auto submenu = std::make_unique<Ui::PopupMenu>(menu, menu->st());''',
        '''	const auto window = Core::App().activeWindow();
	auto submenu = std::make_unique<Ui::PopupMenu>(menu, menu->st());'''),
)


def check(root):
    logic = source(LOGIC).replace('\r\n', '\n')
    code, out = build(root, logic, 'plugin_ui_ext')
    assert code == 0, f'the plugin_ui_logic MAIN fails:\n{out}'
    print(out.strip())
    for index, (before, after) in enumerate(LOGIC_MUTANTS):
        assert logic.count(before) == 1, f'logic mutant anchor moved: {before!r}'
        try:
            code, out = build(root, logic.replace(before, after), f'plugin_ui_ext_m{index}')
        except AssertionError as error:
            # Each one compiles: a mutant only rejected by -Werror proves nothing.
            raise AssertionError(f'logic mutant {index} does not compile: {error}')
        if code == 0:
            raise AssertionError(f'the MAIN accepts the logic mutant {index}: {after!r}')

    files = {path: source(path).replace('\r\n', '\n') for path in FILES}
    for test in CHECKS:
        test(files)
    for test, path, before, after in STATIC_MUTANTS:
        assert files[path].count(before) == 1, f'mutant anchor moved: {before!r}'
        mutated = dict(files)
        mutated[path] = files[path].replace(before, after, 1)
        try:
            test(mutated)
        except (AssertionError, ValueError):
            continue
        raise AssertionError(f'{test.__name__} accepts the mutant: {after!r}')
    print('Plugin UI ext PASS: strict form schema, http(s)-only links through '
          'HiddenUrlClickHandler, labels read from a cache outside the engine, '
          'items, labels and forms dropped on unload, uiExt dispatch, passcode '
          'and service chat guards, async menu clicks and wired client hooks; '
          'no t.me links, links only on a user gesture, compose menu for the '
          'field chat, chatOpened from the main window once per chat')


if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='luxury-plugin-ui-ext-') as directory:
        check(Path(directory))
