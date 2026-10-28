// LuxuryGram plugin API, version 1: the `lux` global, `console` and timers.
//
// Mirrors the prelude (kPrelude) in
// Telegram/SourceFiles/luxury/plugins/js/js_engine.cpp and the host functions
// in plugin_host_api.cpp. tools/tests/plugin_dx_regressions.py fails when a
// lux member exists on one side only.
//
// Use it from an editor without a build step:
//   /// <reference path="./lux.d.ts" />
// at the top of main.js, or copy it next to your sources.
//
// Errors thrown or rejected by the host carry `name`:
//   PermissionError     the permission is not granted, or the call is never
//                       allowed (forbidden method, service chat, Stars)
//   TypeError           a wrong argument
//   RangeError          a limit: handlers, timers, requests in flight,
//                       a response or a file over 10 MiB
//   QuotaExceededError  storage over 5 MiB
//   NetworkError        a failed request or too many redirects
//   Error               no active account or window, the plugin is stopping
// A Telegram API error rejects with name = the error type
// ("FLOOD_WAIT_X", ...) and message = '{"type":"...","code":420}'.

type LuxPermission =
	| 'messages.read'
	| 'messages.send'
	| 'messages.modify'
	| 'messages.manage'
	| 'ui'
	| 'network'
	| 'storage'
	| 'clipboard'
	| 'telegram.api'
	| 'telegram.updates';

type LuxJson =
	| null
	| boolean
	| number
	| string
	| LuxJson[]
	| { [key: string]: LuxJson };

interface LuxPluginInfo {
	/** plugin.json `id`. */
	readonly id: string;
	/** plugin.json `version`. */
	readonly version: string;
	/** The plugin API this client implements: 1. */
	readonly apiVersion: number;
	/** LuxuryGram version, "1.0.22". */
	readonly clientVersion: string;
	/** The version this one replaced on update or rollback, else "". */
	readonly previousVersion: string;
	/** Why this load happened. */
	readonly reason:
		| 'startup' | 'enable' | 'reload' | 'install' | 'update' | 'rollback';
	/** Permission ids the user approved. */
	readonly granted: readonly string[];
	/** UI language id, "en", "ru" or a custom pack id. */
	readonly lang: string;
}

interface LuxPeer {
	/** Dialog id as a decimal string: users > 0, chats and channels < 0. */
	id: string;
	type: 'user' | 'chat' | 'channel';
	title: string;
}

/**
 * A formatting span in UTF-16 units, as String.prototype.length counts.
 * `url` comes with textUrl, `userId` (a decimal string) with mentionName,
 * `emojiId` with customEmoji, `language` with pre.
 */
interface LuxEntity {
	type:
		| 'url' | 'textUrl' | 'email' | 'hashtag' | 'cashtag' | 'mention'
		| 'mentionName' | 'customEmoji' | 'botCommand' | 'mediaTimestamp'
		| 'colorized' | 'phone' | 'bankCard' | 'tonAddress' | 'bold'
		| 'semibold' | 'italic' | 'underline' | 'strikethrough' | 'code'
		| 'pre' | 'blockquote' | 'spoiler' | 'subscript' | 'superscript'
		| 'marked' | 'formattedDate';
	offset: number;
	length: number;
	url?: string;
	userId?: string;
	emojiId?: string;
	language?: string;
}

interface LuxMedia {
	type:
		| 'photo' | 'sticker' | 'voice' | 'round' | 'animation' | 'video'
		| 'audio' | 'file' | 'other';
	/** Bytes, 0 when unknown. */
	size: number;
}

interface LuxForward {
	/** The original sender, null when hidden. */
	from: LuxPeer | null;
	/** The name a hidden sender shows, else null. */
	name: string | null;
	/** Unix time of the original message. */
	date: number;
	/** Id of the original message in its chat, or null. */
	id: string | null;
}

interface LuxMessage {
	/** Message id in its chat, a decimal string. */
	id: string;
	peer: LuxPeer;
	/** null for a message from the Telegram service account. */
	from: LuxPeer | null;
	/** Unix time, seconds. */
	date: number;
	text: string;
	entities: LuxEntity[];
	out: boolean;
	/** Id of the replied message, a decimal string, or null. */
	replyTo: string | null;
	hasMedia: boolean;
	media: LuxMedia | null;
	forward: LuxForward | null;
	/** Topic root id in a forum, a decimal string, else null. */
	topicId: string | null;
	/** Unix time of the last edit, or null. */
	editDate: number | null;
	/** Album id, a decimal string, or null. */
	groupedId: string | null;
}

interface LuxChat extends LuxPeer {
	username: string | null;
	/** 0 until the chat is loaded. */
	unreadCount: number;
}

