// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "luxury/features/message_shot/message_shot.h"

#include "qguiapplication.h"
#include "lang_auto.h"
#include "luxury/luxury_settings.h"
#include "luxury/ui/boxes/message_shot_box.h"
#include "base/call_delayed.h"
#include "boxes/abstract_box.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_origin.h"
#include "data/data_forum.h"
#include "data/data_peer.h"
#include "data/data_photo.h"
#include "data/data_photo_media.h"
#include "data/data_session.h"
#include "dialogs/ui/dialogs_video_userpic.h"
#include "history/history.h"
#include "history/history_inner_widget.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "history/view/history_view_element.h"
#include "history/view/media/history_view_media.h"
#include "main/main_session.h"
#include "styles/style_luxury_styles.h"
#include "styles/style_chat.h"
#include "styles/style_layers.h"
#include "ui/empty_userpic.h"
#include "ui/painter.h"
#include "ui/chat/chat_theme.h"
#include "ui/effects/path_shift_gradient.h"
#include "ui/layers/box_content.h"
#include "ui/toast/toast.h"
#include "window/themes/window_theme.h"

namespace LuxuryFeatures::MessageShot {

bool takingShot = false;
bool choosingTheme = false;
constexpr auto kMaxMessageShotPixels = 16 * 1024 * 1024;

bool ignoreRender(RenderPart part) {
	const auto &s = LuxurySettings::getInstance().messageShotSettings();
	return isTakingShot()
		&& ((part == RenderPart::Date && !s.showDate())
			|| (part == RenderPart::Reactions && !s.showReactions())
			|| (part == RenderPart::HeaderDecorations
				&& !s.showHeaderDecorations()));
}

bool simpleQuotesAndReplies() {
	const auto &settings = LuxurySettings::getInstance();
	return isTakingShot()
		? !settings.messageShotSettings().showColorfulReplies()
		: settings.simpleQuotesAndReplies();
}

namespace {

struct AnonymousPeer {
	QString name;
	uint8 colorIndex = 0;
};

// Node based on purpose: AnonymousName() hands out a reference into this and
// PeerData::name() passes it straight to its caller, so taking in the next peer
// must not move the strings already handed out. Emptied once per shot, in
// Make() -- see the comment there for why not per render pass.
std::map<const PeerData*, AnonymousPeer> AnonymousPeers;

[[nodiscard]] const AnonymousPeer *AnonymousEntry(
		not_null<const PeerData*> peer) {
	if (!isAnonymousShot()) {
		return nullptr;
	}
	const auto i = AnonymousPeers.find(peer.get());
	if (i != end(AnonymousPeers)) {
		return &i->second;
	}
	const auto index = int(AnonymousPeers.size()) + 1;
	return &AnonymousPeers.emplace(peer.get(), AnonymousPeer{
		.name = tr::luxury_MessageShotAnonymousName(
			tr::now,
			lt_index,
			QString::number(index)),
		.colorIndex = Ui::EmptyUserpic::ColorIndex(index),
	}).first->second;
}

} // namespace

bool isAnonymousShot() {
	return takingShot
		&& LuxurySettings::getInstance().messageShotSettings().anonymous();
}

const QString *AnonymousName(not_null<const PeerData*> peer) {
	const auto entry = AnonymousEntry(peer);
	return entry ? &entry->name : nullptr;
}

bool setChoosingTheme(bool val) {
	choosingTheme = val;
	return choosingTheme;
}

bool isChoosingTheme() {
	return choosingTheme;
}

class MessageShotDelegate final : public HistoryView::DefaultElementDelegate
{
public:
	MessageShotDelegate(
		not_null<QWidget*> parent,
		not_null<Ui::ChatStyle*> st,
		Fn<void()> update,
		not_null<History*> history);

