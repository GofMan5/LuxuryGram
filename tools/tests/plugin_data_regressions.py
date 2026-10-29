"""Plugin data API regressions: lux.chats, lux.messages, lux.account.

Compiles the Qt-free plugin_payloads.h and the beforeSend fold of
plugin_host_logic.h against the real core sources, and checks the Qt hook
sites statically. Every check has a mutant it must reject.
"""
from pathlib import Path
import json
import os
import re
import subprocess
import tempfile
from library_regressions import CXX, ROOT, STATIC, function, run, source, standalone
from plugin_dx_regressions import build_core, crlf, flags, prelude_members

SRC = 'Telegram/SourceFiles/'
PLUGINS = SRC + 'luxury/plugins/'
LOGIC = PLUGINS + 'plugin_host_logic.h'
PAYLOADS = PLUGINS + 'plugin_payloads.h'
HOOKS = PLUGINS + 'plugin_hooks.cpp'
HOST = PLUGINS + 'plugin_host_api.cpp'
DATA = PLUGINS + 'plugin_host_ext_data.cpp'
ENGINE = PLUGINS + 'js/js_engine.cpp'
SESSION = SRC + 'data/data_session.cpp'
UPDATES = SRC + 'api/api_updates.cpp'
PHOTO = SRC + 'data/data_photo.cpp'
EXAMPLE = 'docs/plugins/examples/auto-moderate'

