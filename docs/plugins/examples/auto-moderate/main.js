/// <reference path="../../lux.d.ts" />
// Auto moderate: translates the messages you edit and deletes incoming
// messages that match a pattern, through the typed lux.messages API only.
// Deleting needs admin rights in groups; messages it cannot delete stay.

// Messages this plugin edited itself, so their messageEdited is skipped.
const ownEdits = new Set();
const key = (message) => `${message.peer.id}:${message.id}`;

const pattern = () => {
	const source = String(lux.settings.get('pattern') || '');
	if (!source) {
		return null;
	}
	try {
		return new RegExp(source, 'i');
	} catch (e) {
		lux.log('bad pattern:', e.message);
		return null;
	}
};

const moderate = (message) => {
	const re = pattern();
	if (message.out || !re || !re.test(message.text)) {
		return;
	}
	const revoke = lux.settings.get('revoke') === true;
	const count = lux.messages.remove(message.peer.id, message.id, { revoke });
	lux.log(count ? 'deleted' : 'could not delete', key(message));
};

const translate = async (text, language) => {
	const url = 'https://translate.googleapis.com/translate_a/single'
		+ `?client=gtx&sl=auto&tl=${encodeURIComponent(language)}&dt=t`
		+ `&q=${encodeURIComponent(text)}`;
	const response = await lux.http.fetch(url);
	if (response.status !== 200) {
		throw new Error(`translate: HTTP ${response.status}`);
	}
	const [parts, , detected] = response.json();
	return detected === language
		? null
		: parts.map((part) => part[0]).join('');
};

const translateEdit = async (message) => {
	const language = String(lux.settings.get('language') || '');
	if (!message.out || !language || !message.text.trim()) {
		return;
	}
	const translated = await translate(message.text, language);
	if (!translated || translated === message.text) {
		return;
	}
	ownEdits.add(key(message));
	try {
		await lux.messages.edit(message.peer.id, message.id, { text: translated });
		lux.ui.notify(`Translated to ${language}`);
	} catch (e) {
		ownEdits.delete(key(message));
		throw e;
	}
};

lux.on('message', ({ message }) => moderate(message));

lux.on('messageEdited', ({ message }) => {
	if (ownEdits.delete(key(message))) {
		return;
	}
	moderate(message);
	return translateEdit(message);
});

lux.on('messageDeleted', ({ message }) => ownEdits.delete(key(message)));

lux.log(`auto-moderate ${lux.plugin.version} loaded`);
