"""TL <-> JSON codec of the plugin Telegram API bridge.

Plugins call any API method as JSON (docs/plugins/ARCHITECTURE.md, "Telegram
API bridge"); luxury/plugins/core/tl_schema + tl_codec turn it into the same
words tdesktop's generated serializer writes, and decode answers back.

The real api.tl / mtproto.tl are parsed by the real C++ sources, built with
-Wall -Wextra -Werror (and once more with -fsanitize=undefined when the
toolchain has it). Requests are checked against an independent TL serializer
written below in Python: byte-exact padding, boxed vectors, flags computed
from present fields, `true` flags without payload, flags2, !X wrappers.
Decoding is checked on Python-built buffers (gzip_packed included), on
round trips, on JSON error paths, and by a fuzz loop over random, truncated
and bit-flipped buffers that must fail cleanly, never crash.
"""
import base64
import json
import random
import re
import struct
import tempfile
from pathlib import Path

from library_regressions import CXX, ROOT, run, source

SCHEMA = 'Telegram/SourceFiles/luxury/plugins/core/tl_schema.cpp'
CODEC = 'Telegram/SourceFiles/luxury/plugins/core/tl_codec.cpp'
API = 'Telegram/SourceFiles/mtproto/scheme/api.tl'
MTPROTO = 'Telegram/SourceFiles/mtproto/scheme/mtproto.tl'