MAIN = r'''
#include "luxury/plugins/plugin_payloads.h"

#include <cstdio>

using namespace Luxury::Plugins;
using json = nlohmann::json;

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL %d: %s\n", __LINE__, #x); ++failures; } } while (0)

int main() {
	CHECK(DataPermission("messages.delete") == Permission::MessagesManage);
	CHECK(DataPermission("messages.edit") == Permission::MessagesManage);
	CHECK(DataPermission("messages.forward") == Permission::MessagesManage);
	CHECK(DataPermission("messages.markRead") == Permission::MessagesManage);
	CHECK(DataPermission("messages.get") == Permission::MessagesRead);
	CHECK(DataPermission("messages.history") == Permission::MessagesRead);
	CHECK(DataPermission("messages.downloadMedia") == Permission::MessagesRead);
	CHECK(DataPermission("chats.list") == Permission::MessagesRead);
	CHECK(DataPermission("chats.get") == Permission::MessagesRead);
	CHECK(DataPermission("account.me") == Permission::MessagesRead);
	CHECK(DataPermission("messages.sendFile") == Permission::MessagesSend);
	CHECK(DataPermission("ui.notify") == Permission::Ui);
	CHECK(!DataPermission("settings.set"));
	CHECK(IsDataFunction("settings.set"));
	CHECK(!IsDataFunction("messages.send"));
	CHECK(!IsDataFunction("storage.get"));

	CHECK(DataEventPermission("messageEdited") == Permission::MessagesRead);
	CHECK(DataEventPermission("messageDeleted") == Permission::MessagesRead);
	CHECK(DataEventPermission("chatRead") == Permission::MessagesRead);
	CHECK(DataEventPermission("accountChanged") == Permission::MessagesRead);
	CHECK(!DataEventPermission("message"));
	CHECK(KnownEvent("messageEdited") && KnownEvent("chatRead"));

	CHECK(!ChatVisible(777000));
	CHECK(!ChatVisible(0));
	CHECK(ChatVisible(-100123));
	CHECK(ChatVisible(42));
	CHECK(!ChatVisible((std::int64_t(1) << 48) + 777000));

	CHECK(LimitArg(json(), 20, 100) == 20);
	CHECK(LimitArg(json("5"), 20, 100) == 20);
	CHECK(LimitArg(json(5), 20, 100) == 5);
	CHECK(LimitArg(json(0), 20, 100) == 1);
	CHECK(LimitArg(json(1000), 20, 100) == 100);

	CHECK(MessageIdArg(json("12")) == 12);
	CHECK(MessageIdArg(json(12)) == 12);
	CHECK(!MessageIdArg(json("x")));
	CHECK(!MessageIdArg(json("12x")));
	CHECK(!MessageIdArg(json(0)));
	CHECK(!MessageIdArg(json(-3)));
	CHECK(MessageIdArg(json(2147483647)) == 2147483647);
	CHECK(MessageIdArg(json("2147483647")) == 2147483647);
	CHECK(!MessageIdArg(json(std::int64_t(2147483648))));
	CHECK(!MessageIdArg(json("2147483648")));
	CHECK(!MessageIdArg(json(std::int64_t(1) << 56)));
	CHECK(!MessageIdsArg(json::array({ 1, std::int64_t(1) << 40 })));
	CHECK(MessageIdsArg(json("7"))->size() == 1);
	CHECK(MessageIdsArg(json::array({ 1, "2" }))->size() == 2);
	CHECK(!MessageIdsArg(json::array()));
	CHECK(!MessageIdsArg(json::array({ 1, "x" })));
	auto many = json::array();
	for (auto i = 1; i <= 101; ++i) {
		many.push_back(i);
	}
	CHECK(!MessageIdsArg(many));
	many.erase(many.size() - 1);
	CHECK(MessageIdsArg(many)->size() == 100);

	const auto entities = ParseEntities(json::parse(R"([
		{"type": "italic", "offset": 4, "length": 2},
		{"type": "bold", "offset": 0, "length": 3},
		{"type": "mentionName", "offset": 0, "length": 2, "userId": "777000"},
		{"type": "mentionName", "offset": 0, "length": 2, "userId": 777000},
		{"type": "mentionName", "offset": 1, "length": 2, "userId": "42"},
		{"type": "bold", "offset": 5, "length": 9},
		{"type": "nonsense", "offset": 0, "length": 1},
		{"type": "textUrl", "offset": 0, "length": 1},
		"junk"
	])"), 8);
	CHECK(entities.size() == 3);
	CHECK(entities.size() == 3 && entities[0].type == EntityTypeByName("bold"));
	CHECK(entities.size() == 3 && entities[1].type == kEntityMentionName
		&& entities[1].data == "42");
	CHECK(entities.size() == 3 && entities[2].offset == 4);
	CHECK(ParseEntities(json::object(), 8).empty());

	CHECK(EntityJson({ .type = kEntityMentionName, .offset = 0, .length = 1, .data = "777000" }).is_null());
	const auto mention = EntityJson({ .type = kEntityMentionName, .offset = 0, .length = 1, .data = "42" });
	CHECK(mention.value("type", "") == "mentionName" && mention.value("userId", "") == "42");
	CHECK(EntityJson({ .type = kEntityTextUrl, .offset = 1, .length = 2, .data = "https://a" })
		.value("url", "") == "https://a");
	CHECK(EntityJson({ .type = 0, .offset = 0, .length = 1 }).is_null());

	CHECK(Utf16Length("abc") == 3);
	CHECK(Utf16Length("\xD0\xBF") == 1);
	CHECK(Utf16Length("\xF0\x9F\x98\x80") == 2);

	{
		auto fold = SendFold{ .text = "hi", .entities = json::array({ json::object() }) };
		FoldBeforeSend(fold, json{ { "replyTo", "12" } });
		CHECK(fold.changed && fold.replyChanged && fold.replyTo == "12");
		CHECK(!fold.entitiesChanged && fold.entities.size() == 1 && fold.text == "hi");
	}
	{
		auto fold = SendFold{ .text = "hi", .entities = json::array({ json::object() }) };
		FoldBeforeSend(fold, json("HI"));
		CHECK(fold.text == "HI" && fold.entities.empty() && fold.entitiesChanged);
		CHECK(!fold.replyChanged);
	}
	{
		auto fold = SendFold{ .text = "hi" };
		FoldBeforeSend(fold, json{ { "text", "hey" }, { "entities", json::array({ json{ { "type", "bold" } } }) } });
		CHECK(fold.text == "hey" && fold.entities.size() == 1 && fold.entitiesChanged);
	}
	{
		auto fold = SendFold{ .text = "hi" };
		FoldBeforeSend(fold, json(false));
		FoldBeforeSend(fold, json("later"));
		CHECK(fold.cancelled && fold.text == "hi");
	}
	{
		auto fold = SendFold{ .text = "hi" };
		FoldBeforeSend(fold, json());
		FoldBeforeSend(fold, json(7));
		CHECK(!fold.changed);
	}
	if (failures) {
		return 1;
	}
	std::printf("plugin data fixture ok\n");
	return 0;
}
'''

