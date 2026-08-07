// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "luxury/luxury_worker.h"

#include "apiwrap.h"
#include "luxury_settings.h"
#include "base/timer.h"
#include "base/unixtime.h"
#include "core/application.h"
#include "data/data_user.h"
#include "data/entities.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"

#include <unordered_map>
#include <unordered_set>

namespace LuxuryWorker {

void runOnce();

std::unordered_map<uint64, bool> state;

base::Timer &workerTimer() {
	static base::Timer timer([] {
		runOnce();
	});
	return timer;
}

void markAsOnline(not_null<Main::Session*> session) {
	state[session->uniqueId()] = true;
	workerTimer().cancel();
	workerTimer().callEach(3000);
}

void runOnce() {
	if (!Core::IsAppLaunched() || !Core::App().domain().started() || Core::Quitting()) {
		return;
	}

	const auto t = base::unixtime::now();
	auto active = std::unordered_set<uint64>();

	for (const auto &[index, account] : Core::App().domain().accounts()) {
		if (account) {
			if (const auto session = account->maybeSession()) {
				const auto id = session->uniqueId();
				active.insert(id);
				if (!state.contains(id)) {
					state[id] = true;
				}

				const auto &ghost = LuxurySettings::ghost(session);
				if (!ghost.sendOfflinePacketAfterOnline()) {
					continue;
				}

				if (state[id] || session->user()->lastseen().isOnline(t)) {
					session->api().request(MTPaccount_UpdateStatus(
						MTP_bool(true)
					)).send();
					state[id] = false;

					DEBUG_LOG(("[LuxuryGram] Sent offline for account with id %1").arg(id));
				}
			}
		}
	}
	for (auto i = begin(state); i != end(state);) {
		if (active.contains(i->first)) {
			++i;
		} else {
			i = state.erase(i);
		}
	}
}

void initialize() {
	workerTimer().callEach(3000);
}

}
