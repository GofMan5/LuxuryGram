// This is the source code of LuxuryGram for Desktop.
// See LEGAL for license and copyright information.
//
// lux.chats, lux.messages beyond send, lux.account, settings.set and
// ui.notify: Instance::data(), reached from Instance::call. Call() has
// checked DataPermission() already; every peer resolves through
// ResolvePeer, so the service chat never shows up.
#include "luxury/plugins/plugin_host_api.h"

#include "luxury/plugins/plugin_hooks.h"
#include "luxury/plugins/plugin_host_logic.h"
#include "luxury/plugins/plugin_manager.h"
#include "luxury/plugins/plugin_payloads.h"
#include "luxury/plugins/core/plugin_manifest.h"
#include "luxury/utils/telegram_helpers.h"
#include "api/api_common.h"
#include "api/api_editing.h"
#include "apiwrap.h"
#include "base/call_delayed.h"
#include "base/unixtime.h"
#include "core/application.h"
#include "data/data_chat_participant_status.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_origin.h"
#include "data/data_histories.h"
#include "data/data_media_types.h"
#include "data/data_photo.h"
#include "data/data_photo_media.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "dialogs/dialogs_indexed_list.h"
#include "dialogs/dialogs_main_list.h"
#include "dialogs/dialogs_row.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "storage/file_download.h"
#include "storage/localimageloader.h"
#include "ui/toast/toast.h"

#include <QFile>

