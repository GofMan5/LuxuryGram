/// <reference path="../../lux.d.ts" />
// Hello world: a /hello command and a Ctrl+Shift+H hotkey that greet you.
// Needs only the "ui" permission; settings are always readable.

const greet = async (who) => {
	const marks = '!'.repeat(Number(lux.settings.get('repeat')) || 0);
	const text = `${lux.settings.get('greeting')}, ${who}${marks}`;
	if (lux.settings.get('style') === 'alert') {
		await lux.ui.alert({ title: lux.plugin.id, text });
	} else {
		lux.ui.toast(text);
	}
};

lux.ui.addCommand({
	name: 'hello',
	description: 'Say hello',
	handler: ({ peer, args }) => greet(args.trim() || peer.title),
});

lux.ui.addHotkey({
	keys: 'Ctrl+Shift+H',
	handler: () => greet('world'),
});

lux.settings.onChange(({ key, value }) => {
	console.log('setting changed:', key, value);
});

lux.on('unload', () => console.log('bye'));

lux.log(`hello-world ${lux.plugin.version} loaded on LuxuryGram ${lux.plugin.clientVersion}`);
