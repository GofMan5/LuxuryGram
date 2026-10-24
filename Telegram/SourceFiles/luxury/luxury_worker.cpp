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
namespace {

// Only ever runs while there is an account that both wants the offline packet
// and has a reason to send one, so the interval can stay short.
constexpr auto kPollInterval = crl::time(3000);

void runOnce();

// Per session: true means "an offline packet is owed". Main thread only --
// every writer is a timer callback or an API response handler.
std::unordered_map<uint64, bool> State;

std::unordered_map<uint64, mtpRequestId> Requests;

base::Timer &workerTimer() {
	static base::Timer timer([] {
		runOnce();
	});
	return timer;
}

void runOnce() {
	if (!Core::IsAppLaunched() || !Core::App().domain().started() || Core::Quitting()) {
		return;
	}

	auto active = std::unordered_set<uint64>();
	// sendOfflinePacketAfterOnline is off by default, so for almost every user
	// this used to be a three-second wake-up for the whole life of the process
	// that did nothing at all -- enough to keep a laptop out of its deeper idle
	// states. Stop when there is nothing owed: every event that can owe one goes
	// through markAsOnline(), and enabling the setting goes through wake().
	for (const auto &[index, account] : Core::App().domain().accounts()) {
		if (account) {
			if (const auto session = account->maybeSession()) {
				const auto id = session->uniqueId();
				active.insert(id);
				if (!State.contains(id)) {
					State[id] = false;
				}

				const auto &ghost = LuxurySettings::ghost(session);
				if (!ghost.sendOfflinePacketAfterOnline()) {
					if (const auto request = Requests.find(id); request != end(Requests)) {
						session->api().request(request->second).cancel();
						Requests.erase(request);
					}
					continue;
				}

				if (State[id] && !Requests.contains(id)) {
					Requests[id] = session->api().request(MTPaccount_UpdateStatus(
						MTP_bool(true)
					)).done([id](const auto &, mtpRequestId requestId) {
						if (const auto i = Requests.find(id)
							; i != end(Requests) && i->second == requestId) {
							Requests.erase(i);
							if (State[id]) {
								wake();
							}
						}
					}).fail([id](const MTP::Error &, mtpRequestId requestId) {
						if (const auto i = Requests.find(id)
							; i != end(Requests) && i->second == requestId) {
							Requests.erase(i);
							if (State[id]) {
								wake();
							}
						}
					}).send();
					State[id] = false;

					DEBUG_LOG(("[LuxuryGram] Sent offline for account with id %1").arg(id));
				}
			}
		}
	}
	for (auto i = begin(State); i != end(State);) {
		if (active.contains(i->first)) {
			++i;
		} else {
			Requests.erase(i->first);
			i = State.erase(i);
		}
	}

	workerTimer().cancel();
}

} // namespace

void markAsOnline(not_null<Main::Session*> session) {
	State[session->uniqueId()] = true;
	wake();
}

void wake() {
	// callEach() on a running timer restarts the interval, which is what the
	// caller wants either way: something just happened.
	workerTimer().callEach(kPollInterval);
}

void initialize() {
	wake();
}

}
