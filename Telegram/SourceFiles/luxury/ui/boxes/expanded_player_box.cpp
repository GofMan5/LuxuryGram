// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "luxury/ui/boxes/expanded_player_box.h"

#include "base/algorithm.h"
#include "boxes/abstract_box.h"
#include "core/file_utilities.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_origin.h"
#include "data/data_session.h"
#include "lang_auto.h"
#include "luxury/ui/utils/itunes_search.h"
#include "main/main_session.h"
#include "media/audio/media_audio.h"
#include "media/player/media_player_button.h"
#include "media/player/media_player_instance.h"
#include "ui/image/image.h"
#include "ui/painter.h"
#include "ui/style/style_core_scale.h"
#include "ui/text/format_song_document_name.h"
#include "ui/text/format_values.h"
#include "ui/toast/toast.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/continuous_sliders.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "window/themes/window_theme.h"
#include "window/window_session_controller.h"

#include "styles/palette.h"
#include "styles/style_layers.h"
#include "styles/style_luxury_styles.h"
#include "styles/style_media_player.h"

#include <QPainterPath>
#include <QSvgRenderer>

#include <algorithm>

ExpandedPlayerBox::ExpandedPlayerBox(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	AudioMsgId::Type type)
: _controller(controller)
, _type(type) {
}

void ExpandedPlayerBox::prepare() {
	setupContent();
}

class ExpandedPlayerBox::CoverWidget final : public Ui::RpWidget {
public:
	CoverWidget(QWidget *parent, int radius)
	: RpWidget(parent)
	, _radius(radius) {
	}

	void setImage(QImage image) {
		_image = std::move(image);
		_prepared = QImage();
		_placeholder = QImage();
		update();
	}

	[[nodiscard]] const QImage &image() const {
		return _image;
	}

protected:
	int resizeGetHeight(int newWidth) override {
		_prepared = QImage();
		_placeholder = QImage();
		return newWidth;
	}

	void paintEvent(QPaintEvent *e) override {
		auto p = Painter(this);
		auto hq = PainterHighQualityEnabler(p);

		auto clip = QPainterPath();
		clip.addRoundedRect(rect(), _radius, _radius);
		p.setClipPath(clip);
		if (_image.isNull()) {
			ensurePlaceholder();
			if (!_placeholder.isNull()) {
				p.drawImage(QPoint(0, 0), _placeholder);
			} else {
				p.fillRect(rect(), st::boxBg);
			}
		} else {
			ensurePrepared();
			if (!_prepared.isNull()) {
				p.drawImage(QPoint(0, 0), _prepared);
			}
		}
	}

private:
	void ensurePrepared() const {
		if (!_prepared.isNull()) {
			return;
		}
		const auto dpr = style::DevicePixelRatio();
		const auto width = int(base::SafeRound(this->width() * dpr));
		const auto height = int(base::SafeRound(this->height() * dpr));
		if (!width || !height) {
			return;
		}
		auto scaled = _image.scaled(
			width,
			height,
			Qt::KeepAspectRatioByExpanding,
			Qt::SmoothTransformation);
		if (scaled.width() > width) {
			scaled = scaled.copy(
				(scaled.width() - width) / 2,
				0,
				width,
				scaled.height());
		} else if (scaled.height() > height) {
			scaled = scaled.copy(
				0,
				(scaled.height() - height) / 2,
				scaled.width(),
				height);
		}
		scaled.setDevicePixelRatio(dpr);
		_prepared = std::move(scaled);
	}

	void ensurePlaceholder() const {
		if (!_placeholder.isNull()) {
			return;
		}
		const auto dpr = style::DevicePixelRatio();
		const auto width = int(base::SafeRound(this->width() * dpr));
		const auto height = int(base::SafeRound(this->height() * dpr));
		if (!width || !height) {
			return;
		}
		auto image = QImage(width, height, QImage::Format_ARGB32);
		image.fill(Window::Theme::IsNightMode()
			? st::windowBoldFg->c.darker()
			: st::windowBoldFg->c.lighter());
		{
			auto p = Painter(&image);
			auto svg = QSvgRenderer(u":/gui/icons/luxury/nocover.svg"_q);
			svg.render(&p, QRect(0, 0, width, height));
		}
		image.setDevicePixelRatio(dpr);
		_placeholder = std::move(image);
	}

	int _radius = 0;
	QImage _image;
	mutable QImage _prepared;
	mutable QImage _placeholder;

};