namespace Luxury::Plugins {
namespace {

using json = nlohmann::json;

// DocumentData::save(origin, QString()) loads into memory only up to this.
static_assert(kMaxMediaBytes == Storage::kMaxFileInMemory);

constexpr auto kDownloadTimeout = crl::time(60000);

[[nodiscard]] Js::HostResult TooManyRequests() {
	return Js::HostResult::Error(
		"RangeError",
		"too many requests in flight, at most "
			+ std::to_string(kMaxInFlight));
}

[[nodiscard]] Js::HostResult RateLimited(std::string_view function) {
	return Js::HostResult::Error(
		"RangeError",
		std::string(function)
			+ ": rate limited, at most one file or forward a second");
}

[[nodiscard]] Js::HostResult PermissionError(Permission permission) {
	return Js::HostResult::Error(
		"PermissionError",
		'"' + std::string(PermissionId(permission)) + "\" was not granted");
}

[[nodiscard]] Js::HostResult TypeError(std::string text) {
	return Js::HostResult::Error("TypeError", std::move(text));
}

[[nodiscard]] const json &Arg(const json &args, std::size_t index) {
	static const auto null = json();
	return (args.is_array() && index < args.size()) ? args[index] : null;
}

[[nodiscard]] json Option(const json &options, const char *key) {
	return options.is_object() ? options.value(key, json()) : json();
}

// A peer argument of the active account, or nullptr with the error set.
[[nodiscard]] PeerData *PeerArg(
		Main::Session *session,
		const json &value,
		Js::HostResult &error) {
	if (!session) {
		error = Js::HostResult::Error("Error", "no active Telegram account");
		return nullptr;
	} else if (IsServiceUserId(value)) {
		error = Js::HostResult::Error(
			"PermissionError",
			"the Telegram service chat is never available to plugins");
		return nullptr;
	}
	const auto dialogId = DialogId(value);
	const auto peer = dialogId ? ResolvePeer(session, *dialogId) : nullptr;
	if (!peer) {
		error = Js::HostResult::Error("Error", "unknown peer");
	}
	return peer;
}

// A message as plugins see it, null for none or a service message.
[[nodiscard]] json ItemJson(HistoryItem *item) {
	return (item && item->isRegular() && !item->isService())
		? MessageJson(item)
		: json();
}

[[nodiscard]] json ChatJson(not_null<PeerData*> peer) {
	auto result = PeerJson(peer);
	const auto username = peer->username();
	const auto history = peer->owner().historyLoaded(peer.get());
	result["username"] = username.isEmpty()
		? json()
		: json(username.toStdString());
	result["unreadCount"] = history ? history->unreadCount() : 0;
	return result;
}

} // namespace

Js::HostResult Instance::data(
		std::string_view function,
		const json &args,
		std::uint64_t promiseId) {
	const auto &first = Arg(args, 0);
	const auto &second = Arg(args, 1);
	const auto &third = Arg(args, 2);
	if (function == "settings.set") {
		if (!first.is_string()) {
			return TypeError("settings.set: key must be a string");
		}
		const auto &key = first.get_ref<const std::string&>();
		const auto i = std::find_if(
			_manifest.settings.begin(),
			_manifest.settings.end(),
			[&](const SettingDescriptor &setting) {
				return setting.key == key;
			});
		if (i == _manifest.settings.end()) {
			return TypeError("settings.set: no setting \"" + key + '"');
		} else if (!SettingAccepts(*i, second)) {
			return TypeError("settings.set: the value does not fit \""
				+ key + '"');
		}
		const auto manager = Manager::Maybe();
		if (!manager) {
			return Result::Error("Error", "the plugin manager is gone");
		}
		manager->setSettingValue(
			QString::fromStdString(_plugin.id),
			QString::fromStdString(key),
			second);
		return Result::Value(nullptr);
	} else if (function == "ui.notify") {
		if (!first.is_string() || first.get_ref<const std::string&>().empty()) {
			return TypeError("ui.notify: text must be a non-empty string");
		} else if (Core::IsAppLaunched() && Core::App().passcodeLocked()) {
			return Result::Error("Error", "the app is locked with a passcode");
		}
		const auto now = crl::now();
		if (!ToastAllowed(now, _lastToast)) {
			return Result::Error("RangeError", "at most one toast a second");
		}
		_lastToast = now;
		// The title names the plugin: a notice must not pass for the app's.
		Ui::Toast::Show(Ui::Toast::Config{
			.title = QString::fromStdString(_plugin.name.empty()
				? _plugin.id
				: _plugin.name),
			.text = TextWithEntities{
				QString::fromStdString(first.get<std::string>()),
			},
		});
		return Result::Value(nullptr);
	}

	const auto session = ActiveSession();
	if (function == "account.me") {
		if (!session) {
			return Result::Error("Error", "no active Telegram account");
		}
		// Never the phone number, see HideOwnPhone.
		const auto user = session->user();
		const auto username = user->username();
		return Result::Value({
			{ "id", std::to_string(getDialogIdFromPeer(user)) },
			{ "firstName", user->firstName.toStdString() },
			{ "lastName", user->lastName.toStdString() },
			{ "username", username.isEmpty()
				? json()
				: json(username.toStdString()) },
			{ "premium", user->isPremium() },
		});
	} else if (function == "chats.list") {
		if (!session) {
			return Result::Error("Error", "no active Telegram account");
		}
		const auto limit = LimitArg(
			Option(first, "limit"),
			kMaxChatsLimit,
			kMaxChatsLimit);
		auto result = json::array();
		for (const auto &row : session->data().chatsList()->indexed()->all()) {
			const auto history = row->history();
			if (!history || !ChatVisible(getDialogIdFromPeer(history->peer))) {
				continue;
			}
			result.push_back(ChatJson(history->peer));
			if (int(result.size()) >= limit) {
				break;
			}
		}
		return Result::Value(std::move(result));
	}

	auto error = Result();
	const auto peer = PeerArg(session, first, error);
	if (!peer) {
		return error;
	}
	const auto weak = base::make_weak(this);
	// Takes an in-flight slot; the returned settle drops the reply when
	// the plugin stopped or the account changed meanwhile.
	const auto later = [&] {
		_requests.emplace(promiseId);
		return [=](bool ok, json value, std::string message) {
			const auto strong = weak.get();
			if (strong
				&& !strong->_stopping
				&& strong->_requests.contains(promiseId)) {
				strong->settleLater(
					promiseId,
					ok,
					std::move(value),
					"Error",
					std::move(message));
			}
		};
	};
	const auto message = [&](const json &value) -> HistoryItem* {
		const auto id = MessageIdArg(value);
		return id ? session->data().message(peer, MsgId(*id)) : nullptr;
	};

	if (function == "chats.get") {
		return Result::Value(ChatJson(peer));
	} else if (function == "messages.get") {
		const auto id = MessageIdArg(second);
		if (!id) {
			return TypeError("messages.get: id must be a message id");
		} else if (const auto item = message(second)) {
			return Result::Value(ItemJson(item));
		} else if (!InFlightAllowed(int(_requests.size()))) {
			return TooManyRequests();
		}
		const auto settle = later();
		const auto msgId = MsgId(*id);
		session->api().requestMessageData(peer, msgId, [=] {
			// Called on failure too: the message is then still unknown.
			settle(true, ItemJson(session->data().message(peer, msgId)), {});
		});
		return Result::Pending();
	} else if (function == "messages.history") {
		const auto offset = MessageIdArg(Option(second, "offsetId"));
		const auto limit = LimitArg(
			Option(second, "limit"),
			20,
			kMaxHistoryLimit);
		if (!InFlightAllowed(int(_requests.size()))) {
			return TooManyRequests();
		}
		const auto settle = later();
		session->api().request(MTPmessages_GetHistory(
			peer->input(),
			MTP_int(offset ? int(*offset) : 0), // MessageIdArg caps it
			MTP_int(0), // offset_date
			MTP_int(0), // add_offset
			MTP_int(limit),
			MTP_int(0), // max_id
			MTP_int(0), // min_id
			MTP_long(0) // hash
		)).done([=](const MTPmessages_Messages &result) {
			session->data().processExistingMessages(peer->asChannel(), result);
			auto list = json::array();
			result.match([](const MTPDmessages_messagesNotModified &) {
			}, [&](const auto &data) {
				for (const auto &item : data.vmessages().v) {
					auto value = ItemJson(session->data().message(
						peer,
						IdFromMessage(item)));
					if (!value.is_null()) {
						list.push_back(std::move(value));
					}
				}
			});
			settle(true, std::move(list), {});
		}).fail([=](const MTP::Error &failure) {
			settle(false, nullptr, failure.type().toStdString());
		}).send();
		return Result::Pending();
	} else if (function == "messages.edit") {
		const auto item = message(second);
		const auto text = Option(third, "text");
		if (!item) {
			return Result::Error("Error", "unknown message");
		} else if (!text.is_string()) {
			return TypeError("messages.edit: text must be a string");
		} else if (!item->allowsEdit(base::unixtime::now())) {
			return Result::Error("Error", "this message cannot be edited");
		} else if (!InFlightAllowed(int(_requests.size()))) {
			return TooManyRequests();
		}
		const auto settle = later();
		Api::EditCaption(
			item,
			PluginText(
				session,
				text.get<std::string>(),
				Option(third, "entities")),
			Api::SendOptions(),
			[=] { settle(true, nullptr, {}); },
			[=](const QString &failure) {
				settle(false, nullptr, failure.toStdString());
			});
		return Result::Pending();
	} else if (function == "messages.delete") {
		// Needed() asks for it already; deletion is never one check away.
		if (!_plugin.granted.has(Permission::MessagesManage)) {
			return PermissionError(Permission::MessagesManage);
		}
		const auto ids = MessageIdsArg(second);
		if (!ids) {
			return TypeError("messages.delete: expected 1 to "
				+ std::to_string(kMaxHistoryLimit)
				+ " message ids");
		}
		auto list = MessageIdsList();
		for (const auto id : *ids) {
			const auto item = session->data().message(peer, MsgId(id));
			if (item && item->canDelete()) {
				list.push_back(item->fullId());
			}
		}
		const auto revoke = (Option(third, "revoke") == json(true));
		if (!list.empty()) {
			session->data().histories().deleteMessages(list, revoke);
			session->data().sendHistoryChangeNotifications();
		}
		return Result::Value(int(list.size()));
	} else if (function == "messages.forward") {
		const auto ids = MessageIdsArg(second);
		if (!ids) {
			return TypeError("messages.forward: expected 1 to "
				+ std::to_string(kMaxHistoryLimit)
				+ " message ids");
		}
		const auto to = PeerArg(session, third, error);
		if (!to) {
			return error;
		} else if (!Data::CanSendAnything(to)) {
			return Result::Error("Error", "you cannot write to this chat");
		}
		auto items = HistoryItemsList();
		for (const auto id : *ids) {
			const auto item = session->data().message(peer, MsgId(id));
			if (item && item->allowsForward()) {
				items.push_back(item);
			}
		}
		const auto count = int(items.size());
		if (count) {
			const auto now = crl::now();
			if (!MediaSendAllowed(now, _lastMediaSend)) {
				return RateLimited("messages.forward");
			}
			_lastMediaSend = now;
			session->api().forwardMessages(
				Data::ResolvedForwardDraft{ .items = std::move(items) },
				Api::SendAction(session->data().history(to)));
		}
		return Result::Value(count);
	} else if (function == "messages.markRead") {
		const auto history = session->data().history(peer);
		if (const auto till = MessageIdArg(second)) {
			session->data().histories().readInboxTill(history, MsgId(*till));
		} else if (second.is_null()) {
			session->data().histories().readInbox(history);
		} else {
			return TypeError("messages.markRead: maxId must be a message id");
		}
		return Result::Value(nullptr);
	} else if (function == "messages.sendFile") {
		auto bytes = QByteArray();
		const auto bin = Option(second, "$bin");
		if (bin.is_string()) {
			const auto &text = bin.get_ref<const std::string&>();
			if (std::int64_t(text.size()) > (kMaxMediaBytes / 3 + 1) * 4) {
				return Result::Error("RangeError", "the file is too large");
			}
			auto decoded = QByteArray::fromBase64Encoding(
				QByteArray::fromStdString(text),
				QByteArray::Base64Encoding
					| QByteArray::AbortOnBase64DecodingErrors);
			if (!decoded) {
				return TypeError("messages.sendFile: bad binary data");
			}
			bytes = std::move(*decoded);
		} else if (second.is_string()) {
			// A file the plugin keeps in its own data folder, nothing else.
			const auto &path = second.get_ref<const std::string&>();
			if (!IsSafeRelativePath(path)) {
				return TypeError("messages.sendFile: unsafe path");
			}
			auto file = QFile(_root
				+ u"/data/"_q
				+ QString::fromStdString(path));
			if (file.size() > kMaxMediaBytes) {
				return Result::Error("RangeError", "the file is too large");
			} else if (!file.open(QIODevice::ReadOnly)) {
				return Result::Error("Error", "could not read " + path);
			}
			bytes = file.readAll();
		} else {
			return TypeError(
				"messages.sendFile: expected an ArrayBuffer or a path");
		}
		if (bytes.isEmpty()) {
			return TypeError("messages.sendFile: the file is empty");
		} else if (bytes.size() > kMaxMediaBytes) {
			return Result::Error("RangeError", "the file is too large");
		} else if (!Data::CanSend(peer, ChatRestriction::SendFiles)) {
			return Result::Error("Error", "you cannot send files to this chat");
		}
		const auto now = crl::now();
		if (!MediaSendAllowed(now, _lastMediaSend)) {
			return RateLimited("messages.sendFile");
		}
		_lastMediaSend = now;
		auto action = Api::SendAction(session->data().history(peer));
		action.options.silent = (Option(third, "silent") == json(true));
		session->api().sendFile(bytes, SendMediaType::File, action);
		return Result::Value(nullptr);
	} else if (function == "messages.downloadMedia") {
		const auto item = message(second);
		const auto media = item ? item->media() : nullptr;
		const auto photo = media ? media->photo() : nullptr;
		const auto document = media ? media->document() : nullptr;
		if (!item) {
			return Result::Error("Error", "unknown message");
		} else if (!photo && !document) {
			return TypeError("messages.downloadMedia: no photo or file");
		} else if (item->forbidsSaving()) {
			return Result::Error(
				"Error",
				"saving content is restricted in this chat");
		} else if ((document
			? document->size
			: photo->imageByteSize(Data::PhotoSize::Large)) > kMaxMediaBytes) {
			return Result::Error(
				"RangeError",
				"the file is larger than "
					+ std::to_string(kMaxMediaBytes)
					+ " bytes");
		} else if (!InFlightAllowed(int(_requests.size()))) {
			return TooManyRequests();
		}
		struct Download {
			std::shared_ptr<Data::DocumentMedia> document;
			std::shared_ptr<Data::PhotoMedia> photo;
			rpl::lifetime lifetime;
			bool done = false;
		};
		const auto state = std::make_shared<Download>();
		const auto settle = later();
		// Never touches the session: the timeout may outlive it.
		const auto finish = [=](QByteArray bytes, std::string error) {
			if (state->done) {
				return;
			}
			state->done = true;
			state->document = nullptr;
			state->photo = nullptr;
			if (error.empty()) {
				settle(true, { { "$bin", bytes.toBase64().toStdString() } }, {});
			} else {
				settle(false, nullptr, std::move(error));
			}
			// Not from inside the subscription it destroys.
			crl::on_main([=] { state->lifetime.destroy(); });
		};
		const auto check = [=] {
			if (const auto view = state->document) {
				const auto path = document->filepath(true);
				if (!view->bytes().isEmpty()) {
					finish(view->bytes(), {});
				} else if (!path.isEmpty()) {
					auto file = QFile(path);
					if (file.size() <= kMaxMediaBytes
						&& file.open(QIODevice::ReadOnly)) {
						finish(file.readAll(), {});
					} else {
						finish({}, "could not read the downloaded file");
					}
				} else if (!document->loading()) {
					finish({}, "the download failed");
				}
			} else if (const auto view = state->photo) {
				if (view->loaded()) {
					auto bytes = view->imageBytes(Data::PhotoSize::Large);
					if (bytes.isEmpty()) {
						finish({}, "the photo has no file data");
					} else {
						finish(std::move(bytes), {});
					}
				} else if (photo->failed(Data::PhotoSize::Large)
					|| photo->cancelled()) {
					finish({}, "the download failed");
				}
			}
		};
		const auto origin = Data::FileOriginMessage(item->fullId());
		if (document) {
			state->document = document->createMediaView();
			document->save(origin, QString());
		} else {
			state->photo = photo->createMediaView();
			state->photo->wanted(Data::PhotoSize::Large, origin);
		}
		// A cancelled or failed load reports itself here, not as finished.
		session->downloaderTaskFinished(
		) | rpl::on_next(check, state->lifetime);
		if (document) {
			session->data().documentLoadProgress(
			) | rpl::filter([=](not_null<DocumentData*> loaded) {
				return (loaded == document);
			}) | rpl::on_next(check, state->lifetime);
		} else {
			session->data().photoLoadProgress(
			) | rpl::filter([=](not_null<PhotoData*> loaded) {
				return (loaded == photo);
			}) | rpl::on_next(check, state->lifetime);
		}
		// accountChanged() already rejects the promise; this drops the views
		// and the subscriptions, which hold raw pointers into the session.
		rpl::merge(
			session->data().sessionDataAboutToBeCleared(),
			Core::App().domain().activeSessionChanges() | rpl::to_empty
		) | rpl::on_next([=] {
			finish({}, "the active account changed");
		}, state->lifetime);
		base::call_delayed(kDownloadTimeout, [=] {
			finish({}, "the download timed out");
		});
		check();
		return Result::Pending();
	}
	return TypeError("unknown host function " + std::string(function));
}

} // namespace Luxury::Plugins
