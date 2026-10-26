// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

#include "boxes/abstract_box.h"

namespace Ui {

class MultiSelect;
struct ScrollToRequest;

} // namespace Ui

namespace Window {

class SessionController;

} // namespace Window

namespace LuxuryUi {

class FontSelectorBox : public Ui::BoxContent
{
public:
	FontSelectorBox(QWidget *, Window::SessionController *controller, Fn<void(QString font)> hook);

	void setInnerFocus() override;
	static void Show(Window::SessionController *controller, const Fn<void(QString font)> hook);

private:
	QString _selectedFont;

protected:
	void prepare() override;
	void keyPressEvent(QKeyEvent *e) override;

private:
	[[nodiscard]] int rowsInPage() const;

	Window::SessionController *_controller = nullptr;
	Fn<void()> _setInnerFocus;
	Fn<Ui::ScrollToRequest(int rows)> _jump;
	Fn<void(QString font)> _hook;

};

}
