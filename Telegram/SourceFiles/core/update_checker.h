/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "mtproto/dedicated_file_loader.h"

namespace Core {

bool UpdaterDisabled();
void SetUpdaterDisabledAtStartup();

class Updater;

class UpdateChecker {
public:
	enum class State {
		None,
		Download,
		Ready,
	};
	using Progress = MTP::AbstractDedicatedLoader::Progress;

	UpdateChecker();

	rpl::producer<> checking() const;
	rpl::producer<> isLatest() const;
	rpl::producer<Progress> progress() const;
	rpl::producer<> failed() const;
	rpl::producer<> ready() const;

	void start(bool forceWait = false);
	void stop();
	void test();

	State state() const;
	int already() const;
	int size() const;
	bool percent() const;

private:
	const std::shared_ptr<Updater> _updater;

};

bool checkReadyUpdate();
void UpdateApplication();
QString countAlphaVersionSignature(uint64 version);

// The updater's state as one line of text, so every place that shows it says the
// same thing. whenIdle is what to show when nothing is happening -- normally the
// current version. The producer emits the state at subscription and then on
// every change, so a caller only needs a label.
[[nodiscard]] rpl::producer<QString> UpdateStateText(QString whenIdle);

// Exposed because progress arrives through UpdateChecker::progress() as well,
// and a caller that wants to react to more than the text still has to format it.
[[nodiscard]] QString UpdateDownloadText(
	int64 already,
	int64 total,
	bool preferPercent);

} // namespace Core
