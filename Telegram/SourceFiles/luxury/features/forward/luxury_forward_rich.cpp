// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "luxury/features/forward/luxury_forward_rich.h"

#include "luxury/features/forward/luxury_sync.h"
#include "luxury/utils/telegram_helpers.h"
#include "base/flat_map.h"
#include "data/data_document.h"
#include "data/data_file_origin.h"
#include "data/data_peer.h"
#include "data/data_photo.h"
#include "data/data_session.h"
#include "history/history.h"
#include "iv/iv_rich_message_serializer.h"
#include "iv/iv_rich_page.h"
#include "storage/localimageloader.h"

#include <atomic>

namespace LuxuryForward {
namespace {

using Block = Iv::RichPage::Block;
using BlockKind = Iv::RichPage::BlockKind;

struct RichMedia
{
	base::flat_map<PhotoId, PhotoId> photos;
	base::flat_map<DocumentId, DocumentId> documents;
};

[[nodiscard]] PhotoData *resolvePhoto(not_null<Main::Session*> session, PhotoId id, PhotoData *photo) {
	return photo ? photo : (id ? session->data().photo(id).get() : nullptr);
}

[[nodiscard]] DocumentData *resolveDocument(not_null<Main::Session*> session,
											DocumentId id,
											DocumentData *document) {
	return document ? document : (id ? session->data().document(id).get() : nullptr);
}

[[nodiscard]] bool isSerializableKind(BlockKind kind) {
	switch (kind) {
	case BlockKind::Unsupported:
	case BlockKind::AuthorDate:
	case BlockKind::Embed:
	case BlockKind::EmbedPost:
	case BlockKind::Channel:
	case BlockKind::RelatedArticles:
		return false;
	default:
		return true;
	}
}

void collectMedia(not_null<Main::Session*> session, const std::vector<Block> &blocks, RichMedia &media) {
	const auto addPhoto = [&](PhotoId id, PhotoData *photo)
	{
		if (const auto resolved = resolvePhoto(session, id, photo)) {
			media.photos.emplace(resolved->id, resolved->id);
		}
	};
	const auto addDocument = [&](DocumentId id, DocumentData *document)
	{
		if (const auto resolved = resolveDocument(session, id, document)) {
			media.documents.emplace(resolved->id, resolved->id);
		}
	};

	for (const auto &block : blocks) {
		if (!isSerializableKind(block.kind)) {
			continue;
		}

		switch (block.kind) {
		case BlockKind::Photo:
			addPhoto(block.photoId, block.photo);
			break;
		case BlockKind::Video:
		case BlockKind::Audio:
		case BlockKind::File:
			addDocument(block.documentId, block.document);
			break;
		case BlockKind::GroupedMedia:
			for (const auto &item : block.mediaItems) {
				if (item.kind == BlockKind::Photo) {
					addPhoto(item.photoId, item.photo);
				} else if (item.kind == BlockKind::Video) {
					addDocument(item.documentId, item.document);
				}
			}
			break;
		default:
			break;
		}

		collectMedia(session, block.blocks, media);
		for (const auto &item : block.listItems) {
			collectMedia(session, item.blocks, media);
		}
	}
}

[[nodiscard]] RichMedia reuploadMedia(
		LuxurySync::WeakSession session,
		PeerId peerId,
		Data::FileOrigin origin,
		const RichMedia &source,
		const Fn<bool()> &cancelled) {
	auto result = RichMedia();
	for (const auto &[id, photoId] : source.photos) {
		if (cancelled()) return result;
		const auto path = LuxurySync::loadPhotoSync(session, photoId, origin, cancelled);
		const auto uploaded = LuxurySync::uploadFileSync(
			session, peerId, path, SendMediaType::Photo, false, {}, cancelled);
		if (uploaded.photo) result.photos.emplace(id, uploaded.photo);
	}
	for (const auto &[id, documentId] : source.documents) {
		if (cancelled()) return result;
		auto playable = false;
		auto name = QString();
		crl::on_main_sync([&] {
			if (cancelled()) return;
			if (const auto strong = session.get()) {
				const auto document = strong->data().document(documentId);
				playable = document->isVideoFile() || document->isGifv()
					|| document->isSong() || document->isAudioFile()
					|| document->isVoiceMessage();
				name = LuxurySync::documentFileName(document);
			}
		});
		const auto path = LuxurySync::loadDocumentSync(session, documentId, origin, cancelled);
		const auto uploaded = LuxurySync::uploadFileSync(
			session, peerId, path, SendMediaType::File, !playable, name, cancelled);
		if (uploaded.document) result.documents.emplace(id, uploaded.document);
	}
	return result;
}

[[nodiscard]] bool runOnMainSync(
		LuxurySync::WeakSession session,
		const Fn<bool()> &cancelled,
		Fn<void(not_null<Main::Session*>)> callback) {
	auto ran = false;
	crl::on_main_sync([&] {
		if (cancelled()) return;
		if (const auto strong = session.get()) {
			callback(strong);
			ran = true;
		}
	});
	return ran;
}

void sanitizeRichText(Iv::RichPage::RichText &text) {
	text.anchorId = QString();
	text.anchorIds.clear();
	const auto autolink = [](const EntityInText &entity)
	{
		switch (entity.type()) {
		case EntityType::Mention:
		case EntityType::Hashtag:
		case EntityType::BotCommand:
		case EntityType::Cashtag:
		case EntityType::Url:
		case EntityType::Email:
		case EntityType::Phone:
		case EntityType::BankCard:
			return true;
		default:
			return false;
		}
	};
	const auto removed = std::ranges::remove_if(text.text.entities, autolink);
	text.text.entities.erase(removed.begin(), removed.end());
}

// A button the serializer cannot write fails the whole page, which then
// goes out as plain text: drop the button instead, keep the page.
[[nodiscard]] bool isSerializableButton(const HistoryMessageMarkupButton &button) {
	using Type = HistoryMessageMarkupButton::Type;
	switch (button.type) {
	case Type::Url:
	case Type::CopyText:
	case Type::Disabled:
		return true;
	case Type::UserProfile:
		return button.data.toULongLong() != 0;
	default:
		return false;
	}
}

void pruneBlocks(not_null<Main::Session*> session, std::vector<Block> &blocks, const RichMedia &remap);

[[nodiscard]] bool prepareBlock(not_null<Main::Session*> session, Block &block, const RichMedia &remap) {
	if (!isSerializableKind(block.kind)) {
		return false;
	}

	const auto remapPhoto = [&](PhotoId &id, PhotoData *&photo)
	{
		const auto resolved = resolvePhoto(session, id, photo);
		const auto i = resolved ? remap.photos.find(resolved->id) : remap.photos.end();
		if (i == remap.photos.end()) {
			return false;
		}
		photo = session->data().photo(i->second).get();
		id = i->second;
		return true;
	};
	const auto remapDocument = [&](DocumentId &id, DocumentData *&document)
	{
		const auto resolved = resolveDocument(session, id, document);
		const auto i = resolved ? remap.documents.find(resolved->id) : remap.documents.end();
		if (i == remap.documents.end()) {
			return false;
		}
		document = session->data().document(i->second).get();
		id = i->second;
		return true;
	};

	switch (block.kind) {
	case BlockKind::Photo:
		if (!remapPhoto(block.photoId, block.photo)) {
			return false;
		}
		break;
	case BlockKind::Video:
	case BlockKind::Audio:
	case BlockKind::File:
		if (!remapDocument(block.documentId, block.document)) {
			return false;
		}
		break;
	case BlockKind::ButtonRow: {
		auto &buttons = block.buttons;
		const auto removed = std::ranges::remove_if(buttons, [](
				const Iv::RichPage::Button &button) {
			return !isSerializableButton(button.button);
		});
		buttons.erase(removed.begin(), removed.end());
		if (buttons.empty()) {
			return false;
		}
		for (auto &button : buttons) {
			sanitizeRichText(button.text);
		}
		break;
	}
	case BlockKind::GroupedMedia: {
		auto &items = block.mediaItems;
		for (auto i = items.begin(); i != items.end();) {
			auto kept = false;
			if (i->kind == BlockKind::Photo) {
				kept = remapPhoto(i->photoId, i->photo);
			} else if (i->kind == BlockKind::Video) {
				kept = remapDocument(i->documentId, i->document);
			}
			i = kept ? (i + 1) : items.erase(i);
		}
		if (items.empty()) {
			return false;
		}
		break;
	}
	case BlockKind::Anchor:
		if (block.anchorId.isEmpty()) {
			return false;
		}
		break;
	case BlockKind::Quote:
		if (block.pullquote && !block.blocks.empty()) {
			return false;
		}
		break;
	case BlockKind::Code:
		if (!block.blocks.empty()) {
			return false;
		}
		break;
	case BlockKind::Map:
		if (block.zoom <= 0) {
			return false;
		}
		break;
	default:
		break;
	}

	sanitizeRichText(block.text);
	sanitizeRichText(block.caption);
	if (block.kind != BlockKind::Anchor) {
		block.anchorId = QString();
	}
	for (auto &row : block.tableRows) {
		for (auto &cell : row.cells) {
			sanitizeRichText(cell.text);
		}
	}

	pruneBlocks(session, block.blocks, remap);
	for (auto &item : block.listItems) {
		sanitizeRichText(item.text);
		item.anchorId = QString();
		pruneBlocks(session, item.blocks, remap);
	}
	return true;
}

void pruneBlocks(not_null<Main::Session*> session, std::vector<Block> &blocks, const RichMedia &remap) {
	auto write = blocks.begin();
	for (auto read = blocks.begin(); read != blocks.end(); ++read) {
		if (prepareBlock(session, *read, remap)) {
			if (write != read) {
				*write = std::move(*read);
			}
			++write;
		}
	}
	blocks.erase(write, blocks.end());
}

} // namespace

bool forwardRichMessage(
	base::weak_ptr<Main::Session> session,
	FullMsgId itemId,
	const Api::SendAction &action,
	Fn<bool()> cancelled) {
	const auto active = std::make_shared<std::atomic_bool>(true);
	const auto lifetime = std::make_shared<rpl::lifetime>();
	const auto stopped = [=] {
		return !active->load() || !session.get() || (cancelled && cancelled());
	};
	crl::on_main_sync([&] {
		if (const auto strong = session.get()) {
			strong->data().sessionDataAboutToBeCleared() | rpl::on_next([=] {
				active->store(false);
			}, *lifetime);
		}
	});
	const auto cleanup = gsl::finally([&] {
		active->store(false);
		crl::on_main_sync([&] { lifetime->destroy(); });
	});
	if (stopped()) return false;
	const auto source = LuxurySync::loadFullRichPageSync(session, itemId, stopped);
	if (!source || stopped()) return false;

	struct PrepareState
	{
		Iv::RichPage page;
		RichMedia media;
		bool premiumBlocked = false;
		PeerId peerId;
	};
	const auto prepared = std::make_shared<PrepareState>();

	const auto preparedOk = runOnMainSync(session, stopped, [=](not_null<Main::Session*> strong)
	{
		prepared->peerId = action.history->peer->id;
		prepared->page = *source;
		prepared->page.part = false;
		prepared->page.views = 0;
		prepared->premiumBlocked = !strong->premium()
			&& Iv::RichPageUsesPremiumFormatting(prepared->page);
		if (!prepared->premiumBlocked) {
			collectMedia(strong, prepared->page.blocks, prepared->media);
		}
	});
	if (!preparedOk || prepared->premiumBlocked || stopped()) {
		return false;
	}

	const auto uploaded = reuploadMedia(
		session,
		prepared->peerId,
		itemId,
		prepared->media,
		stopped);
	if (stopped()) {
		return false;
	}

	const auto serialized = std::make_shared<std::optional<MTPInputRichMessage>>();
	const auto serializedOk = runOnMainSync(session, stopped, [=](not_null<Main::Session*> strong)
	{
		pruneBlocks(strong, prepared->page.blocks, uploaded);
		if (prepared->page.blocks.empty()) {
			return;
		}
		const auto result = Iv::SerializeInputRichMessage(
			strong,
			prepared->page,
			Iv::SerializeInputRichMessageMode::FinalSubmit);
		if (result.status == Iv::SerializeInputRichMessageStatus::Success) {
			*serialized = result.value;
		}
	});
	if (!serializedOk || !serialized->has_value() || stopped()) {
		return false;
	}

	return LuxurySync::sendRichMessageSync(session, **serialized, action, stopped);
}

} // namespace LuxuryForward