class ExpandedPlayerBox::TimeWidget final : public Ui::RpWidget {
public:
	explicit TimeWidget(QWidget *parent)
	: RpWidget(parent)
	, _positionLabel(this, st::mediaPlayerTime)
	, _durationLabel(this, st::mediaPlayerTime) {
		setAttribute(Qt::WA_TransparentForMouseEvents);
		setFixedHeight(st::mediaPlayerTime.font->height);
	}

	void updateTexts(const QString &position, const QString &duration) {
		_positionLabel.setText(position);
		_durationLabel.setText(duration);
		_positionLabel.moveToLeft(0, 0);
		_durationLabel.moveToRight(0, 0);
	}

private:
	Ui::LabelSimple _positionLabel;
	Ui::LabelSimple _durationLabel;

};

class ExpandedPlayerBox::ControlsWidget final : public Ui::RpWidget {
public:
	ControlsWidget(QWidget *parent, AudioMsgId::Type type)
	: RpWidget(parent)
	, _previousTrack(this, st::mediaPlayerPreviousButton)
	, _playPause(this, st::mediaPlayerPlayButton)
	, _nextTrack(this, st::mediaPlayerNextButton) {
		_previousTrack->setClickedCallback([=] {
			Media::Player::instance()->previous(type);
		});
		_previousTrack->setAccessibleName(
			tr::lng_shortcuts_media_previous(tr::now));
		_playPause->setClickedCallback([=] {
			Media::Player::instance()->playPauseCancelClicked(type);
		});
		_nextTrack->setClickedCallback([=] {
			Media::Player::instance()->next(type);
		});
		_nextTrack->setAccessibleName(
			tr::lng_shortcuts_media_next(tr::now));
	}

	[[nodiscard]] Media::Player::PlayButton *playPause() const {
		return _playPause;
	}

	[[nodiscard]] Ui::IconButton *previousTrack() const {
		return _previousTrack;
	}

	[[nodiscard]] Ui::IconButton *nextTrack() const {
		return _nextTrack;
	}

protected:
	int resizeGetHeight(int newWidth) override {
		const auto skip = st::luxuryPlayerControlsSkip;
		const auto total = _previousTrack->width()
			+ skip
			+ _playPause->width()
			+ skip
			+ _nextTrack->width();
		const auto left = (newWidth - total) / 2;
		const auto height = std::max({
			_previousTrack->height(),
			_playPause->height(),
			_nextTrack->height() });
		_previousTrack->moveToLeft(
			left,
			(height - _previousTrack->height()) / 2);
		_playPause->moveToLeft(
			left + _previousTrack->width() + skip,
			(height - _playPause->height()) / 2);
		_nextTrack->moveToLeft(
			left + _previousTrack->width() + skip + _playPause->width() + skip,
			(height - _nextTrack->height()) / 2);
		return height;
	}

private:
	Ui::IconButton *_previousTrack = nullptr;
	Media::Player::PlayButton *_playPause = nullptr;
	Ui::IconButton *_nextTrack = nullptr;

};

void ExpandedPlayerBox::setupContent() {
	setTitle(tr::luxury_PlayerTitle());

	auto wrap = object_ptr<Ui::VerticalLayout>(this);
	const auto content = wrap.data();
	setInnerWidget(object_ptr<Ui::OverrideMargins>(this, std::move(wrap)));

	_cover = content->add(
		object_ptr<CoverWidget>(content, st::luxuryPlayerCoverRadius),
		st::luxuryPlayerCoverPadding);
	_titleLabel = content->add(
		object_ptr<Ui::FlatLabel>(content, st::luxuryPlayerTitle),
		st::luxuryPlayerTitlePadding);
	_performerWrap = content->add(
		object_ptr<Ui::SlideWrap<Ui::FlatLabel>>(
			content,
			object_ptr<Ui::FlatLabel>(content, st::luxuryPlayerPerformer)),
		st::luxuryPlayerPerformerPadding);
	_playbackSlider = content->add(
		object_ptr<Ui::FilledSlider>(content, st::mediaPlayerPlayback),
		st::luxuryPlayerSliderPadding);
	_playbackSlider->setFixedHeight(st::mediaPlayerPlayback.fullWidth);
	_timeRow = content->add(
		object_ptr<TimeWidget>(content),
		st::luxuryPlayerTimePadding);
	_controls = content->add(
		object_ptr<ControlsWidget>(content, _type),
		st::luxuryPlayerControlsPadding);

	_playbackProgress.setInLoadingStateChangedCallback([=](bool loading) {
		_playbackSlider->setDisabled(loading);
	});
	_playbackProgress.setValueChangedCallback([=](float64 value, float64) {
		_playbackSlider->setValue(value);
	});
	_playbackSlider->setChangeProgressCallback([=](float64 value) {
		_playbackProgress.setValue(value, false);
		handleSeekProgress(value);
	});
	_playbackSlider->setChangeFinishedCallback([=](float64 value) {
		_playbackProgress.setValue(value, false);
		handleSeekFinished(value);
	});

	_saveCoverButton = addButton(
		tr::luxury_PlayerSaveCover(),
		[=] { saveCover(); });
	_saveCoverButton->setDisabled(true);

	Media::Player::instance()->updatedNotifier(
	) | rpl::on_next([=](const Media::Player::TrackState &state) {
		handleSongUpdate(state);
	}, lifetime());

	Media::Player::instance()->trackChanged(
	) | rpl::filter([=](AudioMsgId::Type type) {
		return (type == _type);
	}) | rpl::on_next([=](AudioMsgId::Type type) {
		refreshTrack();
	}, lifetime());

	Media::Player::instance()->playlistChanges(
		_type
	) | rpl::on_next([=] {
		handlePlaylistUpdate();
	}, lifetime());

	refreshTrack();
	handleSongUpdate(Media::Player::instance()->getState(_type));
	_controls->playPause()->finishTransform();
	handlePlaylistUpdate();
	updateTimeLabels();

	setDimensionsToContent(st::luxuryPlayerBoxWidth, content);
}

