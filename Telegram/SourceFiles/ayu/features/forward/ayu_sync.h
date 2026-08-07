// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

#include "apiwrap.h"
#include "base/random.h"
#include "data/data_document.h"
#include "data/data_media_types.h"
#include "data/data_photo.h"
#include "history/history_item.h"
#include "storage/file_download.h"
#include "storage/file_upload.h"
#include "storage/storage_account.h"
#include "ui/chat/attach/attach_prepare.h"

namespace LuxurySync {

using Cancelled = Fn<bool()>;

QString pathForSave(not_null<Main::Session*> session);
QString filePath(not_null<Main::Session*> session, const Data::Media *media);
void loadDocuments(
	not_null<Main::Session*> session,
	const std::vector<not_null<HistoryItem*>> &items,
	const Cancelled &cancelled);
void sendMessageSync(
	not_null<Main::Session*> session,
	Api::MessageToSend &&message,
	const Cancelled &cancelled);

void sendDocumentSync(not_null<Main::Session*> session,
					  Ui::PreparedGroup &group,
					  SendMediaType type,
					  TextWithTags &&caption,
					  const Api::SendAction &action,
					  const Cancelled &cancelled);

void sendStickerSync(not_null<Main::Session*> session,
					 Api::MessageToSend &&message,
					 not_null<DocumentData*> document,
					 const Cancelled &cancelled);
void loadPhotoSync(
	not_null<Main::Session*> session,
	const std::pair<not_null<PhotoData*>, FullMsgId> &photos,
	const Cancelled &cancelled);
void loadDocumentSync(
	not_null<Main::Session*> session,
	DocumentData *data,
	not_null<HistoryItem*> item,
	const Cancelled &cancelled);
void forwardMessagesSync(not_null<Main::Session*> session,
						 const std::vector<not_null<HistoryItem*>> &items,
						 const ApiWrap::SendAction &action,
						 Data::ForwardOptions options,
						 const Cancelled &cancelled);
void sendVoiceSync(not_null<Main::Session*> session,
				   const QByteArray &data,
				   int64_t duration,
				   bool video,
				   Api::MessageToSend &&message,
				   const Cancelled &cancelled);
}