# (path, before, after): each one breaks a compiled assertion.
LOGIC_MUTANTS = (
    (PAYLOADS, 'return IsBareDialogId(dialogId) && (dialogId != kServiceUserId);',
     'return IsBareDialogId(dialogId);'),
    (PAYLOADS, '|| function == "messages.delete"\n', ''),
    (PAYLOADS, 'if (event == "messageEdited"\n\t\t|| ', 'if ('),
    (PAYLOADS, '&& !IsServiceUserId(nlohmann::json(entity.data));', ';'),
    (PAYLOADS, '|| IsServiceUserId(nlohmann::json(entity.data))))) {', '))) {'),
    (PAYLOADS, 'value.size() > std::size_t(kMaxHistoryLimit)', 'value.size() > std::size_t(kMaxHistoryLimit + 1)'),
    (PAYLOADS, 'return a.offset < b.offset;', 'return a.offset > b.offset;'),
    (PAYLOADS, "result += ((byte & 0xF8) == 0xF0) ? 2 : 1;", 'result += 1;'),
    (PAYLOADS, 'return (result > 0 && result <= kMaxMessageId)', 'return (result >= 0 && result <= kMaxMessageId)'),
    (PAYLOADS, 'return (result > 0 && result <= kMaxMessageId)', 'return (result > 0)'),
    (PAYLOADS, 'std::numeric_limits<std::int32_t>::max());', 'std::numeric_limits<std::int64_t>::max());'),
    (LOGIC, 'fold.changed = fold.replyChanged = true;', 'fold.changed = true;'),
    (LOGIC, 'fold.entities = nlohmann::json::array();\n\t\tfold.changed = fold.entitiesChanged = true;',
     'fold.changed = true;'),
)


def fixture(root, tree, objects, texts, name):
    for path, text in texts.items():
        (tree / path[len(SRC):]).write_text(text, encoding='utf-8', newline='\n')
    main = root / (name + '.cpp')
    main.write_text(MAIN, encoding='utf-8')
    exe = root / (name + ('.exe' if os.name == 'nt' else ''))
    run([CXX, '-std=c++20', '-O0', *flags(tree), str(main), *objects.values(), *STATIC, '-o', str(exe)], root)
    standalone(exe)
    result = subprocess.run([str(exe)], cwd=root, capture_output=True, text=True, errors='replace')
    return result.returncode, result.stdout + result.stderr


def branch(text, name):
    start = text.index(f'function == "{name}"')
    end = text.find('} else if (function ==', start)
    return text[start:end if end >= 0 else len(text)]


