// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

#include "apiwrap.h"
#include "base/random.h"
#include "base/weak_ptr.h"
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
using WeakSession = base::weak_ptr<Main::Session>;

QString pathForSave(not_null<Main::Session*> session);
QString filePath(not_null<Main::Session*> session, const Data::Media *media);
void sendMessageSync(
	WeakSession session,
	Api::MessageToSend &&message,
	const Cancelled &cancelled);

void sendDocumentSync(WeakSession session,
					  Ui::PreparedGroup &group,
					  SendMediaType type,
					  TextWithTags &&caption,
					  const Api::SendAction &action,
					  const Cancelled &cancelled);

void sendStickerSync(WeakSession session,
					 Api::MessageToSend &&message,
					 FullMsgId itemId,
					 const Cancelled &cancelled);
void loadPhotoSync(
	WeakSession session,
	FullMsgId itemId,
	const QString &path,
	const Cancelled &cancelled);
void loadDocumentSync(
	WeakSession session,
	FullMsgId itemId,
	const QString &path,
	qint64 expectedSize,
	const Cancelled &cancelled);
void forwardMessagesSync(WeakSession session,
						 const std::vector<FullMsgId> &itemIds,
						 const ApiWrap::SendAction &action,
						 Data::ForwardOptions options,
						 const Cancelled &cancelled);
void sendVoiceSync(WeakSession session,
				   const QByteArray &data,
				   int64_t duration,
				   bool video,
				   Api::MessageToSend &&message,
				   const Cancelled &cancelled);
}