DRIVER = r'''
#include "luxury/plugins/core/tl_codec.h"
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
using namespace Luxury::Plugins::Tl;
using nlohmann::json;

std::string Read(const char *path) {
	std::ifstream f(path, std::ios::binary);
	std::stringstream s;
	s << f.rdbuf();
	return s.str();
}

template <typename T>
json Out(const Result<T> &r) {
	return r ? json{ { "ok", *r.value } } : json{ { "error", r.error } };
}

int main(int, char **argv) {
	const auto api = Read(argv[1]);
	auto parsedApi = Schema::Parse(api);
	auto parsedMt = Schema::Parse(Read(argv[2]));
	if (!parsedApi || !parsedMt) {
		std::cerr << parsedApi.error << parsedMt.error << "\n";
		return 1;
	}
	const auto &schema = *parsedApi.value;
	const auto &mtproto = *parsedMt.value;
	const Inflate inflate = [](std::string_view data) -> std::optional<std::string> {
		if (data.starts_with("corrupt")) {
			return std::nullopt;
		}
		return std::string(data); // Identity: the test packs raw words.
	};
	auto out = json::object();
	auto methods = 0, constructors = 0;
	for (const auto &c : schema.combinators()) {
		++(c.function ? methods : constructors);
	}
	out["methods"] = methods;
	out["constructors"] = constructors;
	out["types"] = schema.typesCount();
	out["mtproto"] = mtproto.combinators().size();
	auto ids = json::object();
	for (const auto &c : schema.combinators()) {
		ids[c.name] = c.id;
	}
	for (const auto &c : mtproto.combinators()) {
		ids["mtproto:" + c.name] = c.id;
	}
	out["ids"] = ids;
	auto crc = json::object();
	std::istringstream lines(api);
	for (std::string line; std::getline(lines, line);) {
		const auto hash = line.find('#');
		const auto space = line.find(' ');
		if (hash == 0 || hash == line.npos || line.starts_with("//") || hash > space) {
			continue;
		}
		crc[line.substr(0, hash)] = Schema::ComputeId(line);
	}
	for (const auto &line : json::parse(Read(argv[3]))) {
		crc[line.get<std::string>()] = Schema::ComputeId(line.get<std::string>());
	}
	out["crc"] = crc;

	auto results = json::array();
	auto corpus = std::vector<std::pair<std::string, std::vector<uint32_t>>>();
	for (const auto &c : json::parse(Read(argv[4]))) {
		const auto &s = (c.value("schema", "") == "mtproto") ? mtproto : schema;
		const auto op = c["op"].get<std::string>();
		auto r = json::object();
		if (op == "method") {
			const auto e = EncodeMethod(s, c["call"]);
			r = Out(e);
			const auto type = CallReturnType(s, c["call"]);
			r["returns"] = type ? json(*type) : json();
			if (e && type) {
				corpus.emplace_back(*type, *e.value);
			}
		} else if (op == "value") {
			const auto e = EncodeValue(s, c["type"].get<std::string>(), c["value"]);
			r = Out(e);
			if (e) {
				const auto words = *e.value;
				r["result"] = Out(DecodeResult(s, c["type"].get<std::string>(), words.data(), words.size(), inflate));
				r["object"] = Out(DecodeObject(s, words.data(), words.size(), inflate));
				corpus.emplace_back(c["type"].get<std::string>(), words);
			}
		} else if (op == "decode") {
			const auto words = c["words"].get<std::vector<uint32_t>>();
			r = c["type"].is_null()
				? Out(DecodeObject(s, words.data(), words.size(), inflate))
				: Out(DecodeResult(s, c["type"].get<std::string>(), words.data(), words.size(), inflate));
			corpus.emplace_back(c["type"].is_null() ? "" : c["type"].get<std::string>(), words);
		} else if (op == "returns") {
			const auto type = s.MethodReturnType(c["name"].get<std::string>());
			r["ok"] = type ? json(*type) : json();
		}
		results.push_back(r);
	}
	out["results"] = results;

	// Fuzz: none of this may crash, hang or trip UBSan; most of it fails.
	auto random = std::mt19937(20261027);
	auto ids32 = std::vector<uint32_t>{ 0x1cb5c415, 0x3072cfa1, 0x997275b5, 0xbc799737 };
	for (const auto &c : schema.combinators()) {
		ids32.push_back(c.id);
	}
	auto decoded = 0, failed = 0;
	const auto decode = [&](const std::string &type, const std::vector<uint32_t> &w) {
		const auto a = DecodeObject(schema, w.data(), w.size(), inflate);
		const auto b = DecodeResult(schema, type.empty() ? "Updates" : type, w.data(), w.size(), inflate);
		(a ? decoded : failed) += 1;
		(b ? decoded : failed) += 1;
		if (a) {
			(void)a.value->dump(-1, ' ', false, json::error_handler_t::strict);
		}
	};
	for (const auto &[type, words] : corpus) {
		for (auto n = size_t(0); n < words.size(); ++n) {
			decode(type, std::vector<uint32_t>(words.begin(), words.begin() + n));
		}
		for (auto k = 0; k != 40 && !words.empty(); ++k) {
			auto copy = words;
			copy[random() % copy.size()] ^= (1U << (random() % 32));
			if (random() % 3 == 0) {
				copy[random() % copy.size()] = random() % 300;
			}
			decode(type, copy);
		}
	}
	for (auto k = 0; k != 4000; ++k) {
		auto w = std::vector<uint32_t>(random() % 48);
		for (auto &v : w) {
			const auto pick = random() % 10;
			v = (pick < 5) ? ids32[random() % ids32.size()]
				: (pick < 8) ? (random() % 300)
				: uint32_t(random());
		}
		decode("", w);
	}
	decode("", {});
	decode("Updates", {});
	out["fuzz"] = { { "decoded", decoded }, { "failed", failed } };
	std::ofstream(argv[5], std::ios::binary) << out.dump(1);
	std::cout << "TL driver done\n";
}
'''


def tl_table(text):
    """name -> (id, [(param, type)], result), types and functions apart."""
    tables, section = ({}, {}), 0
    for raw in text.splitlines():
        line = raw.split('//')[0].strip()
        if line in ('---types---', '---functions---'):
            section = line == '---functions---'
            continue
        found = re.match(r'([a-zA-Z0-9_.]+)#([0-9a-f]+)((?: [^=]*)?)= ([^;]+);$', line)
        if found:
            params = [p.split(':', 1) for p in found[3].split() if not p.startswith('{')]
            tables[section][found[1]] = (int(found[2], 16), params, found[4].strip())
    return tables


def tl_bytes(data):
    head = bytes([len(data)]) if len(data) < 254 else b'\xfe' + struct.pack('<I', len(data))[:3]
    body = head + data
    return body + b'\0' * (-len(body) % 4)