# --- Qt hook sites, checked by text.
def check_sources(files):
    hooks, data, host = files[HOOKS], files[DATA], files[HOST]
    message = function(hooks, 'json MessageJson(')
    for key in ('"entities"', '"media"', '"forward"', '"topicId"', '"editDate"', '"groupedId"'):
        assert '{ ' + key + ',' in message, f'MessageJson lost {key}'
    send = function(hooks, 'bool BeforeSend(')
    assert '{ "entities", fold.entities }' in send, 'beforeSend does not pass the entities'
    assert '{ "replyTo", fold.replyTo }' in send, 'beforeSend drops replyTo'
    assert 'message.action.replyTo = PluginReply(' in send, 'beforeSend ignores a returned replyTo'
    assert 'ConvertEntitiesToTextTags(' in send, 'beforeSend ignores returned entities'
    assert 'granted.has(Permission::MessagesModify)' in send, 'beforeSend runs without messages.modify'
    emit = function(hooks, 'void EmitMessage(')
    assert 'instance->wants(event)' in emit, 'EmitMessage skips the subscription check'
    assert 'EmitMessage(item, "messageEdited")' in function(hooks, 'void MessageEdited('), \
        'messageEdited bypasses EmitMessage and its filters'
    assert 'EmitMessage(item, "messageDeleted")' in function(hooks, 'void MessagesDeleted('), \
        'messageDeleted bypasses EmitMessage and its filters'
    assert 'instance->wants("chatRead")' in function(hooks, 'void ChatRead('), 'chatRead skips wants'

    needed = function(host, 'std::optional<Permission> Needed(')
    assert needed.index('DataPermission(function)') < needed.index('starts("ui.")'), \
        'Needed() does not ask DataPermission first'
    events = function(host, 'std::optional<Permission> EventPermission(')
    assert 'DataEventPermission(event)' in events, 'EventPermission skips the data events'
    assert 'if (delta > 0 && !_plugin.granted.has(*needed))' in function(host, 'Js::HostResult Instance::subscribe('), \
        'subscribe does not check the event permission'
    call = host[host.index('else if (const auto needed = Needed(function))'):]
    assert call.index('Needed(function)') < call.index('IsDataFunction(function)'), \
        'data functions are dispatched before the permission check'

    remove = branch(data, 'messages.delete')
    assert 'if (!_plugin.granted.has(Permission::MessagesManage)) {' in remove \
        and 'return PermissionError(Permission::MessagesManage);' in remove, \
        'messages.delete does not check messages.manage itself'
    assert 'ChatVisible(getDialogIdFromPeer(history->peer))' in branch(data, 'chats.list'), \
        'chats.list lets the service chat through'
    peer = function(data, 'PeerData *PeerArg(')
    assert 'IsServiceUserId(value)' in peer and 'ResolvePeer(session' in peer, 'PeerArg lets 777000 through'
    assert 'IsSafeRelativePath(path)' in branch(data, 'messages.sendFile'), 'sendFile reads any path'
    assert 'item->forbidsSaving()' in branch(data, 'messages.downloadMedia'), \
        'downloadMedia ignores chats that restrict saving'
    # Neither waits for the upload or the forward, so the in-flight cap
    # cannot hold them: both share one rate limit (ToastAllowed, tested by
    # plugin_host), checked before sending.
    for name, call in (('messages.sendFile', 'session->api().sendFile('),
                       ('messages.forward', 'session->api().forwardMessages(')):
        body = branch(data, name)
        assert body.index('if (!ToastAllowed(now, _lastMediaSend)) {') \
            < body.index('_lastMediaSend = now;') < body.index(call), f'{name} is not rate limited'
    # A refused sendFile reads nothing.
    send_file = branch(data, 'messages.sendFile')
    assert send_file.index('if (!ToastAllowed(now, _lastMediaSend)) {') \
        < send_file.index('auto bytes = QByteArray();'), 'sendFile reads the file before the rate limit'
    # replyTo is a message id: a dialog id would let 2^32 + 5 reply to 5.
    reply = function(host, 'Js::HostResult Instance::send(')
    assert 'const auto replyTo = MessageIdArg(options.value("replyTo", json()));' in reply \
        and 'if (replyTo) {' in reply, 'messages.send reads replyTo as a dialog id'
    # A cancelled or failed load and an account switch settle at once;
    # a photo cancel fires nothing else, so PhotoData::cancel reports it.
    cancel = function(files[PHOTO], 'void PhotoData::cancel()')
    assert 'loader->cancel();' in cancel \
        and cancel.index('loader->cancel();') < cancel.find('_owner->photoLoadProgress(this);'), \
        'a cancelled photo download waits out the timeout'
    download = branch(data, 'messages.downloadMedia')
    assert 'session->data().documentLoadProgress(\n' in download \
        and 'session->data().photoLoadProgress(\n' in download, 'downloadMedia waits out a failed load'
    assert '|| photo->cancelled()) {' in download, 'downloadMedia waits out a cancelled photo'
    assert 'session->data().sessionDataAboutToBeCleared(),' in download \
        and 'Core::App().domain().activeSessionChanges() | rpl::to_empty' in download, \
        'downloadMedia outlives the account'
    me = branch(data, 'account.me')
    assert '"phone"' not in me and 'phone()' not in me, 'account.me leaks the phone'

    assert 'Luxury::Plugins::MessageEdited(' in files[SESSION], 'no messageEdited hook in Data::Session'
    assert files[SESSION].count('Luxury::Plugins::MessagesDeleted(') == 2, 'messageDeleted hook missing'
    assert files[UPDATES].count('Luxury::Plugins::ChatRead(') == 2, 'chatRead hook missing'

    engine = files[ENGINE]
    assert "'beforeSend handlers run synchronously:'" in engine \
        and "} else if (result && typeof result.then === 'function') {" in engine, 'no warning for a thenable beforeSend'
    assert "{ $bin: bytes.toBase64() }" in engine and 'Uint8Array.fromBase64(r.$bin).buffer' in engine, \
        'the ArrayBuffer codec is gone'
    assert "if (object && ('replyTo' in result))" in engine, 'the prelude drops a returned replyTo'


