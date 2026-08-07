// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "luxury/ui/context_menu/context_menu.h"

#include "apiwrap.h"
#include "lang_auto.h"
#include "mainwidget.h"
#include "api/api_sending.h"
#include "luxury/luxury_settings.h"
#include "luxury/luxury_state.h"
#include "luxury/data/messages_storage.h"
#include "luxury/features/filters/filters_controller.h"
#include "luxury/features/forward/luxury_forward.h"
#include "luxury/ui/context_menu/menu_item_subtext.h"
#include "luxury/ui/message_history/history_section.h"
#include "luxury/ui/settings/filters/edit_filter.h"
#include "luxury/ui/settings/filters/settings_filters_list.h"
#include "luxury/utils/qt_key_modifiers_extended.h"
#include "luxury/utils/telegram_helpers.h"
#include "base/call_delayed.h"
#include "base/random.h"
#include "base/unixtime.h"
#include "boxes/translate_box.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "core/mime_type.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_forum_topic.h"
#include "data/data_search_controller.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/history_item_components.h"
#include "history/view/history_view_context_menu.h"
#include "history/view/history_view_element.h"
#include "main/main_session.h"
#include "main/session/send_as_peers.h"
#include "styles/style_luxury_icons.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "ui/boxes/confirm_box.h"
#include "ui/boxes/choose_language_box.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/menu/menu_add_action_callback_factory.h"
#include "window/window_peer_menu.h"
#include "window/window_session_controller.h"