class Writer:
    """Independent TL serializer, straight from the TL binary spec."""

    def __init__(self, tables):
        self.types, self.functions = tables

    def object(self, table, value):
        cid, params, _ = table[value['_']]
        flags = {}
        for name, kind in params:
            if '?' in kind:
                cond, inner = kind.split('?')
                field, bit = cond.split('.')
                x = value.get(name)
                if (x is True) if inner == 'true' else (x is not None):
                    flags[field] = flags.get(field, 0) | 1 << int(bit)
        out = struct.pack('<I', cid)
        for name, kind in params:
            if kind == '#':
                out += struct.pack('<I', flags.get(name, 0))
                continue
            if '?' in kind:
                cond, kind = kind.split('?')
                field, bit = cond.split('.')
                if kind == 'true' or not flags.get(field, 0) >> int(bit) & 1:
                    continue
            out += self.value(kind, value[name])
        return out

    def value(self, kind, x):
        if kind == 'int':
            return struct.pack('<i', x)
        if kind == 'long':
            return struct.pack('<Q', int(x) % 2**64)
        if kind == 'double':
            return struct.pack('<d', x)
        if kind == 'string':
            return tl_bytes(x.encode())
        if kind == 'bytes':
            return tl_bytes(base64.b64decode(x))
        if kind in ('int128', 'int256'):
            return bytes.fromhex(x)
        if kind == 'Bool':
            return struct.pack('<I', 0x997275b5 if x else 0xbc799737)
        if kind == '!X':
            return self.object(self.functions, x)
        if kind.startswith('Vector<'):
            return struct.pack('<II', 0x1cb5c415, len(x)) + b''.join(self.value(kind[7:-1], i) for i in x)
        return self.object(self.types, x)

    def method(self, call):
        return self.object(self.functions, call)


def words(data):
    assert len(data) % 4 == 0, len(data)
    return list(struct.unpack(f'<{len(data) // 4}I', data))


def b64(data):
    return base64.b64encode(data).decode()


PEER = {'_': 'inputPeerUser', 'user_id': '123456789012', 'access_hash': '-5'}
SEND = {
    '_': 'messages.sendMessage', 'no_webpage': True, 'silent': False, 'peer': PEER,
    'reply_to': {
        '_': 'inputReplyToMessage', 'reply_to_msg_id': 42, 'quote_text': 'q',
        'quote_entities': [{'_': 'messageEntityBold', 'offset': 0, 'length': 1}],
        'poll_option': b64(b'\1\2\3'),
    },
    'message': 'Привет 👋', 'random_id': '18446744073709551615',
    'entities': [{'_': 'messageEntityTextUrl', 'offset': 0, 'length': 6, 'url': 'https://t.me'}],
    'schedule_date': 1700000000, 'effect': '-9223372036854775808',
}
WRAPPED = {
    '_': 'invokeWithLayer', 'layer': 232, 'query': {
        '_': 'initConnection', 'api_id': 2040, 'device_model': 'PC', 'system_version': 'Windows 11',
        'app_version': '1.0.21', 'system_lang_code': 'en', 'lang_pack': 'tdesktop', 'lang_code': 'en',
        'proxy': {'_': 'inputClientProxy', 'address': 'proxy.example', 'port': 443},
        'params': {'_': 'jsonObject', 'value': [
            {'_': 'jsonObjectValue', 'key': 'tz', 'value': {'_': 'jsonNumber', 'value': -2.5}},
            {'_': 'jsonObjectValue', 'key': 'a', 'value': {'_': 'jsonArray', 'value': [
                {'_': 'jsonNull'}, {'_': 'jsonBool', 'value': False}, {'_': 'jsonString', 'value': ''}]}},
        ]},
        'query': {'_': 'invokeWithoutUpdates', 'query': {'_': 'help.getConfig'}},
    },
}
MESSAGE = {
    '_': 'message', 'out': True, 'pinned': True, 'offline': True, 'id': 7,
    'peer_id': {'_': 'peerUser', 'user_id': '777000'}, 'date': 1700000001, 'message': 'hi',
    'media': {'_': 'messageMediaEmpty'},
    'entities': [{'_': 'messageEntityBold', 'offset': 0, 'length': 2}],
    'grouped_id': '-1', 'via_business_bot_id': '9007199254740993', 'effect': '5',
    'from_rank': 'admin', 'views': 3, 'forwards': 4,
}
UPDATES = {
    '_': 'updates', 'updates': [
        {'_': 'updateNewMessage', 'message': MESSAGE, 'pts': 10, 'pts_count': 1},
        {'_': 'updateDeleteMessages', 'messages': [1, -2, 3], 'pts': 11, 'pts_count': 1},
    ], 'users': [], 'chats': [], 'date': 1700000002, 'seq': 0,
}
CANONICAL_MESSAGE = {
    **{k: v for k, v in MESSAGE.items() if k not in ('out', 'pinned', 'offline')},
    'out': True, 'pinned': True, 'offline': True,
}