SOURCE_MUTANTS = (
    (HOOKS, '\t\t{ "entities", EntitiesJson(text.entities) },\n', ''),
    (HOOKS, '\t\t\t\t{ "replyTo", fold.replyTo },\n', ''),
    (HOOKS, 'message.action.replyTo = PluginReply(', 'const auto unused = PluginReply('),
    (DATA, 'if (!_plugin.granted.has(Permission::MessagesManage)) {', 'if (false) {'),
    (DATA, '!ChatVisible(getDialogIdFromPeer(history->peer))', '!history->peer'),
    (HOST, 'if (const auto data = DataEventPermission(event)) {\n\t\treturn data;\n\t} else ', ''),
    (HOOKS, 'EmitMessage(item, "messageEdited");', 'Emit(item, "messageEdited");'),
    (DATA, 'if (!IsSafeRelativePath(path)) {', 'if (path.empty()) {'),
    (DATA, '{ "premium", user->isPremium() },', '{ "premium", user->isPremium() }, { "phone", user->phone() },'),
    (ENGINE, "} else if (result && typeof result.then === 'function') {", '} else if (false) {'),
    (ENGINE, "if (object && ('replyTo' in result)) {", 'if (false) {'),
    (DATA, '\t\t\tif (!ToastAllowed(now, _lastMediaSend)) {\n\t\t\t\treturn RateLimited("messages.forward");\n\t\t\t}\n', ''),
    (DATA, '\t\tif (!ToastAllowed(now, _lastMediaSend)) {\n\t\t\treturn RateLimited("messages.sendFile");\n\t\t}\n', ''),
    (DATA, '\t\t_lastMediaSend = now;\n\t\tauto action', '\t\tauto action'),
    # The sendFile rate limit back after the 10 MiB read.
    (DATA, ('\t\tconst auto now = crl::now();\n\t\tif (!ToastAllowed(now, _lastMediaSend)) {\n'
            '\t\t\treturn RateLimited("messages.sendFile");\n\t\t}\n\t\tauto bytes',
            '\t\t_lastMediaSend = now;\n\t\tauto action'),
           ('\t\tauto bytes',
            '\t\tconst auto now = crl::now();\n\t\tif (!ToastAllowed(now, _lastMediaSend)) {\n'
            '\t\t\treturn RateLimited("messages.sendFile");\n\t\t}\n\t\t_lastMediaSend = now;\n\t\tauto action')),
    (HOST, 'const auto replyTo = MessageIdArg(options.value(', 'const auto replyTo = DialogId(options.value('),
    (HOST, 'if (replyTo) {', 'if (replyTo && *replyTo > 0) {'),
    (PHOTO, '\t\t_owner->photoLoadProgress(this);\n\t}\n}', '\t}\n}'),
    (DATA, '\t\t\tsession->data().documentLoadProgress(\n', '\t\t\trpl::never<not_null<DocumentData*>>(\n'),
    (DATA, '\t\t\tsession->data().photoLoadProgress(\n', '\t\t\trpl::never<not_null<PhotoData*>>(\n'),
    (DATA, 'Large)\n\t\t\t\t\t|| photo->cancelled()) {', 'Large)) {'),
    (DATA, '\t\t\tsession->data().sessionDataAboutToBeCleared(),\n', ''),
)


