// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

#include "luxury/features/message_shot/message_shot_theme_state.h"
#include "ui/layers/box_content.h"
#include "ui/style/style_core_palette.h"
#include "window/window_session_controller.h"

namespace Ui {
class RoundButton;
} // namespace Ui

struct ShotThemeSelection {
	QByteArray palette;
	Window::Theme::EmbeddedType embedded = Window::Theme::EmbeddedType(-1);
	std::optional<QColor> color;
	std::optional<Data::CloudTheme> cloud;
	QString name;
};

class ThemeSelectorBox : public Ui::BoxContent
{
public:
	ThemeSelectorBox(QWidget *parent, not_null<Window::SessionController*> controller);

	rpl::producer<ShotThemeSelection> themeSelected();

protected:
	void prepare() override;

private:
	void setupContent();
	void setApplyEnabled(bool enabled);

	not_null<Window::SessionController*> _controller;

	rpl::event_stream<ShotThemeSelection> _themes;
	std::optional<ShotThemeSelection> _selection;
	QPointer<Ui::RoundButton> _applyButton;
	rpl::lifetime _selectionLifetime;
	int _selectionGeneration = 0;
};