def error(call, path):
    return dict(op='method', call=call, expect=path)


def cases(writer, mt_writer):
    out = []
    golden = [
        SEND,
        {'_': 'messages.sendMessage', 'peer': {'_': 'inputPeerSelf'}, 'message': '', 'random_id': 7},
        {'_': 'messages.sendMessage', 'peer': PEER, 'message': 'x' * 300, 'random_id': -1, 'silent': True},
        WRAPPED,
        {'_': 'messages.deleteMessages', 'revoke': True, 'id': [1, 2, -3]},
        {'_': 'messages.deleteMessages', 'revoke': False, 'id': []},
        {'_': 'messages.getHistory', 'peer': PEER, 'offset_id': 0, 'offset_date': 0, 'add_offset': -20,
         'limit': 100, 'max_id': 0, 'min_id': 0, 'hash': 0},
        {'_': 'phone.joinGroupCall', 'muted': True, 'call': {'_': 'inputGroupCall', 'id': '1', 'access_hash': '2'},
         'join_as': {'_': 'inputPeerSelf'}, 'public_key': '00112233445566778899aabbccddeeff' * 2,
         'block': b64(b'\xff' * 254), 'params': {'_': 'dataJSON', 'data': '{}'}},
    ]
    golden += [
        {'_': 'upload.saveFilePart', 'file_id': '-42', 'file_part': n, 'bytes': b64(bytes(i % 256 for i in range(n)))}
        for n in (0, 1, 2, 3, 4, 5, 253, 254, 255, 300, 70000)
    ]
    for call in golden:
        out.append(dict(op='method', call=call, golden=words(writer.method(call))))
    out.append(dict(op='value', schema='mtproto', type='ResPQ', value={
        '_': 'resPQ', 'nonce': '0f' * 15 + 'a0', 'server_nonce': 'ff' * 16, 'pq': '\x17\xed',
        'server_public_key_fingerprints': ['-1', '1'],
    }))
    out.append(dict(op='value', type='Updates', value=UPDATES,
                    golden=words(writer.value('Updates', UPDATES))))
    out.append(dict(op='value', type='Vector<long>', value=['1', -1, 2**63, '-9223372036854775808'],
                    canonical=['1', '-1', '-9223372036854775808', '-9223372036854775808']))
    out.append(dict(op='value', type='Message', value={**MESSAGE, 'grouped_id': -1, 'silent': False},
                    canonical=CANONICAL_MESSAGE))

    updates = writer.value('Updates', UPDATES)
    packed = struct.pack('<I', 0x3072cfa1) + tl_bytes(updates)
    expected = {**UPDATES, 'updates': [{**UPDATES['updates'][0], 'message': CANONICAL_MESSAGE}, UPDATES['updates'][1]]}
    out.append(dict(op='decode', type='Updates', words=words(updates), canonical=expected))
    out.append(dict(op='decode', type=None, words=words(packed), canonical=expected))
    out.append(dict(op='decode', type='Vector<long>', words=words(writer.value('Vector<long>', ['1', '-2'])),
                    canonical=['1', '-2']))
    out.append(dict(op='decode', type='messages.AffectedMessages',
                    words=words(struct.pack('<I', 0x3072cfa1) + tl_bytes(struct.pack('<III', 0x84d19185, 5, 1))),
                    canonical={'_': 'messages.affectedMessages', 'pts': 5, 'pts_count': 1}))
    out.append(dict(op='decode', type='Bool', words=[0x997275b5], canonical=True))
    peer = words(writer.value('InputPeer', PEER))
    for type_, data, message in (
        ('Updates', peer, '"inputPeerUser" is not a Updates'),
        ('InputPeer', peer + [0], '1 words left over'),
        ('InputPeer', peer[:-1], 'unexpected end of data'),
        (None, [0xf7444763, 0x1cb5c415, 1] * 40 + [0x3f6d7b68], 'nesting deeper than 64'),
        ('Vector<int>', [0x1cb5c415, 0xffffffff], 'longer than the data'),
        ('Vector<true>', [0x1cb5c415, 0x10000000], 'longer than the data'),
        ('Updates', words(struct.pack('<I', 0x3072cfa1) + tl_bytes(b'corrupt!')), 'corrupt gzip_packed'),
        ('Updates', words(struct.pack('<I', 0x3072cfa1) + tl_bytes(b'abc')), 'corrupt gzip_packed'),
        ('Updates', words(struct.pack('<I', 0x3072cfa1) + tl_bytes(b'')), 'corrupt gzip_packed'),
        ('Bool', [0x12345678], 'expected Bool'),
        ('string', [0xffffff], 'bad string length marker'),
        ('string', [0x000100fe], 'string is longer than the data'),
        (None, [0xdeadbeef], 'unknown constructor 0xdeadbeef'),
    ):
        out.append(dict(op='decode', type=type_, words=data, expect=message))

    bad_peer = {**SEND, 'peer': {'_': 'inputPeerX'}}
    for call, message in (
        (bad_peer, 'peer._: unknown constructor "inputPeerX"'),
        ({**SEND, 'peer': {'_': 'peerUser', 'user_id': '1'}}, 'peer._: "peerUser" is not a InputPeer'),
        ({**SEND, 'entities': [SEND['entities'][0], {'_': 'messageEntityBold', 'offset': 1}]},
         'entities[1].length: missing field of messageEntityBold'),
        ({**SEND, 'bogus': 1}, 'bogus: unknown field of messages.sendMessage'),
        ({**SEND, 'random_id': 'abc'}, 'random_id: expected a long'),
        ({**SEND, 'random_id': '18446744073709551616'}, 'random_id: expected a long'),
        ({**SEND, 'schedule_date': 2**31}, 'schedule_date: expected a 32-bit int'),
        ({**SEND, 'silent': 1}, 'silent: expected true or false'),
        ({**SEND, 'reply_to': {**SEND['reply_to'], 'poll_option': '!!!'}}, 'reply_to.poll_option:'),
        ({**WRAPPED, 'query': {**WRAPPED['query'], 'query': {'_': 'help.getConfigX'}}},
         'query.query._: unknown method "help.getConfigX"'),
        ({'_': 'phone.joinGroupCall', 'call': {'_': 'inputGroupCall', 'id': '1', 'access_hash': '2'},
          'join_as': {'_': 'inputPeerSelf'}, 'public_key': 'ab', 'params': {'_': 'dataJSON', 'data': ''}},
         'public_key:'),
        ({'_': 'phone.joinGroupCall', 'call': {'_': 'inputGroupCall', 'id': '1', 'access_hash': '2'},
          'join_as': {'_': 'inputPeerSelf'}, 'public_key': '00' * 32, 'params': {'_': 'dataJSON', 'data': ''}},
         'block: required by flags.3 set by another field'),
        ({'peer': PEER}, '_:'),
    ):
        out.append(error(call, message))
    for name, result in (('messages.sendMessage', 'Updates'), ('messages.receivedQueue', 'Vector<long>'),
                         ('invokeWithLayer', 'X'), ('help.getConfig', 'Config'), ('nope', None)):
        out.append(dict(op='returns', name=name, canonical=result))
    out.append(dict(op='returns', schema='mtproto', name='req_pq_multi', canonical='ResPQ'))
    return out