void ExpandedPlayerBox::refreshTrack() {
	const auto current = Media::Player::instance()->current(_type);
	const auto document = current.audio();
	if (!current
		|| !document
		|| ((_lastSongId.audio() == document)
			&& (_lastSongId.contextId() == current.contextId()))) {
		return;
	}
	_lastSongId = current;

	if (document->isVoiceMessage() || document->isVideoMessage()) {
		_titleLabel->setMarkedText(
			Ui::Text::FormatVoiceName(
				document,
				current.contextId()).textWithEntities(true));
		_performerWrap->entity()->setText(QString());
		_performerWrap->toggle(false, anim::type::instant);
	} else {
		const auto song = document->song();
		const auto title = (song && !song->title.isEmpty())
			? song->title
			: document->filename();
		const auto performer = song ? song->performer : QString();
		_titleLabel->setText(title);
		_performerWrap->entity()->setText(performer);
		_performerWrap->toggle(!performer.isEmpty(), anim::type::instant);
	}

	refreshCover(document, current);
	handlePlaylistUpdate();
}

void ExpandedPlayerBox::refreshCover(
		not_null<DocumentData*> document,
		const AudioMsgId &current) {
	const auto generation = ++_coverGeneration;
	_coverDownloadLifetime.destroy();
	_mediaView = document->createMediaView();
	_mediaView->thumbnailWanted(Data::FileOrigin(current.contextId()));

	if (document->isSongWithCover()) {
		if (const auto thumbnail = _mediaView->thumbnail()) {
			setCover(thumbnail->original());
			return;
		}
		rpl::single(
		) | rpl::then(
			document->owner().session().downloaderTaskFinished()
		) | rpl::filter([=] {
			return (_mediaView->thumbnail() != nullptr);
		}) | rpl::take(1) | rpl::on_next([=] {
			if (const auto thumbnail = _mediaView->thumbnail()) {
				setCover(thumbnail->original());
			}
		}, _coverDownloadLifetime);
		return;
	}

	const auto song = document->song();
	if (!song) {
		return;
	}
	const auto performer = song->performer;
	const auto title = song->title;
	if (performer.isEmpty() && title.isEmpty()) {
		return;
	}
	const auto sizeHint = int(st::luxuryPlayerCoverFetchSize);
	crl::async([=] {
		auto image = Luxury::Ui::Itunes::FetchCover(
			performer,
			title,
			sizeHint);
		crl::on_main(this, [=, image = std::move(image)]() mutable {
			if (_coverGeneration != generation
				|| image.isNull()
				|| !_cover->image().isNull()) {
				return;
			}
			setCover(std::move(image));
		});
	});
}

void ExpandedPlayerBox::setCover(QImage image) {
	_cover->setImage(std::move(image));
	_saveCoverButton->setDisabled(_cover->image().isNull());
}