namespace LuxuryUi {

namespace {

Fn<void()> ClearDeletedMessagesHandler(not_null<Window::SessionController*> controller, not_null<PeerData*> peer, ID topicId) {
	return [=] {
		controller->show(Ui::MakeConfirmBox({
			.text = tr::luxury_ClearDeletedMessagesText(tr::now),
			.confirmed = [=](Fn<void()> &&close) {
				auto items = std::vector<not_null<HistoryItem*>>();
				for (const auto &block : peer->owner().history(peer)->blocks) {
					for (const auto &view : block->messages) {
						const auto item = view->data();
						if (item->isDeleted() && (!topicId || (item->topicRootId().bare == topicId))) {
							items.push_back(item);
						}
					}
				}
				LuxuryMessages::clearDeletedMessages(peer, topicId);
				for (const auto item : items) {
					item->destroy();
				}
				close();
			},
			.confirmText = tr::luxury_ClearDeletedMessagesActionText(tr::now),
			.cancelText = tr::lng_cancel(),
			.confirmStyle = &st::attentionBoxButton,
		}));
	};
}

void DeleteMyMessagesAfterConfirm(not_null<PeerData*> peer) {
	const auto session = &peer->session();

	if (const auto channel = peer->asChannel()) {
		if (channel->isMegagroup() && channel->canDeleteMessages()) {
			session->api().deleteAllFromParticipant(channel, session->user());
			return;
		}
	}

	struct State {
		base::weak_ptr<Main::Session> session;
		PeerId peerId;
		int totalDeleted = 0;
		Fn<void(MsgId)> requestNext;
		Fn<void(QVector<MTPint>, MsgId, bool)> removeBatch;
	};
	const auto state = std::make_shared<State>();
	state->session = base::make_weak(session);
	state->peerId = peer->id;
	const auto weak = std::weak_ptr<State>(state);

	state->removeBatch = [weak](
			QVector<MTPint> ids,
			MsgId nextFrom,
			bool hasMore) {
		const auto state = weak.lock();
		if (!state || ids.isEmpty()) {
			return;
		}
		const auto session = state->session.get();
		const auto peer = session
			? session->data().peerLoaded(state->peerId)
			: nullptr;
		if (!peer) {
			return;
		}
		const auto batch = state->totalDeleted / 100 + 1;
		const auto done = [state, ids, nextFrom, hasMore, batch](
				const MTPmessages_AffectedMessages &result) {
			const auto session = state->session.get();
			const auto peer = session
				? session->data().peerLoaded(state->peerId)
				: nullptr;
			if (!peer) {
				return;
			}
			session->api().applyAffectedMessages(peer, result);
			if (peer->isChannel()) {
				session->data().processMessagesDeleted(peer->id, ids);
			} else {
				session->data().processNonChannelMessagesDeleted(ids);
			}
			state->totalDeleted += ids.size();
			DEBUG_LOG(("Deleted batch %1, total deleted %2")
				.arg(batch)
				.arg(state->totalDeleted));
			if (!hasMore) {
				DEBUG_LOG(("Deleted all %1 my messages in this chat")
					.arg(state->totalDeleted));
				return;
			}
			const auto delay = crl::time(
				500 + base::RandomValue<int>() % 500);
			base::call_delayed(delay, [state, nextFrom] {
				state->requestNext(nextFrom);
			});
		};
		const auto fail = [state, ids, nextFrom, hasMore, batch](
				const MTP::Error &error) {
			const auto type = error.type();
			DEBUG_LOG(("Delete batch %1 failed: %2").arg(batch).arg(type));
			if (type.startsWith(u"FLOOD_WAIT_"_q)
				|| type.startsWith(u"FLOOD_PREMIUM_WAIT_"_q)) {
				const auto underscore = type.lastIndexOf('_');
				const auto seconds = (underscore >= 0)
					? type.mid(underscore + 1).toInt()
					: 0;
				const auto delay = crl::time(std::max(seconds, 1) * 1000);
				base::call_delayed(delay, [state, ids, nextFrom, hasMore] {
					state->removeBatch(ids, nextFrom, hasMore);
				});
				return;
			}
			if (type == u"MESSAGE_DELETE_FORBIDDEN"_q
				|| type == u"MSG_ID_INVALID"_q
				|| type == u"MESSAGE_ID_INVALID"_q) {
				DEBUG_LOG(("Skipping batch %1 (%2 ids)")
					.arg(batch)
					.arg(ids.size()));
				if (hasMore) {
					const auto delay = crl::time(
						500 + base::RandomValue<int>() % 500);
					base::call_delayed(delay, [state, nextFrom] {
						state->requestNext(nextFrom);
					});
				}
				return;
			}
			DEBUG_LOG(("Stopping deletion, unrecoverable error: %1").arg(type));
		};

		if (const auto channel = peer->asChannel()) {
			session->api()
				.request(MTPchannels_DeleteMessages(
					channel->inputChannel(),
					MTP_vector<MTPint>(ids)))
				.done(done)
				.fail(fail)
				.handleFloodErrors()
				.send();
		} else {
			using Flag = MTPmessages_DeleteMessages::Flag;
			session->api()
				.request(MTPmessages_DeleteMessages(
					MTP_flags(Flag::f_revoke),
					MTP_vector<MTPint>(ids)))
				.done(done)
				.fail(fail)
				.handleFloodErrors()
				.send();
		}
	};

	state->requestNext = [weak](MsgId from) {
		const auto state = weak.lock();
		if (!state) {
			return;
		}
		const auto session = state->session.get();
		const auto peer = session
			? session->data().peerLoaded(state->peerId)
			: nullptr;
		if (!peer) {
			return;
		}

		using Flag = MTPmessages_Search::Flag;
		auto request = MTPmessages_Search(
			MTP_flags(Flag::f_from_id),
			peer->input(),
			MTP_string(),
			MTP_inputPeerSelf(),
			MTPInputPeer(),
			MTPVector<MTPReaction>(),
			MTP_int(0),
			MTP_inputMessagesFilterEmpty(),
			MTP_int(0),
			MTP_int(0),
			MTP_int(from.bare),
			MTP_int(0),
			MTP_int(100),
			MTP_int(0),
			MTP_int(0),
			MTP_long(0));

		session->api()
			.request(std::move(request))
			.done([state, from](const Api::HistoryRequestResult &result) {
				const auto session = state->session.get();
				const auto peer = session
					? session->data().peerLoaded(state->peerId)
					: nullptr;
				if (!peer) {
					return;
				}
				auto parsed = Api::ParseHistoryResult(
					peer,
					from,
					Data::LoadDirection::Before,
					result);
				auto ids = QVector<MTPint>();
				ids.reserve(parsed.messageIds.size());
				auto minId = MsgId();
				for (const auto &id : parsed.messageIds) {
					ids.push_back(MTP_int(id.bare));
					if (!minId || id < minId) {
						minId = id;
					}
				}
				if (ids.isEmpty()) {
					DEBUG_LOG(("Deleted all %1 my messages in this chat")
						.arg(state->totalDeleted));
					return;
				}
				const auto hasMore = parsed.messageIds.size() == 100 && minId;
				state->removeBatch(
					std::move(ids),
					hasMore ? (minId - MsgId(1)) : MsgId(),
					hasMore);
			})
			.fail([state](const MTP::Error &error) {
				DEBUG_LOG(("History fetch failed: %1").arg(error.type()));
			})
			.send();
	};

	state->requestNext(MsgId(0));
}

Fn<void()> DeleteMyMessagesHandler(not_null<Window::SessionController*> controller, not_null<PeerData*> peer) {
	return [=]
	{
		if (!controller->showFrozenError()) {
			controller->show(Ui::MakeConfirmBox({
				.text = tr::luxury_DeleteOwnMessagesConfirmation(tr::now),
				.confirmed =
				[=](Fn<void()> &&close)
				{
					DeleteMyMessagesAfterConfirm(peer);
					close();
				},
				.confirmText = tr::lng_box_delete(),
				.cancelText = tr::lng_cancel(),
				.confirmStyle = &st::attentionBoxButton,
			}));
		}
	};
}

}

bool needToShowItem(ContextMenuVisibility state) {
	return state == ContextMenuVisibility::Visible
		|| (state == ContextMenuVisibility::VisibleWithModifier && base::IsExtendedContextMenuModifierPressed());
}

void AddLuxuryGramActions(PeerData *peerData,
							   Data::Thread *thread,
							   not_null<Window::SessionController*> sessionController,
							   const Window::PeerMenuCallback &addCallback) {
	if (!peerData) {
		return;
	}

	const auto &settings = LuxurySettings::getInstance();
	const auto user = peerData->asUser();
	const auto showFilters = settings.filtersEnabled()
		&& (!user || user->isBot());
	const auto saveDeletedMessages = settings.saveDeletedMessages();
	if (!showFilters && !saveDeletedMessages) {
		return;
	}

	const auto topic = peerData->isForum() && thread ? thread->asTopic() : nullptr;
	const auto topicId = topic ? topic->rootId().bare : 0;

	addCallback(Window::PeerMenuCallback::Args{
		.text = u"LuxuryGram"_q,
		.handler = nullptr,
		.icon = &st::menuIconGroupReactions,
		.fillSubmenu = [=](not_null<Ui::PopupMenu*> menu) {
			const auto addAction = Ui::Menu::CreateAddActionCallback(menu);
			if (showFilters) {
				addAction(
					tr::luxury_ViewFiltersMenuText(tr::now),
					[=]
					{
						sessionController->dialogId = getDialogIdFromPeer(peerData);
						sessionController->showExclude = true;
						sessionController->shadowBan = false;
						sessionController->showSettings(Settings::LuxuryFiltersList::Id());
					},
					&st::menuIconAddToFolder);
			}
			const auto filteredToggleShown = FiltersController::filteredMessagesShown(peerData);
			if (filteredToggleShown) {
				addAction(
					*filteredToggleShown
						? tr::luxury_HideFilteredMessagesMenuText(tr::now)
						: tr::luxury_ShowFilteredMessagesMenuText(tr::now),
					[=]
					{
						FiltersController::toggleFilteredMessagesShown(peerData);
					},
					*filteredToggleShown
						? &st::menuIconCaptionHide
						: &st::menuIconCaptionShow);
			}
			if (saveDeletedMessages) {
				addAction(
					tr::luxury_ViewDeletedMenuText(tr::now),
					[=]
					{
						if (const auto window = sessionController->session().tryResolveWindow()) {
							window->showSection(std::make_shared<MessageHistory::SectionMemento>(
								peerData,
								nullptr,
								topicId));
						}
					},
					&st::menuIconArchive);
				if (showFilters || filteredToggleShown.value_or(false)) addAction({ .isSeparator = true });
				addAction({
					.text = tr::luxury_ClearDeletedMenuText(tr::now),
					.handler = ClearDeletedMessagesHandler(sessionController, peerData, topicId),
					.icon = &st::menuIconClearAttention,
					.isAttention = true,
				});
			}
		},
	});
}

void AddJumpToBeginningAction(PeerData *peerData,
							  Data::Thread *thread,
							  not_null<Window::SessionController*> sessionController,
							  const Window::PeerMenuCallback &addCallback) {
	if (!peerData) {
		return;
	}
	const auto user = peerData->asUser();
	const auto group = peerData->isChat() ? peerData->asChat() : nullptr;
	const auto chat = peerData->isMegagroup()
						  ? peerData->asMegagroup()
						  : peerData->isChannel()
								? peerData->asChannel()
								: nullptr;
	const auto topic = peerData->isForum() && thread ? thread->asTopic() : nullptr;
	if (!user && !group && !chat && !topic) {
		return;
	}
	if (topic && topic->creating()) {
		return;
	}

	const auto controller = sessionController;
	const auto jumpToDate = [=](auto history, auto callback)
	{
		const auto weak = base::make_weak(controller);
		controller->session().api().resolveJumpToDate(
			history,
			QDate(2013, 8, 1),
			[=](not_null<PeerData*> peer, MsgId id)
			{
				if (weak.get()) {
					// API returns 0 if message "Channel created" (ID: 1) was deleted, which scrolls to the bottom
					if (id.bare == 0) {
						id = MsgId(2);
					}
					callback(peer, id);
				}
			});
	};

	const auto showPeerHistory = [=](auto peer, MsgId id)
	{
		controller->showPeerHistory(
			peer,
			Window::SectionShow::Way::Forward,
			id);
	};

	const auto showTopic = [=](auto topic, MsgId id)
	{
		controller->showTopic(
			topic,
			id,
			Window::SectionShow::Way::Forward);
	};

	addCallback(
		tr::luxury_JumpToBeginning(tr::now),
		[=]
		{
			if (user) {
				jumpToDate(controller->session().data().history(user), showPeerHistory);
			} else if (group && !chat) {
				jumpToDate(controller->session().data().history(group), showPeerHistory);
			} else if (chat && !topic) {
				if (!chat->migrateFrom() && chat->availableMinId() == 1) {
					showPeerHistory(chat, 1);
				} else {
					jumpToDate(controller->session().data().history(chat), showPeerHistory);
				}
			} else if (topic) {
				if (topic->isGeneral()) {
					showTopic(topic, 1);
				} else {
					jumpToDate(
						topic,
						[=](not_null<PeerData*>, MsgId id)
						{
							showTopic(topic, id);
						});
				}
			}
		},
		&st::luxuryToBeginningMenuIcon);
}

void AddOpenChannelAction(PeerData *peerData,
						  not_null<Window::SessionController*> sessionController,
						  const Window::PeerMenuCallback &addCallback) {
	if (!peerData || !peerData->isMegagroup()) {
		return;
	}

	const auto chat = peerData->asMegagroup()->discussionLink();
	if (!chat) {
		return;
	}

	addCallback(
		tr::lng_context_open_channel(tr::now),
		[=]
		{
			sessionController->showPeerHistory(chat, Window::SectionShow::Way::Forward);
		},
		&st::menuIconChannel);
}

void AddShadowBanAction(PeerData *peerData,
						const Window::PeerMenuCallback &addCallback) {
	const auto &settings = LuxurySettings::getInstance();
	if (!peerData || !(peerData->isUser() || peerData->isBroadcast()) || !settings.filtersEnabled()) {
		return;
	}

	if (const auto user = peerData->asUser()) {
		if (user->isSelf()) {
			return;
		}
	}

	const auto realId = getDialogIdFromPeer(peerData);
	const auto shadowBanned = LuxurySettings::getInstance().isShadowBanned(realId);
	const auto toggleShadowBan = [=]
	{
		if (shadowBanned) {
			LuxurySettings::getInstance().removeShadowBan(realId);
		} else {
			LuxurySettings::getInstance().addShadowBan(realId);
		}
	};

	addCallback({
		.text = (shadowBanned
					 ? tr::luxury_FiltersQuickUnshadowBan(tr::now)
					 : tr::luxury_FiltersQuickShadowBan(tr::now)),
		.handler = toggleShadowBan,
		.icon = shadowBanned ? &st::menuIconShowInChat : &st::menuIconStealth,
	});
}

void AddDeleteOwnMessagesAction(PeerData *peerData,
								Data::ForumTopic *topic,
								not_null<Window::SessionController*> sessionController,
								const Window::PeerMenuCallback &addCallback) {
	if (topic) {
		return;
	}
	if (const auto chat = peerData->asChat()) {
		if (!chat->amIn()) {
			return;
		}
	} else if (const auto channel = peerData->asChannel()) {
		if (!channel->isMegagroup() || !channel->amIn()) {
			return;
		}
	} else {
		return;
	}
	addCallback(
		tr::luxury_DeleteOwnMessages(tr::now),
		DeleteMyMessagesHandler(sessionController, peerData),
		&st::menuIconTTL);
}

void AddHistoryAction(not_null<Ui::PopupMenu*> menu, HistoryItem *item) {
	if (item->hideEditedBadge()) {
		return;
	}

	const auto edited = item->Get<HistoryMessageEdited>();
	if (!edited) {
		return;
	}

	const auto has = LuxuryMessages::hasRevisions(item);
	if (!has) {
		return;
	}

	menu->addAction(
		tr::luxury_EditsHistoryMenuText(tr::now),
		[=]
		{
			if (const auto window = item->history()->session().tryResolveWindow()) {
				window->showSection(
					std::make_shared<MessageHistory::SectionMemento>(item->history()->peer, item, 0));
			}
		},
		&st::luxuryEditsHistoryIcon);
}

void AddTranslateMessageAction(
		not_null<Ui::PopupMenu*> menu,
		not_null<Window::SessionController*> controller,
		HistoryItem *item,
		TextWithEntities text,
		bool hasCopyRestriction) {
	if (!item || text.text.isEmpty()) {
		return;
	}
	const auto to = Core::App().settings().translateTo();
	const auto title = tr::lng_translate_bar_to(
		tr::now,
		lt_name,
		Ui::LanguageName(to));
	for (const auto action : menu->actions()) {
		if (action->text() == title) {
			return;
		}
	}
	const auto weak = base::make_weak(controller);
	const auto itemId = item->fullId();
	menu->addAction(title, [=, text = std::move(text)]() mutable {
		if (const auto strong = weak.get()) {
			const auto item = strong->session().data().message(itemId);
			if (!item) {
				return;
			}
			strong->show(Box(
				Ui::TranslateBoxTo,
				item->history()->peer,
				MsgId(),
				std::move(text),
				hasCopyRestriction,
				to));
		}
	}, &st::menuIconTranslate);
}

void AddHideMessageAction(not_null<Ui::PopupMenu*> menu, HistoryItem *item) {
	const auto &settings = LuxurySettings::getInstance();
	if (!needToShowItem(settings.showHideMessageInContextMenu())) {
		return;
	}

	if (item->history()->peer->isSelf()) {
		return;
	}

	const auto history = item->history();
	const auto owner = &history->owner();
	menu->addAction(
		tr::luxury_ContextHideMessage(tr::now),
		[=]()
		{
			const auto ids = owner->itemOrItsGroup(item);
			for (const auto &fullId : ids) {
				if (const auto current = owner->message(fullId)) {
					LuxuryState::hide(current);
					current->destroy();
				}
			}
			history->requestChatListMessage();
		},
		&st::menuIconClear);
}

void AddUserMessagesAction(not_null<Ui::PopupMenu*> menu, HistoryItem *item) {
	const auto &settings = LuxurySettings::getInstance();
	if (!needToShowItem(settings.showUserMessagesInContextMenu())) {
		return;
	}

	if (!item->isHistoryEntry()) {
		return;
	}

	if (item->history()->peer->isChat() || item->history()->peer->isMegagroup()) {
		menu->addAction(
			tr::luxury_UserMessagesMenuText(tr::now),
			[=]
			{
				if (const auto controller = item->history()->session().tryResolveWindow()) {
					const auto peer = item->history()->peer;
					const auto key = (peer && !peer->isUser())
										 ? item->topic()
											   ? Dialogs::Key{item->topic()}
											   : Dialogs::Key{item->history()}
										 : Dialogs::Key{item->history()};
					controller->searchInChat(key, item->from());
				}
			},
			&st::menuIconTTL);
	}
}

void AddMessageDetailsAction(not_null<Ui::PopupMenu*> menu, HistoryItem *item) {
	const auto &settings = LuxurySettings::getInstance();
	if (!needToShowItem(settings.showMessageDetailsInContextMenu())) {
		return;
	}

	if (item->isLocal()) {
		return;
	}

	const auto view = item->mainView();
	const auto forwarded = item->Get<HistoryMessageForwarded>();
	const auto views = item->Get<HistoryMessageViews>();
	const auto media = item->media();

	const auto isSticker = media && media->document() && media->document()->sticker();

	const auto emojiPacks = HistoryView::CollectEmojiPacks(item, HistoryView::EmojiPacksSource::Message);
	auto containsSingleCustomEmojiPack = emojiPacks.size() == 1;
	if (!containsSingleCustomEmojiPack && emojiPacks.size() > 1) {
		const auto author = emojiPacks.front().id >> 32;
		auto sameAuthor = true;
		for (const auto &pack : emojiPacks) {
			if (pack.id >> 32 != author) {
				sameAuthor = false;
				break;
			}
		}

		containsSingleCustomEmojiPack = sameAuthor;
	}

	const auto isForwarded = forwarded && !forwarded->story && forwarded->psaType.isEmpty();

	const auto messageId = QString::number(item->id.bare);
	const auto messageDate = base::unixtime::parse(item->date());
	const auto messageEditDate = base::unixtime::parse(view ? view->displayedEditDate() : TimeId(0));

	const auto messageForwardedDate =
		isForwarded && forwarded
			? base::unixtime::parse(forwarded->originalDate)
			: QDateTime();

	const auto
		messageViews = item->hasViews() && item->viewsCount() > 0 ? QString::number(item->viewsCount()) : QString();
	const auto messageForwards = views && views->forwardsCount > 0 ? QString::number(views->forwardsCount) : QString();

	const auto mediaSize = media ? getMediaSize(item) : QString();
	const auto mediaMime = media ? getMediaMime(item) : QString();
	// todo: bitrate (?)
	const auto mediaName = media ? getMediaName(item) : QString();
	const auto mediaResolution = media ? getMediaResolution(item) : QString();
	const auto mediaDC = media ? getMediaDC(item) : QString();

	const auto hasAnyPostField =
		!messageViews.isEmpty() ||
		!messageForwards.isEmpty();

	const auto hasAnyMediaField =
		!mediaSize.isEmpty() ||
		!mediaMime.isEmpty() ||
		!mediaName.isEmpty() ||
		!mediaResolution.isEmpty() ||
		!mediaDC.isEmpty();

	const auto callback = Ui::Menu::CreateAddActionCallback(menu);

	callback(Window::PeerMenuCallback::Args{
		.text = tr::luxury_MessageDetailsPC(tr::now),
		.handler = nullptr,
		.icon = &st::menuIconInfo,
		.fillSubmenu = [&](not_null<Ui::PopupMenu*> menu2)
		{
			if (hasAnyPostField) {
				if (!messageViews.isEmpty()) {
					menu2->addAction(Ui::ContextActionWithSubText(
						menu2->menu(),
						st::menuIconShowInChat,
						tr::luxury_MessageDetailsViewsPC(tr::now),
						messageViews
					));
				}

				if (!messageForwards.isEmpty()) {
					menu2->addAction(Ui::ContextActionWithSubText(
						menu2->menu(),
						st::menuIconViewReplies,
						tr::luxury_MessageDetailsSharesPC(tr::now),
						messageForwards
					));
				}

				menu2->addSeparator();
			}

			menu2->addAction(Ui::ContextActionWithSubText(
				menu2->menu(),
				st::menuIconInfo,
				QString("ID"),
				messageId
			));

			menu2->addAction(Ui::ContextActionWithSubText(
				menu2->menu(),
				st::menuIconSchedule,
				tr::luxury_MessageDetailsDatePC(tr::now),
				formatDateTime(messageDate)
			));

			if (view && view->displayedEditDate()) {
				menu2->addAction(Ui::ContextActionWithSubText(
					menu2->menu(),
					st::menuIconEdit,
					tr::luxury_MessageDetailsEditedDatePC(tr::now),
					formatDateTime(messageEditDate)
				));
			}

			if (isForwarded) {
				menu2->addAction(Ui::ContextActionWithSubText(
					menu2->menu(),
					st::menuIconTTL,
					tr::luxury_MessageDetailsForwardedDatePC(tr::now),
					formatDateTime(messageForwardedDate)
				));
			}

			if (media && hasAnyMediaField) {
				menu2->addSeparator();

				if (!mediaSize.isEmpty()) {
					menu2->addAction(Ui::ContextActionWithSubText(
						menu2->menu(),
						st::menuIconDownload,
						tr::luxury_MessageDetailsFileSizePC(tr::now),
						mediaSize
					));
				}

				if (!mediaMime.isEmpty()) {
					const auto mime = Core::MimeTypeForName(mediaMime);

					menu2->addAction(Ui::ContextActionWithSubText(
						menu2->menu(),
						st::menuIconShowAll,
						tr::luxury_MessageDetailsMimeTypePC(tr::now),
						mime.name()
					));
				}

				if (!mediaName.isEmpty()) {
					auto const shortified = mediaName.length() > 20 ? "…" + mediaName.right(20) : mediaName;

					menu2->addAction(Ui::ContextActionWithSubText(
						menu2->menu(),
						st::luxuryEditsHistoryIcon,
						tr::luxury_MessageDetailsFileNamePC(tr::now),
						shortified,
						[=]
						{
							QGuiApplication::clipboard()->setText(mediaName);
						}
					));
				}

				if (!mediaResolution.isEmpty()) {
					menu2->addAction(Ui::ContextActionWithSubText(
						menu2->menu(),
						st::menuIconStats,
						tr::luxury_MessageDetailsResolutionPC(tr::now),
						mediaResolution
					));
				}

				if (!mediaDC.isEmpty()) {
					menu2->addAction(Ui::ContextActionWithSubText(
						menu2->menu(),
						st::menuIconBoosts,
						tr::luxury_MessageDetailsDatacenterPC(tr::now),
						mediaDC
					));
				}

				if (isSticker) {
					const auto authorId = getUserIdFromPackId(media->document()->sticker()->set.id);

					if (authorId != 0) {
						menu2->addAction(Ui::ContextActionStickerAuthor(
							menu2->menu(),
							&item->history()->session(),
							authorId
						));
					}
				}
			}

			if (containsSingleCustomEmojiPack) {
				const auto authorId = getUserIdFromPackId(emojiPacks.front().id);

				if (authorId != 0) {
					menu2->addAction(Ui::ContextActionStickerAuthor(
						menu2->menu(),
						&item->history()->session(),
						authorId
					));
				}
			}
		},
	});
}

void AddRepeatMessageAction(not_null<Ui::PopupMenu*> menu, HistoryItem *item, HistoryView::Context context) {
	const auto &settings = LuxurySettings::getInstance();
	if (!needToShowItem(settings.showRepeatMessageInContextMenu())) {
		return;
	}

	if (!item || !item->isHistoryEntry() || item->isService() || item->isLocal() || !item->allowsForward() || item->id <= 0) {
		return;
	}

	const auto history = item->history();
	const auto peer = history->peer;
	if (!peer->isUser() && !peer->isChat() && !peer->isMegagroup() && !peer->isGigagroup()) {
		return;
	}

	const auto itemId = item->fullId();
	const auto session = &history->session();

	menu->addAction(
		tr::luxury_RepeatMessage(tr::now),
		[=]
		{
			const auto sendAs = (peer->isUser() || peer->isChat() || history->peer->isMonoforum())
				? nullptr
				: session->sendAsPeers().resolveChosen(peer).get();

			const auto inRepliesView = (context == HistoryView::Context::Replies);
			const auto replyTo = item->replyTo();
			const auto hasReply = replyTo.messageId.msg != 0;
			const auto shiftPressed = base::IsShiftPressed();

			const auto useNoQuote = shiftPressed || (inRepliesView && !history->peer->isForum());
			const auto preserveReply = inRepliesView ? hasReply : (hasReply && shiftPressed);

			const auto currentItem = history->owner().message(itemId);
			if (!currentItem) {
				return;
			}

			auto action = Api::SendAction(
				history,
				Api::SendOptions{ .sendAs = sendAs });
			if (history->peer->amMonoforumAdmin()) {
				action.replyTo.monoforumPeerId = currentItem->sublistPeerId();
			}
			action.clearDraft = false;

			applyGhostScheduling(session, action.options);

			if (currentItem->topic()) {
				action.replyTo.topicRootId = currentItem->topicRootId();
			}

			if (preserveReply) {
				action.replyTo.messageId = replyTo.messageId;
			}

			if (useNoQuote) {
				auto message = ApiWrap::MessageToSend(action);
				const auto media = currentItem->media();
				if (!currentItem->originalText().text.isEmpty()) {
					message.textWithTags = {
						currentItem->originalText().text,
						TextUtilities::ConvertEntitiesToTextTags(
							currentItem->originalText().entities),
					};
				}
				if (media) {
					if (const auto photo = media->photo()) {
						Api::SendExistingPhoto(std::move(message), photo);
					} else if (const auto document = media->document()) {
						Api::SendExistingDocument(std::move(message), document);
					}
				} else {
					session->api().sendMessage(std::move(message));
				}
			} else {
				const auto forwardDraft = Data::ForwardDraft{
					.ids = MessageIdsList{ itemId },
					.options = Data::ForwardOptions::PreserveInfo,
				};
				auto resolvedDraft = history->resolveForwardDraft(forwardDraft);

				if (LuxuryForward::isFullLuxuryForwardNeeded(currentItem)) {
					LuxuryForward::forwardMessages(session, action, resolvedDraft);
				} else if (LuxuryForward::isLuxuryForwardNeeded(currentItem)) {
					LuxuryForward::intelligentForward(session, action, resolvedDraft);
				} else {
					session->api().forwardMessages(std::move(resolvedDraft), action, [] {});
				}
			}
		},
		&st::luxuryRepeatMenuIcon);
}

void AddReadUntilAction(not_null<Ui::PopupMenu*> menu, HistoryItem *item) {
	const auto group = item->history()->owner().groups().find(item);
	const auto readItem = group ? group->items.back().get() : item;
	if (!readItem->isHistoryEntry()
		|| readItem->isLocal()
		|| readItem->out()
		|| readItem->isDeleted()
		|| readItem->history()->peer->isSelf()) {
		return;
	}

	const auto &ghost = LuxurySettings::ghost(&readItem->history()->session());
	if (ghost.sendReadMessages()) {
		return;
	}

	menu->addAction(
		tr::luxury_ReadUntilMenuText(tr::now),
		[=]
		{
			readHistory(readItem);
			const auto media = readItem->media();
			if (media
				&& media->ttlSeconds() <= 0
				&& readItem->unsupportedTTL() <= 0
				&& !readItem->out()) {
				const auto ids = MTP_vector<MTPint>(1, MTP_int(readItem->id));
				if (const auto channel = readItem->history()->peer->asChannel()) {
					readItem->history()->session().api().request(
						MTPchannels_ReadMessageContents(
						channel->inputChannel(),
						ids)).send();
				} else {
					readItem->history()->session().api().request(
						MTPmessages_ReadMessageContents(ids)
					).done([=](const MTPmessages_AffectedMessages &result)
					{
						readItem->history()->session().api()
							.applyAffectedMessages(
							readItem->history()->peer,
							result);
					}).send();
				}
				readItem->markContentsRead();
			}
		},
		&st::menuIconShowInChat);
}

void AddBurnAction(not_null<Ui::PopupMenu*> menu, HistoryItem *item) {
	if (!item->media() || (item->media()->ttlSeconds() <= 0 && item->unsupportedTTL() <= 0) || item->out() ||
		!item->hasUnreadMediaFlag()) {
		return;
	}

	menu->addAction(
		tr::luxury_ExpireMediaContextMenuText(tr::now),
		[=]
		{
			const auto ids = MTP_vector<MTPint>(1, MTP_int(item->id));

			item->history()->session().api().request(MTPmessages_ReadMessageContents(
					ids
				)).done([=](const MTPmessages_AffectedMessages &result)
				{
					item->history()->session().api().applyAffectedMessages(
						item->history()->peer,
						result);
					item->markContentsRead();
				}).send();
		},
		&st::menuIconTTLAny);
}

void AddCreateFilterAction(not_null<Ui::PopupMenu*> menu,
						   not_null<Window::SessionController*> controller,
						   HistoryItem *item,
						   const QString &selectedText) {
	const auto &settings = LuxurySettings::getInstance();
	if (!needToShowItem(settings.showAddFilterInContextMenu()) || !settings.filtersEnabled()) {
		return;
	}

	if (!item || selectedText.isEmpty()) {
		return;
	}

	menu->addAction(
		tr::luxury_RegexFilterQuickAdd(tr::now),
		[=]
		{
			RegexFilter filter;
			filter.text = selectedText.toStdString();
			filter.enabled = true;
			filter.caseInsensitive = true;
			filter.reversed = false;

			controller->show(Settings::RegexEditBox(
				&filter,
				getDialogIdFromPeer(item->history()->peer),
				true));
		},
		&st::menuIconAddToFolder);
}

} // namespace LuxuryUi
