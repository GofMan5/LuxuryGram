// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

#include "luxury/features/message_shot/message_shot.h"
#include "ui/layers/box_content.h"

#include <QImage>

namespace Ui {
class RoundButton;
} // namespace Ui

class MessageShotBox : public Ui::BoxContent
{
public:
	MessageShotBox(QWidget *parent, LuxuryFeatures::MessageShot::ShotConfig config);

	bool tookShot() const {
		return _tookShot;
	}

protected:
	void prepare() override;

private:
	void setupContent();
	void setExportButtonsEnabled(bool enabled);

	LuxuryFeatures::MessageShot::ShotConfig _config;
	std::shared_ptr<style::palette> _selectedPalette;
	rpl::lifetime _savedThemeLifetime;
	QImage _exportImage;
	QPointer<Ui::RoundButton> _saveButton;
	QPointer<Ui::RoundButton> _copyButton;

	bool _tookShot = false;
};
