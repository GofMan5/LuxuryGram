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
#include "data/data_file_origin.h"
#include "data/data_media_types.h"
#include "data/data_peer.h"
#include "data/data_photo.h"
#include "history/history_item.h"
#include "storage/file_download.h"
#include "storage/file_upload.h"
#include "storage/storage_account.h"
#include "ui/chat/attach/attach_prepare.h"

namespace Iv {
struct RichPage;
} // namespace Iv

namespace LuxurySync {

using Cancelled = Fn<bool()>;
using WeakSession = base::weak_ptr<Main::Session>;

struct UploadedFile {
	PhotoData *photo = nullptr;
	DocumentData *document = nullptr;
};

QString pathForSave(not_null<Main::Session*> session);
QString documentFileName(not_null<DocumentData*> document);
QString filePath(not_null<Main::Session*> session, not_null<PhotoData*> photo);
QString filePath(not_null<Main::Session*> session, const Data::Media *media);
qint64 fileSize(const QString &path);
[[nodiscard]] bool sendMessageSync(
	WeakSession session,
	Api::MessageToSend &&message,
	const Cancelled &cancelled);

[[nodiscard]] bool sendDocumentSync(WeakSession session,
					  Ui::PreparedGroup &group,
					  SendMediaType type,
					  TextWithTags &&caption,
					  const Api::SendAction &action,
					  const Cancelled &cancelled);

[[nodiscard]] bool sendStickerSync(WeakSession session,
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
void loadPhotoSync(
	not_null<Main::Session*> session,
	not_null<PhotoData*> photo,
	Data::FileOrigin origin,
	const Cancelled &cancelled);
[[nodiscard]] QString loadDocumentSync(
	not_null<Main::Session*> session,
	not_null<DocumentData*> document,
	Data::FileOrigin origin,
	const Cancelled &cancelled);
[[nodiscard]] bool forwardMessagesSync(WeakSession session,
						 const std::vector<FullMsgId> &itemIds,
						 const ApiWrap::SendAction &action,
						 Data::ForwardOptions options,
						 const Cancelled &cancelled);
[[nodiscard]] bool sendVoiceSync(WeakSession session,
				   const QByteArray &data,
				   int64_t duration,
				   bool video,
				   Api::MessageToSend &&message,
				   const Cancelled &cancelled);
[[nodiscard]] UploadedFile uploadFileSync(
	not_null<Main::Session*> session,
	not_null<PeerData*> peer,
	const QString &path,
	SendMediaType type,
	bool forceFile,
	const QString &displayName = {});
[[nodiscard]] std::shared_ptr<const Iv::RichPage> loadFullRichPageSync(
	not_null<Main::Session*> session,
	FullMsgId itemId);
[[nodiscard]] bool sendRichMessageSync(
	not_null<Main::Session*> session,
	const MTPInputRichMessage &richMessage,
	const Api::SendAction &action);
}