def check_example(read):
    manifest = json.loads(read(EXAMPLE + '/plugin.json'))
    assert 'messages.manage' in manifest['permissions'], 'the example does not ask for messages.manage'
    script = read(EXAMPLE + '/main.js')
    members = prelude_members(crlf(read(ENGINE)))
    code = '\n'.join(line for line in script.splitlines() if not line.lstrip().startswith('//'))
    used = set(re.findall(r'\blux\.(\w+(?:\.\w+)?)', code))
    # lux.plugin is a getter over plugin.info: its fields are not literal members.
    unknown = sorted(name for name in used if name not in members and name.split('.')[0] != 'plugin')
    assert not unknown, f'the example uses lux members the prelude lacks: {unknown}'
    for name in ('messages.edit', 'messages.remove', 'ui.notify'):
        assert name in used, f'the example does not use lux.{name}'
    assert "lux.on('messageEdited'" in script, 'the example ignores edits'
    assert 'api.call' not in script, 'the example goes around the typed API'


EXAMPLE_MUTANTS = (
    (EXAMPLE + '/main.js', 'lux.messages.remove(', 'lux.messages.purge('),
    (EXAMPLE + '/plugin.json', '"messages.manage", ', ''),
)


def check(root, read=source):
    files = {path: crlf(read(path)) for path in (HOOKS, HOST, DATA, ENGINE, SESSION, UPDATES, PHOTO)}
    check_sources(files)
    for path, before, after in SOURCE_MUTANTS:
        # Tuples edit several sites at once, as a moved check needs.
        pairs = zip(before, after) if isinstance(before, tuple) else [(before, after)]
        mutated = dict(files)
        for one, other in pairs:
            assert files[path].count(one) == 1, f'source mutant anchor moved: {one!r}'
            mutated[path] = mutated[path].replace(one, other)
        try:
            check_sources(mutated)
        except (AssertionError, ValueError):
            continue
        raise AssertionError(f'the source check accepts the mutant: {after!r} for {before!r}')

    check_example(read)
    for path, before, after in EXAMPLE_MUTANTS:
        assert read(path).count(before) == 1, f'example mutant anchor moved: {before!r}'
        mutated = lambda p: read(p).replace(before, after) if p == path else read(p)
        try:
            check_example(mutated)
        except AssertionError:
            continue
        raise AssertionError(f'the example check accepts the mutant: {after!r}')

    tree, objects = build_core(root, read)
    texts = {LOGIC: crlf(read(LOGIC)), PAYLOADS: crlf(read(PAYLOADS))}
    code, out = fixture(root, tree, objects, texts, 'plugin_data')
    assert code == 0 and out.strip() == 'plugin data fixture ok', out
    for index, (path, before, after) in enumerate(LOGIC_MUTANTS):
        assert texts[path].count(before) == 1, f'logic mutant anchor moved: {before!r}'
        mutated = dict(texts)
        mutated[path] = texts[path].replace(before, after)
        try:
            code, out = fixture(root, tree, objects, mutated, f'plugin_data_l{index}')
        except AssertionError as error:
            raise AssertionError(f'logic mutant {index} does not compile: {error}')
        if code == 0:
            raise AssertionError(f'the data fixture accepts the logic mutant: {after!r} for {before!r}')
    print('Plugin data PASS: data permissions (messages.manage for edit/delete/forward/markRead), '
          'data events behind messages.read, 777000 hidden from chats.list, peers and entities, '
          'entity round trip, id and limit arguments, beforeSend entities and replyTo, '
          'thenable warning, ArrayBuffer codec, hook sites, the auto-moderate example')


if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='luxury-plugin-data-') as directory:
        check(Path(directory))