interface LuxAccount {
	id: string;
	firstName: string;
	lastName: string;
	username: string | null;
	premium: boolean;
}

/** What beforeSend may return besides a string or false. */
interface LuxSendChange {
	/** A new text drops the old entities unless `entities` is given. */
	text?: string;
	entities?: LuxEntity[];
	/** A message id in the same chat, or null for no reply. */
	replyTo?: string | number | null;
}

/** A menu item handler without `messages.read` gets only id and peer. */
type LuxMessageRef = LuxMessage | { id: string; peer: LuxPeer };

interface LuxEvents {
	/** An incoming message. Needs `messages.read`. */
	message: (event: { message: LuxMessage }) => void | Promise<void>;
	/** A message this account sent. Needs `messages.read`. */
	messageOut: (event: { message: LuxMessage }) => void | Promise<void>;
	/** A message was edited. Needs `messages.read`. */
	messageEdited: (event: { message: LuxMessage }) => void | Promise<void>;
	/** A loaded message was deleted. Needs `messages.read`. */
	messageDeleted: (event: { message: LuxMessage }) => void | Promise<void>;
	/** A chat was read up to maxId, here or elsewhere. Needs `messages.read`. */
	chatRead: (event: { peer: LuxPeer; maxId: string; unread: number }) => void | Promise<void>;
	/** The active Telegram account changed. Needs `messages.read`. */
	accountChanged: (event: {}) => void | Promise<void>;
	/**
	 * A message about to be sent, synchronous. Return a string to replace
	 * the text, a LuxSendChange to replace what it names, `false` to cancel
	 * the send, anything else to keep it. Handlers run in subscription
	 * order, each sees what the previous one returned. A returned promise
	 * is ignored with a warning in the log. Needs `messages.modify`.
	 */
	beforeSend: (event: {
		peer: LuxPeer;
		text: string;
		entities: LuxEntity[];
		replyTo: string | null;
	}) => string | LuxSendChange | false | void;
	/**
	 * One raw update in the JSON form of TL, at least once: dedupe by
	 * id/pts. Needs `telegram.updates`.
	 */
	update: (event: { update: { _: string; [key: string]: LuxJson } }) => void | Promise<void>;
	/**
	 * Another chat was opened in the main window (not repeated for its
	 * topics, sections or other windows). Needs `ui`.
	 */
	chatOpened: (event: { peer: LuxPeer }) => void | Promise<void>;
	/** Before the plugin is disabled, updated or removed. */
	unload: () => void;
}

interface LuxSendOptions {
	/** Dialog id: a LuxPeer `id` string or a number. */
	peer: string | number;
	/** Must not be empty. Goes through every plugin's beforeSend. */
	text: string;
	/** Message id in the same chat. */
	replyTo?: string | number;
	silent?: boolean;
}

interface LuxDialogOptions {
	title?: string;
	text?: string;
}

interface LuxPromptOptions {
	title?: string;
	placeholder?: string;
	/** Initial text of the field. */
	value?: string;
}

interface LuxCommandOptions {
	/** [a-z0-9_]{1,32}, a leading "/" is dropped. */
	name: string;
	description?: string;
	/** `/name rest of line` typed in the chat composer; args = "rest of line". */
	handler: (event: { peer: LuxPeer; args: string }) => void | Promise<void>;
}

interface LuxMenuItemOptions {
	/** Not empty. Shown in the message context menu under "Plugins". */
	title: string;
	handler: (event: { peer: LuxPeer; message: LuxMessageRef }) => void | Promise<void>;
}

interface LuxHotkeyOptions {
	/**
	 * One shortcut in Qt portable text: "Ctrl+Shift+K". Ctrl+C/V/X/A/Z/Y,
	 * Ctrl+Enter, Enter and Esc are reserved. Not delivered while a text
	 * field has focus.
	 */
	keys: string;
	/** peer: the chat open in the active window, or null. */
	handler: (event: { peer: LuxPeer | null }) => void | Promise<void>;
}

interface LuxUiItemOptions {
	/** Not empty. Shown under "Plugins"; the same title again replaces it. */
	title: string;
	handler: (event: { peer: LuxPeer }) => void | Promise<void>;
}

/** One row of ui.showForm; `key` is [A-Za-z0-9_]{1,32}, unique. */
type LuxFormField =
	| { type: 'text'; key: string; label?: string; placeholder?: string; default?: string }
	| { type: 'toggle'; key: string; label?: string; default?: boolean }
	| { type: 'select'; key: string; label?: string; options: string[]; default?: string };