def build(root, read, extra=()):
    for path in (SCHEMA, CODEC):
        (root / Path(path).name).write_text(read(path), encoding='utf-8', newline='\n')
    (root / 'driver.cpp').write_text(DRIVER, encoding='utf-8', newline='\n')
    exe = root / ('driver' + ''.join(extra).replace('-', '_').replace('=', '_') + '.exe')
    run([CXX, '-std=c++20', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-static', *extra,
         '-I', str(ROOT / 'Telegram/SourceFiles'),
         str(root / 'driver.cpp'), str(root / 'tl_schema.cpp'), str(root / 'tl_codec.cpp'), '-o', str(exe)], root)
    return exe


def verify(out, todo, api_text):
    declared = dict(re.findall(r'^([a-zA-Z0-9_.]+)#([0-9a-f]+) ', api_text, re.M))
    assert out['methods'] == 859 and out['constructors'] == 1719 and out['types'] == 658, \
        (out['methods'], out['constructors'], out['types'])
    for name in ('messages.sendMessage', 'inputPeerUser', 'updateNewMessage', 'message', 'invokeWithLayer'):
        assert out['ids'][name] == int(declared[name], 16), name
    assert out['ids']['messages.sendMessage'] == 0xfef48f62
    assert out['ids']['mtproto:resPQ'] == 0x05162463 and out['mtproto'] > 50, out['mtproto']
    wrong = [n for n, v in declared.items() if out['crc'][n] != int(v, 16)]
    assert not wrong and len(declared) == 2579, (wrong[:5], len(declared))
    # Id-less lines get the same CRC: the real ids, recomputed.
    assert out['crc']['boolTrue = Bool;'] == 0x997275b5
    assert out['crc']['messages.deleteMessages flags:# revoke:flags.0?true id:Vector<int> '
                      '= messages.AffectedMessages;'] == 0xe58e95d2
    assert out['crc']['upload.saveFilePart file_id:long file_part:int bytes:bytes = Bool;'] == 0xb304a621

    for case, result in zip(todo, out['results']):
        label = json.dumps(case.get('call') or case.get('type') or case.get('name'), ensure_ascii=False)[:90]
        if 'expect' in case:
            assert 'error' in result and case['expect'] in result['error'], (label, result)
            continue
        if case['op'] == 'returns':
            assert result['ok'] == case['canonical'], (label, result)
            continue
        assert 'ok' in result, (label, result)
        if 'golden' in case:
            got, want = result['ok'], case['golden']
            at = next((i for i, (a, b) in enumerate(zip(got, want)) if a != b), min(len(got), len(want)))
            assert got == want, (label, at, len(got), len(want), got[at:at + 6], want[at:at + 6])
        if case['op'] == 'method':
            continue
        if case['op'] == 'decode':
            assert result['ok'] == case['canonical'], (label, result)
            continue
        canonical = case.get('canonical', case['value'])
        assert result['result'] == {'ok': canonical}, (label, result['result'])
        if isinstance(canonical, dict):
            assert result['object'] == {'ok': canonical}, (label, result['object'])
    call = out['results'][3]
    assert call['returns'] == 'Config', call['returns']
    fuzz = out['fuzz']
    assert fuzz['failed'] > 10000 and fuzz['decoded'] > 100, fuzz
    return fuzz


