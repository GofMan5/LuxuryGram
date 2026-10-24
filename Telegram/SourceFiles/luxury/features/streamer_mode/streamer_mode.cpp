// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026

#include "luxury/features/streamer_mode/streamer_mode.h"

#include "luxury/features/streamer_mode/platform/platform_streamer_mode.h"
#include "base/unique_qptr.h"
#include "core/application.h"
#include "window/window_controller.h"

#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QWidget>

namespace LuxuryFeatures::StreamerMode {

namespace {

constexpr auto kHiddenProperty = "LuxuryStreamerModeHidden";
constexpr auto kProtectedProperty = "LuxuryScreenshotProtected";

[[nodiscard]] bool IsWindowCaptureExcluded(not_null<QWidget*> widget) {
	return widget->property(kHiddenProperty).toBool();
}

void SetWindowCaptureExcluded(QWidget *widget, bool excluded) {
	const auto window = widget->window();
	window->setProperty(kHiddenProperty, excluded);
	if (excluded || !window->property(kProtectedProperty).toBool()) {
		Platform::SetWindowCaptureExcluded(window, excluded);
	}
}

// The per-window calls cover the main window, media viewer, PiP,
// notifications, IV and popup menus only. Every other window -- bot web
// apps, payments, calls, tooltips -- is caught here when it appears, the
// same way Core::ScreenshotProtection catches them.
class WindowFilter final : public QObject {
public:
	using QObject::QObject;

protected:
	bool eventFilter(QObject *watched, QEvent *event) override;

};

bool WindowFilter::eventFilter(QObject *watched, QEvent *event) {
	const auto type = event->type();
	if (type == QEvent::Show || type == QEvent::WinIdChange) {
		const auto widget = qobject_cast<QWidget*>(watched);
		if (widget && widget->isWindow() && widget->internalWinId()) {
			SetWindowCaptureExcluded(widget, true);
		}
	}
	return QObject::eventFilter(watched, event);
}

base::unique_qptr<WindowFilter> Filter;

} // namespace

void apply(bool enabled) {
	if (!enabled) {
		Filter = nullptr;
	} else if (!Filter) {
		Filter = base::make_unique_q<WindowFilter>(qApp);
		qApp->installEventFilter(Filter.get());
	}
	Core::App().enumerateWindows([=](not_null<Window::Controller*> window) {
		SetWindowCaptureExcluded(window->widget(), enabled);
	});
	for (const auto widget : QApplication::topLevelWidgets()) {
		if (!widget->windowHandle()) {
			continue;
		}
		if (enabled) {
			if (widget->isVisible()
				&& !IsWindowCaptureExcluded(widget)) {
				SetWindowCaptureExcluded(widget, true);
			}
		} else if (IsWindowCaptureExcluded(widget)) {
			SetWindowCaptureExcluded(widget, false);
		}
	}
}

void hideWidgetWindow(QWidget *widget) {
	SetWindowCaptureExcluded(widget, true);
}

void showWidgetWindow(QWidget *widget) {
	SetWindowCaptureExcluded(widget, false);
}

bool excludedWithProtection(not_null<QWidget*> window, bool protection) {
	window->setProperty(kProtectedProperty, protection);
	return protection || IsWindowCaptureExcluded(window);
}

} // namespace LuxuryFeatures::StreamerMode