interface LuxFormSchema {
	/** Shown after the plugin name. */
	title?: string;
	/** 1 to 20 fields. An unknown type or member throws TypeError. */
	fields: LuxFormField[];
}

interface LuxDecorateOptions {
	/** Dialog id: a LuxPeer `id` string or a number. */
	peer: string | number;
	/** Message id in that chat. */
	id: string | number;
	/** At most 32 characters, one line; empty or omitted removes the label. */
	text?: string;
}

interface LuxFetchOptions {
	/** GET (default), POST, PUT, PATCH, DELETE, HEAD; any case. */
	method?: string;
	headers?: { [name: string]: string };
	body?: string;
	/** Return the body as `base64` instead of `text()`. */
	binary?: boolean;
}

interface LuxResponse {
	status: number;
	/** Lowercase names. */
	headers: { [name: string]: string };
	text(): string;
	json(): any;
	/** The body, base64-encoded, with `binary: true`. */
	base64?: string;
}

declare namespace lux {
	/** Frozen, read on first use. */
	const plugin: LuxPluginInfo;
	/** Same as console.log: a line in the plugin's log in the manager. */
	function log(...args: any[]): void;
	/**
	 * Subscribes to an event; returns the unsubscribe function. Throws
	 * PermissionError when the event needs a permission not granted.
	 */
	function on<E extends keyof LuxEvents>(event: E, fn: LuxEvents[E]): () => void;

	namespace messages {
		/**
		 * Sends text as this account. Resolves to null, rejects with
		 * "a plugin cancelled the message" when a beforeSend handler
		 * cancelled it. Needs `messages.send`.
		 */
		function send(options: LuxSendOptions): Promise<null>;
		/**
		 * A message by id, loading it when needed; null when there is none
		 * or it is a service message. Needs `messages.read`.
		 */
		function get(peer: string | number, id: string | number): LuxMessage | null | Promise<LuxMessage | null>;
		/**
		 * Newest first, older than offsetId when given. limit 1..100,
		 * default 20. Needs `messages.read`.
		 */
		function history(
			peer: string | number,
			options?: { limit?: number; offsetId?: string | number }): Promise<LuxMessage[]>;
		/**
		 * Replaces the text (or caption) of a message this account may
		 * edit. Needs `messages.manage`.
		 */
		function edit(
			peer: string | number,
			id: string | number,
			options: { text: string; entities?: LuxEntity[] }): Promise<null>;
		/**
		 * Deletes up to 100 messages, skipping those this account cannot
		 * delete; revoke deletes them for everyone. Returns how many were
		 * deleted. Needs `messages.manage`.
		 */
		function remove(
			peer: string | number,
			ids: string | number | (string | number)[],
			options?: { revoke?: boolean }): number;
		/**
		 * Forwards up to 100 messages to another chat; returns how many
		 * could be forwarded. Needs `messages.manage`.
		 */
		function forward(
			peer: string | number,
			ids: string | number | (string | number)[],
			to: string | number): number;
		/** Marks the chat read, up to maxId when given. Needs `messages.manage`. */
		function markRead(peer: string | number, maxId?: string | number): null;
		/**
		 * Sends a file of at most 10 MiB: bytes, or a path relative to the
		 * plugin's data folder. Needs `messages.send`.
		 */
		function sendFile(
			peer: string | number,
			file: ArrayBuffer | ArrayBufferView | string,
			options?: { silent?: boolean }): null;
		/**
		 * The photo or file of a message, at most 10 MiB; rejects when
		 * saving is restricted in the chat. 60 s timeout.
		 * Needs `messages.read`.
		 */
		function downloadMedia(peer: string | number, id: string | number): Promise<ArrayBuffer>;
	}

	/** Never the Telegram service chat. Every member needs `messages.read`. */
	namespace chats {
		function get(peer: string | number): LuxChat;
		/** The main chat list, top first. limit 1..500, default 500. */
		function list(options?: { limit?: number }): LuxChat[];
	}

	namespace account {
		/** The active account, never its phone. Needs `messages.read`. */
		function me(): LuxAccount;
	}