void ExpandedPlayerBox::saveCover() {
	const auto image = _cover->image();
	if (image.isNull()) {
		showToast(tr::luxury_PlayerSaveCoverFailed(tr::now));
		return;
	}
	auto filter = u"PNG Image (*.png);;"_q
		+ FileDialog::AllFilesFilter();
	FileDialog::GetWritePath(
		this,
		tr::lng_save_file(tr::now),
		filter,
		filedialogDefaultName(u"cover"_q, u".png"_q),
		crl::guard(this, [=](const QString &result) {
			if (result.isEmpty()) {
				return;
			}
			const auto path = result.endsWith(u".png"_q, Qt::CaseInsensitive)
				? result
				: (result + u".png"_q);
			closeBox();
			crl::async([=] {
				if (!image.save(path)) {
					crl::on_main([] {
						Ui::Toast::Show(
							tr::luxury_PlayerSaveCoverFailed(tr::now));
					});
				}
			});
		}));
}

void ExpandedPlayerBox::handleSongUpdate(
		const Media::Player::TrackState &state) {
	if (state.id.type() != _type || !state.id.audio()) {
		return;
	}

	if (state.id.audio()->loading()) {
		_playbackProgress.updateLoadingState(
			state.id.audio()->progress());
	} else {
		_playbackProgress.updateState(state);
	}

	auto showPause = Media::Player::ShowPauseIcon(state.state);
	if (Media::Player::instance()->isSeeking(_type)) {
		showPause = true;
	}
	_controls->playPause()->setState(
		state.id.audio()->loading()
			? Media::Player::PlayButton::State::Cancel
			: showPause
			? Media::Player::PlayButton::State::Pause
			: Media::Player::PlayButton::State::Play);
	_controls->playPause()->setAccessibleName(
		showPause
			? tr::lng_shortcuts_media_pause(tr::now)
			: tr::lng_shortcuts_media_play(tr::now));

	updateTimeText(state);
}

void ExpandedPlayerBox::updateTimeText(
		const Media::Player::TrackState &state) {
	qint64 display = 0;
	const auto frequency = state.frequency;
	const auto document = state.id.audio();
	if (!Media::Player::IsStoppedOrStopping(state.state)) {
		display = state.position;
	} else if (state.length) {
		display = state.length;
	} else if (document->song()) {
		display = (document->duration() * frequency) / 1000;
	}

	_lastDurationMs = (state.length * 1000LL) / frequency;

	if (document->loading()) {
		_time = QString::number(
			int(base::SafeRound(document->progress() * 100))) + '%';
		_playbackSlider->setDisabled(true);
	} else {
		display = display / frequency;
		_time = Ui::FormatDurationText(display);
		_playbackSlider->setDisabled(false);
	}
	if (_seekPositionMs < 0) {
		updateTimeLabels();
	}
}

void ExpandedPlayerBox::updateTimeLabels() {
	const auto position = (_seekPositionMs >= 0)
		? Ui::FormatDurationText(_seekPositionMs / 1000LL)
		: _time;
	const auto duration = _lastDurationMs
		? Ui::FormatDurationText(_lastDurationMs / 1000LL)
		: QString();
	_timeRow->updateTexts(position, duration);
}

void ExpandedPlayerBox::handleSeekProgress(float64 progress) {
	if (!_lastDurationMs) {
		return;
	}
	const auto positionMs = std::clamp(
		static_cast<crl::time>(progress * _lastDurationMs),
		crl::time(0),
		_lastDurationMs);
	if (_seekPositionMs != positionMs) {
		_seekPositionMs = positionMs;
		updateTimeLabels();
		Media::Player::instance()->startSeeking(_type);
	}
}

void ExpandedPlayerBox::handleSeekFinished(float64 progress) {
	if (!_lastDurationMs) {
		return;
	}
	_seekPositionMs = -1;
	Media::Player::instance()->finishSeeking(_type, progress);
}

void ExpandedPlayerBox::handlePlaylistUpdate() {
	const auto previousEnabled = Media::Player::instance()->previousAvailable(
		_type);
	const auto nextEnabled = Media::Player::instance()->nextAvailable(
		_type);
	_controls->previousTrack()->setIconOverride(
		previousEnabled ? nullptr : &st::mediaPlayerPreviousDisabledIcon);
	_controls->previousTrack()->setRippleColorOverride(
		previousEnabled ? nullptr : &st::mediaPlayerBg);
	_controls->previousTrack()->setPointerCursor(previousEnabled);
	_controls->nextTrack()->setIconOverride(
		nextEnabled ? nullptr : &st::mediaPlayerNextDisabledIcon);
	_controls->nextTrack()->setRippleColorOverride(
		nextEnabled ? nullptr : &st::mediaPlayerBg);
	_controls->nextTrack()->setPointerCursor(nextEnabled);
}