	bool elementAnimationsPaused() override;
	not_null<Ui::PathShiftGradient*> elementPathShiftGradient() override;
	HistoryView::Context elementContext() override;
	bool elementHideReply(not_null<const HistoryView::Element*> view) override;
	HistoryView::ElementChatMode elementChatMode() override;

private:
	const not_null<QWidget*> _parent;
	const std::unique_ptr<Ui::PathShiftGradient> _pathGradient;
	not_null<History*> _history;
};

MessageShotDelegate::MessageShotDelegate(
	not_null<QWidget*> parent,
	not_null<Ui::ChatStyle*> st,
	Fn<void()> update,
	not_null<History*> history)
	: _parent(parent)
	  , _pathGradient(HistoryView::MakePathShiftGradient(st, update))
	  , _history(history) {
}

bool MessageShotDelegate::elementAnimationsPaused() {
	return _parent->window()->isActiveWindow();
}

auto MessageShotDelegate::elementPathShiftGradient()
	-> not_null<Ui::PathShiftGradient*> {
	return _pathGradient.get();
}

HistoryView::Context MessageShotDelegate::elementContext() {
	return HistoryView::Context::AdminLog;
}

bool MessageShotDelegate::elementHideReply(not_null<const HistoryView::Element*> view) {
	if (const auto reply = view->data()->Get<HistoryMessageReply>()) {
		const auto replyToPeerId = reply->externalPeerId()
									   ? reply->externalPeerId()
									   : _history->peer->id;

		if (reply->fields().manualQuote) {
			return false;
		} else if (replyToPeerId == _history->peer->id) {
			return _history->asForum() && _history->asForum()->topicFor(reply->messageId());
		}
	}
	return false;
}

HistoryView::ElementChatMode MessageShotDelegate::elementChatMode() {
	using Mode = HistoryView::ElementChatMode;
	return Mode::Wide;
}

QImage removeEmptySpaceAround(const QImage &original) {
	if (original.isNull()) {
		return {};
	}

	int minX = original.width();
	int minY = original.height();
	int maxX = 0;
	int maxY = 0;

	for (auto y = 0; y != original.height(); ++y) {
		const auto line = reinterpret_cast<const QRgb*>(
			original.constScanLine(y));
		for (auto x = 0; x != original.width(); ++x) {
			if (qAlpha(line[x]) != 0) {
				minX = std::min(minX, x);
				minY = std::min(minY, y);
				maxX = std::max(maxX, x);
				maxY = std::max(maxY, y);
			}
		}
	}

	if (minX > maxX || minY > maxY) {
		LOG(("Image is fully transparent ?"));
		return {};
	}

	const QRect bounds(minX, minY, maxX - minX + 1, maxY - minY + 1);
	return original.copy(bounds);
}

QImage addPadding(const QImage &original) {
	if (original.isNull()) {
		return {};
	}

	QImage paddedImage(
		original.width() + 2 * st::messageShotPadding * style::DevicePixelRatio(),
		original.height() + 2 * st::messageShotPadding * style::DevicePixelRatio(),
		QImage::Format_ARGB32_Premultiplied
	);
	paddedImage.setDevicePixelRatio(style::DevicePixelRatio());
	paddedImage.fill(Qt::transparent);

	Painter painter(&paddedImage);
	painter.drawImage(st::messageShotPadding, st::messageShotPadding, original);
	painter.end();

	return paddedImage;
}

QColor makeDefaultBackgroundColor() {
	if (Window::Theme::IsNightMode()) {
		return st::boxBg->c.lighter(175);
	}

	return st::boxBg->c.darker(110);
}

std::vector<not_null<HistoryItem*>> ResolveMessages(const ShotConfig &config) {
	auto result = std::vector<not_null<HistoryItem*>>();
	result.reserve(config.messages.size());
	const auto &owner = config.controller->session().data();
	for (const auto &id : config.messages) {
		if (const auto message = owner.message(id)) {
			result.push_back(message);
		}
	}
	return result;
}

void Make(not_null<QWidget*> box, const ShotConfig &config, const Fn<void(QImage&,bool)>& callback) {
	const auto controller = config.controller;
	const auto st = config.st;
	// Resolve first: the ids in the config outlive the messages, so anything
	// deleted since the box was opened has to be dropped before its pointer is
	// touched.
	auto messages = ResolveMessages(config);

	if (messages.empty()) {
		return;
	}

	// Once per shot, not once per pass: render() runs twice when media is still
	// loading, and the Elements cache the from-name by the peer's name version,
	// so a pass that renumbered the peers would disagree with what the first
	// pass had already baked in.
	AnonymousPeers.clear();

	auto delegate = std::make_shared<MessageShotDelegate>(
		box,
		st.get(),
		[=]
		{
			box->update();
		},
		messages.front()->history());

	auto messageIds = std::vector<FullMsgId>();
	messageIds.reserve(messages.size());
	for (const auto &message : messages) {
		messageIds.push_back(message->fullId());
	}

	auto createdViews = std::make_shared<std::unordered_map<not_null<HistoryItem*>, std::shared_ptr<HistoryView::Element>>>();
	createdViews->reserve(messages.size());
	for (const auto &message : messages) {
		createdViews->emplace(message, message->createView(delegate.get()));
	}

	auto getView = [createdViews](not_null<HistoryItem*> msg)
	{
		return createdViews->at(msg).get();
	};

	// recalculate blocks
	if (messages.size() > 1) {
		auto currentMsg = messages[0].get();

		for (auto i = 1; i != messages.size(); ++i) {
			const auto nextMsg = messages[i].get();
			if (getView(nextMsg)->isHidden()) {
				getView(nextMsg)->setDisplayDate(false);
			} else {
				const auto viewDate = getView(currentMsg)->dateTime();
				const auto nextDate = getView(nextMsg)->dateTime();
				getView(nextMsg)->setDisplayDate(nextDate.date() != viewDate.date());
				auto attached = getView(nextMsg)->computeIsAttachToPrevious(getView(currentMsg));
				getView(nextMsg)->setAttachToPrevious(attached, getView(currentMsg));
				getView(currentMsg)->setAttachToNext(attached, getView(nextMsg));
				currentMsg = nextMsg;
			}
		}

		getView(messages[messages.size() - 1])->setAttachToNext(false);
	} else {
		getView(messages[0])->setAttachToPrevious(false);
		getView(messages[0])->setAttachToNext(false);
	}

	struct MediaPreload {
		std::vector<std::shared_ptr<Data::PhotoMedia>> photos;
		std::vector<std::shared_ptr<Data::DocumentMedia>> documents;
	};
	auto preload = std::make_shared<MediaPreload>();

	for (const auto &message : messages) {
		if (!message->media()) continue;
		const auto origin = Data::FileOrigin(message->fullId());
		if (const auto photo = message->media()->photo()) {
			auto media = photo->activeMediaView()
				? photo->activeMediaView()
				: photo->createMediaView();
			if (!media->loaded()) {
				photo->load(origin, LoadFromCloudOrLocal, false);
			}
			preload->photos.push_back(std::move(media));
		} else if (const auto document = message->media()->document()) {
			if (document->hasThumbnail()) {
				auto media = document->activeMediaView()
					? document->activeMediaView()
					: document->createMediaView();
				if (!media->thumbnail()) {
					document->loadThumbnail(origin);
				}
				preload->documents.push_back(std::move(media));
			}
		}
	}

	const auto showBackground = LuxurySettings::getInstance().messageShotSettings().showBackground();
	auto render = [
		=,
		messages = std::move(messages),
		delegate = std::move(delegate),
		sizeRejected = false
	](bool final) mutable
	{
		if (sizeRejected) {
			return;
		}
		for (auto i = 0; i != int(messages.size()); ++i) {
			if (controller->session().data().message(messageIds[i]) != messages[i]) {
				return;
			}
		}
		takingShot = true;
		const auto anonymous = LuxurySettings::getInstance()
			.messageShotSettings().anonymous();
		const auto takingShotGuard = gsl::finally([&] {
			takingShot = false;
			if (anonymous) {
				// HistoryMessageForwarded::text is cached on the item itself, so it
				// is shared with the real chat view, and the layout pass below has
				// just rebuilt it around a pseudonym. create() is a pure function of
				// the item, so calling it again with the shot over restores byte for
				// byte what was there -- widths the real view already cached
				// included, which is why no resize has to be asked for.
				for (const auto &message : messages) {
					if (const auto forwarded
							= message->Get<HistoryMessageForwarded>()) {
						forwarded->create(
							message->Get<HistoryMessageVia>(),
							message);
					}
				}
			}
		});

		// calculate the size of the image
		int width = st::msgMaxWidth + (st::boxPadding.left() + st::boxPadding.right());
		auto height = qint64(0);

		for (int i = 0; i < messages.size(); i++) {
			const auto &message = messages[i];
			const auto view = getView(message);

			if (anonymous) {
				// The guard above puts the real forwarded header back after every
				// pass, so every pass has to lay the message out again to get the
				// pseudonym into it.
				view->setPendingResize();
			}
			view->itemDataChanged(); // refresh reactions
			height += view->resizeGetHeight(width);
			if (LuxurySettings::getInstance().messageShotSettings().revealSpoilers()) {
				view->revealSpoilers();
			}
		}

		const auto pixelWidth = qint64(width) * style::DevicePixelRatio();
		const auto pixelHeight = qint64(height) * style::DevicePixelRatio();
		if (pixelWidth <= 0
			|| pixelHeight <= 0
			|| pixelWidth > (kMaxMessageShotPixels / pixelHeight)) {
			sizeRejected = true;
			Ui::Toast::Show(tr::lng_passport_error_too_large(tr::now));
			return;
		}
		width = int(pixelWidth);
		height = int(pixelHeight);

		// create the image
		QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
		if (image.isNull()) {
			sizeRejected = true;
			Ui::Toast::Show(tr::lng_passport_error_too_large(tr::now));
			return;
		}
		image.setDevicePixelRatio(style::DevicePixelRatio());
		image.fill(Qt::transparent);

		const auto viewport = QRect(0, 0, width, height);

		base::flat_map<not_null<PeerData*>, Ui::PeerUserpicView> userpics;
		base::flat_map<MsgId, Ui::PeerUserpicView> hiddenSenderUserpics;

		Painter p(&image);

		// draw the messages
		int y = 0;
		for (int i = 0; i < messages.size(); i++) {
			const auto &message = messages[i];
			const auto view = getView(message);

			const auto displayUserpic = view->displayFromPhoto() || message->isPost();

			const auto rect = QRect(0, 0, width, view->height());

			auto context = controller->defaultChatTheme()->preparePaintContext(
				st.get(),
				viewport,
				rect,
				rect,
				true);

			p.translate(0, y);
			view->draw(p, context);
			p.translate(0, -y);

			if (displayUserpic) {
				const auto picX = st::msgMargin.left();
				const auto picY = y + view->height() - st::msgPhotoSize;
				const auto from = message->displayFrom();
				const auto pseudonym = from ? AnonymousEntry(from) : nullptr;

				if (pseudonym) {
					// The header says "User 1"; the real photo would say who that is.
					Ui::EmptyUserpic(
						Ui::EmptyUserpic::UserpicColor(pseudonym->colorIndex),
						pseudonym->name
					).paintCircle(p, picX, picY, width, st::msgPhotoSize);
				} else if (from) {
					Dialogs::Ui::PaintUserpic(
						p,
						from,
						nullptr,
						userpics[from],
						picX,
						picY,
						width,
						st::msgPhotoSize,
						context.paused);
				} else if (const auto info = message->displayHiddenSenderInfo()) {
					if (info->customUserpic.empty()) {
						info->emptyUserpic.paintCircle(
							p,
							picX,
							picY,
							width,
							st::msgPhotoSize);
					}
				}
			}

			y += view->height();
		}

		auto result = addPadding(removeEmptySpaceAround(image));
		if (!showBackground) {
			callback(result, final);
			return;
		}

		auto newResult = QImage(result.size(), QImage::Format_ARGB32_Premultiplied);
		newResult.setDevicePixelRatio(style::DevicePixelRatio());
		newResult.fill(makeDefaultBackgroundColor());

		Painter painter(&newResult);
		painter.drawImage(0, 0, result);

		callback(newResult, final);
	};

	if (!preload->documents.empty() || !preload->photos.empty()) {
		render(false); // render immediately to give box width

		struct PreloadState {
			bool finished = false;
			rpl::lifetime lifetime;
			Fn<void()> render;

			void finish() {
				if (finished || !render) {
					return;
				}
				finished = true;
				lifetime.destroy();
				render();
			}
		};
		const auto state = std::make_shared<PreloadState>();
		state->render = crl::guard(box, [render = std::move(render)]() mutable {
			render(true);
		});
		const auto weakState = std::weak_ptr<PreloadState>(state);
		// The Elements built above live inside render, and Element's destructor
		// dereferences its HistoryItem. crl::guard below only suppresses the call:
		// base::call_delayed keeps the closure itself alive until it fires, so the
		// Elements would outlive the box by up to the timeout. Drop them by hand.
		QObject::connect(box, &QObject::destroyed, [weakState] {
			if (const auto state = weakState.lock()) {
				state->lifetime.destroy();
				state->render = nullptr;
			}
		});
		rpl::single() | rpl::then(
			config.controller->session().downloaderTaskFinished()
		) | rpl::filter([=]
			{
				for (const auto &media : preload->photos) {
					if (media->owner()->loading()) return false;
				}
				for (const auto &media : preload->documents) {
					if (media->owner()->thumbnailLoading()) return false;
				}
				return true;
			}
		) | rpl::take(1) | rpl::on_next([weakState] {
			if (const auto state = weakState.lock()) {
				state->finish();
			}
		}, state->lifetime);
		base::call_delayed(
			3 * crl::time(1000),
			box,
			[state] { state->finish(); });
	} else {
		render(true);
	}
}

namespace {

// 🥀🥀🥀

std::shared_ptr<Ui::ChatStyle> BuildShotChatStyle(
		not_null<Window::SessionController*> controller) {
	const auto &shot = LuxurySettings::getInstance().messageShotSettings();
	const auto hasSavedTheme = shot.embeddedThemeType() != -1
		|| shot.cloudThemeId() != 0;
	const auto persistedPalette = getPersistedPalette();
	if (hasSavedTheme && persistedPalette) {
		return std::make_shared<Ui::ChatStyle>(persistedPalette.get());
	}
	return std::make_shared<Ui::ChatStyle>(controller->chatStyle());
}

template <typename ResolveMessage>
void ShowMessageShotBox(
		ResolveMessage resolveMessage,
		not_null<Window::SessionController*> controller,
		const MessageIdsList &ids,
		Fn<void()> clearSelected) {
	auto messages = std::vector<FullMsgId>();
	messages.reserve(ids.size());
	for (const auto &item : ids) {
		if (resolveMessage(item)) {
			messages.push_back(item);
		}
	}
	if (messages.empty()) {
		return;
	}

	const LuxuryFeatures::MessageShot::ShotConfig config = {
		controller,
		BuildShotChatStyle(controller),
		std::move(messages),
	};
	auto box = Box<MessageShotBox>(config);
	const auto raw = box.data();
	raw->boxClosing() | rpl::on_next([=]
	{
		if (raw->tookShot()) clearSelected();
	}, raw->lifetime());
	Ui::show(std::move(box));
}

template <typename Widget, typename GetIds>
void WrapperImpl(
		not_null<Widget*> widget,
		GetIds getIds,
		Fn<void()> clearSelected) {
	const auto items = getIds(widget);
	if (items.empty()) {
		return;
	}

	const auto session = &widget->session();
	const auto controller = widget->session().tryResolveWindow();
	if (!controller) {
		return;
	}

	ShowMessageShotBox(
		[=](const auto item) { return session->data().message(item); },
		controller,
		items,
		std::move(clearSelected));
}

}

void Wrapper(not_null<HistoryView::ListWidget*> widget, Fn<void()> clearSelected) {
	WrapperImpl(
		widget,
		[](const auto widget) { return widget->getSelectedIds(); },
		std::move(clearSelected));
}

void Wrapper(not_null<HistoryInner*> widget, Fn<void()> clearSelected) {
	WrapperImpl(
		widget,
		[](const auto widget) { return widget->getSelectedItems(); },
		std::move(clearSelected));
}

}