	/** Every member needs `ui`. */
	namespace ui {
		/** At most one a second, otherwise throws RangeError. */
		function toast(text: string): void;
		/**
		 * A toast titled with the plugin name; shares toast's limit and
		 * throws while the app is locked.
		 */
		function notify(text: string): void;
		/** Resolves to true when closed. */
		function alert(options: LuxDialogOptions): Promise<boolean>;
		/** Resolves to true on OK, false when closed. */
		function confirm(options: LuxDialogOptions): Promise<boolean>;
		/** Resolves to the text on OK, null when closed. */
		function prompt(options: LuxPromptOptions): Promise<string | null>;
		/** Registering the same name again replaces the handler. */
		function addCommand(options: LuxCommandOptions): void;
		/** Registering the same title again replaces the handler. */
		function addMessageMenuItem(options: LuxMenuItemOptions): void;
		/** Registering the same keys again replaces the handler. */
		function addHotkey(options: LuxHotkeyOptions): void;
		/** An item of the chat context menu and the chat header menu. */
		function addChatMenuItem(options: LuxUiItemOptions): void;
		/** An item of the plugins button in the chat header. */
		function addHeaderButton(options: LuxUiItemOptions): void;
		/** An item of the message field's context menu. */
		function addComposeMenuItem(options: LuxUiItemOptions): void;
		/**
		 * Resolves to the values by key on OK, null when closed. One form
		 * at a time; closed when the plugin unloads.
		 */
		function showForm(schema: LuxFormSchema): Promise<{ [key: string]: string | boolean } | null>;
		/** The chat open in the active window, or null. */
		function currentChat(): LuxPeer | null;
		/** Opens the chat in the active window. */
		function openChat(peer: string | number): void;
		/**
		 * http(s) only, at most one a second, and only within 1 s after the
		 * user runs a command, menu item or hotkey of the plugin
		 * (NotAllowedError otherwise). Telegram links (t.me and the like)
		 * throw TypeError: use openChat. The client's link confirmation
		 * applies, unless the user turned it off for web links.
		 */
		function openUrl(url: string): void;
		/**
		 * A label in the badge after the sender name, kept until unload.
		 * Shown only on incoming messages that show a sender name.
		 */
		function decorateMessage(options: LuxDecorateOptions): void;
		/** Lays the message out again. */
		function refreshMessage(options: { peer: string | number; id: string | number }): void;
	}

	namespace http {
		/**
		 * HTTPS only, to a host of plugin.json `network`, on port 443 unless
		 * that entry names a port. Redirects (at most 5) are checked against
		 * the same list. 30 s timeout, 10 MiB response, User-Agent
		 * LuxuryGram. Invalid UTF-8 in the body becomes U+FFFD.
		 * Needs `network`.
		 */
		function fetch(url: string, options?: LuxFetchOptions): Promise<LuxResponse>;
	}

	/** JSON values, 5 MiB per plugin. Every member needs `storage`. */
	namespace storage {
		/** null when the key is not set. */
		function get(key: string): LuxJson;
		/** Throws QuotaExceededError when the data would exceed 5 MiB. */
		function set(key: string, value: LuxJson): void;
		function remove(key: string): void;
		function keys(): string[];
	}

	/** Values of plugin.json `settings`; always allowed. */
	namespace settings {
		/**
		 * The current value or the manifest default: boolean for bool,
		 * number for int and number, string for string, choice, select,
		 * color ("#RRGGBB") and secret.
		 */
		function get(key: string): LuxJson;
		/**
		 * Sets a declared setting; throws TypeError for an unknown key or a
		 * value the setting cannot hold. Fires onChange.
		 */
		function set(key: string, value: LuxJson): void;
		/** At most 64 handlers. */
		function onChange(fn: (event: { key: string; value: LuxJson }) => void): void;
	}

	/**
	 * Every member needs `clipboard`, and works only within 1 s after the
	 * user runs a command, menu item or hotkey of the plugin; otherwise it
	 * throws NotAllowedError.
	 */
	namespace clipboard {
		function read(): string;
		function write(text: string): void;
	}

	namespace api {
		/**
		 * A raw MTProto method in the JSON form of TL, e.g.
		 * call('messages.getHistory', {peer: {_: 'inputPeerSelf'}, limit: 10}).
		 * Methods that spend Stars or money, touch the login, sessions,
		 * password or privacy, or grant a bot access are refused, and so is
		 * the Telegram service chat. Needs `telegram.api`.
		 */
		function call(method: string, params?: { [key: string]: LuxJson }): Promise<any>;
	}
}

/** log, info and debug write level 0, warn 1, error 2. */
declare const console: {
	log(...args: any[]): void;
	info(...args: any[]): void;
	debug(...args: any[]): void;
	warn(...args: any[]): void;
	error(...args: any[]): void;
};

/** At most 64 timers per plugin; a timeout waits at least 4 ms. */
declare function setTimeout<A extends any[]>(fn: (...args: A) => void, delay?: number, ...args: A): number;
/** An interval fires at most every 50 ms. */
declare function setInterval<A extends any[]>(fn: (...args: A) => void, delay?: number, ...args: A): number;
declare function clearTimeout(id: number): void;
declare function clearInterval(id: number): void;
