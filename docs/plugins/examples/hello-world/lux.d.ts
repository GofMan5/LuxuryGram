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
//                       a response over 10 MiB
//   QuotaExceededError  storage over 5 MiB
//   NetworkError        a failed request or too many redirects
//   Error               no active account or window, the plugin is stopping
// A Telegram API error rejects with name = the error type
// ("FLOOD_WAIT_X", ...) and message = '{"type":"...","code":420}'.

type LuxPermission =
	| 'messages.read'
	| 'messages.send'
	| 'messages.modify'
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

interface LuxMessage {
	/** Message id in its chat, a decimal string. */
	id: string;
	peer: LuxPeer;
	from: LuxPeer;
	/** Unix time, seconds. */
	date: number;
	text: string;
	out: boolean;
	/** Id of the replied message, a decimal string, or null. */
	replyTo: string | null;
	hasMedia: boolean;
}

/** A menu item handler without `messages.read` gets only id and peer. */
type LuxMessageRef = LuxMessage | { id: string; peer: LuxPeer };

interface LuxEvents {
	/** An incoming message. Needs `messages.read`. */
	message: (event: { message: LuxMessage }) => void | Promise<void>;
	/** A message this account sent. Needs `messages.read`. */
	messageOut: (event: { message: LuxMessage }) => void | Promise<void>;
	/**
	 * Text about to be sent, synchronous. Return a string to replace it,
	 * `false` to cancel the send, anything else to keep it. Handlers run in
	 * subscription order, each sees the text the previous one returned.
	 * Needs `messages.modify`.
	 */
	beforeSend: (event: { peer: LuxPeer; text: string }) => string | false | void;
	/**
	 * One raw update in the JSON form of TL, at least once: dedupe by
	 * id/pts. Needs `telegram.updates`.
	 */
	update: (event: { update: { _: string; [key: string]: LuxJson } }) => void | Promise<void>;
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
	handler: (event: { message: LuxMessageRef }) => void | Promise<void>;
}

interface LuxHotkeyOptions {
	/**
	 * One shortcut in Qt portable text: "Ctrl+Shift+K". Ctrl+C/V/X/A/Z/Y,
	 * Ctrl+Enter, Enter and Esc are reserved. Not delivered while a text
	 * field has focus.
	 */
	keys: string;
	handler: () => void | Promise<void>;
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
	}

	/** Every member needs `ui`. */
	namespace ui {
		/** At most one a second, otherwise throws RangeError. */
		function toast(text: string): void;
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