def check(root, read=source):
    api_text, mt_text = read(API), read(MTPROTO)
    writer = Writer(tl_table(api_text))
    todo = cases(writer, Writer(tl_table(mt_text)))
    (root / 'api.tl').write_text(api_text, encoding='utf-8', newline='\n')
    (root / 'mtproto.tl').write_text(mt_text, encoding='utf-8', newline='\n')
    (root / 'cases.json').write_text(json.dumps(todo), encoding='utf-8')
    (root / 'lines.json').write_text(json.dumps([
        'boolTrue = Bool;',
        'messages.deleteMessages flags:# revoke:flags.0?true id:Vector<int> = messages.AffectedMessages;',
        'upload.saveFilePart file_id:long file_part:int bytes:bytes = Bool;',
    ]), encoding='utf-8')
    builds = [build(root, read)]
    # MinGW ships no libubsan; trap mode needs no runtime and dies on UB.
    for flags in (('-fsanitize=undefined', '-fno-sanitize-recover=all'),
                  ('-fsanitize=undefined', '-fsanitize-trap=undefined')):
        try:
            builds.append(build(root, read, flags))
            break
        except AssertionError:
            pass  # This toolchain cannot build it; skip silently.
    for exe in builds:
        target = root / 'out.json'
        run([str(exe), *(str(root / n) for n in ('api.tl', 'mtproto.tl', 'lines.json', 'cases.json')),
             str(target)], root)
        fuzz = verify(json.loads(target.read_text(encoding='utf-8')), todo, api_text)
    print(f'Plugin TL codec PASS: api.tl 859 methods/1719 constructors/2579 CRC ids (vector builtin too), '
          f'{len(todo)} golden/round-trip/error cases, fuzz {fuzz["decoded"] + fuzz["failed"]} buffers, '
          f'{"UBSan" if len(builds) > 1 else "no UBSan"}; NOT a live MTProto exchange')


if __name__ == '__main__':
    import os
    os.environ['PATH'] = str(Path(CXX).parent) + os.pathsep + os.environ.get('PATH', '')
    with tempfile.TemporaryDirectory(prefix='luxury-tl-') as temp:
        check(Path(temp))
