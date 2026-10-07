// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

#include "base/basic_types.h"
#include "data/data_audio_msg_id.h"
#include "media/view/media_view_playback_progress.h"
#include "rpl/lifetime.h"
#include "ui/layers/box_content.h"

#include <crl/crl_time.h>

class DocumentData;
class QImage;

namespace Data {
class DocumentMedia;
} // namespace Data

namespace Media {
namespace Player {
struct TrackState;
} // namespace Player
} // namespace Media

namespace Ui {
class FilledSlider;
class FlatLabel;
class RoundButton;
template <typename Widget>
class SlideWrap;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

class ExpandedPlayerBox final : public Ui::BoxContent {
public:
	ExpandedPlayerBox(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		AudioMsgId::Type type);

protected:
	void prepare() override;

private:
	class CoverWidget;
	class TimeWidget;
	class ControlsWidget;

	void setupContent();
	void refreshTrack();
	void refreshCover(
		not_null<DocumentData*> document,
		const AudioMsgId &current);
	void setCover(QImage image);
	void saveCover();
	void handleSongUpdate(const Media::Player::TrackState &state);
	void handlePlaylistUpdate();
	void handleSeekProgress(float64 progress);
	void handleSeekFinished(float64 progress);
	void updateTimeText(const Media::Player::TrackState &state);
	void updateTimeLabels();

	const not_null<Window::SessionController*> _controller;
	AudioMsgId::Type _type = AudioMsgId::Type::Unknown;

	CoverWidget *_cover = nullptr;
	Ui::FlatLabel *_titleLabel = nullptr;
	Ui::SlideWrap<Ui::FlatLabel> *_performerWrap = nullptr;
	Ui::FilledSlider *_playbackSlider = nullptr;
	TimeWidget *_timeRow = nullptr;
	ControlsWidget *_controls = nullptr;
	QPointer<Ui::RoundButton> _saveCoverButton = nullptr;

	Media::View::PlaybackProgress _playbackProgress;
	std::shared_ptr<Data::DocumentMedia> _mediaView;
	rpl::lifetime _coverDownloadLifetime;
	AudioMsgId _lastSongId;
	QString _time;
	crl::time _seekPositionMs = -1;
	crl::time _lastDurationMs = 0;
	int _coverGeneration = 0;

};
