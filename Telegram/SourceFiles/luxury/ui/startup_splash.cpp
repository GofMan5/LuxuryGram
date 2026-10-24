// This is the source code of LuxuryGram for Desktop.
// See LEGAL for license and copyright information.
#include "luxury/ui/startup_splash.h"

#include "boxes/about_box.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "core/version.h"
#include "lang/lang_keys.h"
#include "luxury/ui/luxury_logo.h"
#include "settings.h"
#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "ui/widgets/shadow.h"
#include "styles/style_luxury_styles.h"

#include <QtGui/QGuiApplication>
#include <QtGui/QScreen>
#include <qpa/qwindowsysteminterface.h>

namespace LuxuryStartup {
namespace {

// MainWindow::applyInitialWorkMode() sends such a start straight to the tray
// or the taskbar, so announcing it on screen would be wrong. Its passcode
// exception is unknown here, the accounts are not loaded yet; a passcode
// autostart only loses the splash, never shows one over a hidden window.
[[nodiscard]] bool StartsHidden() {
	return cStartInTray()
		|| (cLaunchMode() == LaunchModeAutoStart && cStartMinimized());
}

[[nodiscard]] QScreen *TargetScreen() {
	const auto saved = Core::App().settings().windowPosition().rect();
	const auto screen = QGuiApplication::screenAt(saved.center());
	return screen ? screen : QGuiApplication::primaryScreen();
}

} // namespace

Splash::Splash()
: QWidget(
	nullptr,
	Qt::SplashScreen
		| Qt::FramelessWindowHint
		| Qt::WindowStaysOnTopHint
		| Qt::NoDropShadowWindowHint)
, _version(tr::lng_about_version(
	tr::now,
	lt_version,
	currentVersionShortText())) {
	setAttribute(Qt::WA_TranslucentBackground);
	setAttribute(Qt::WA_ShowWithoutActivating);
	setAttribute(Qt::WA_QuitOnClose, false);

	const auto ratio = style::DevicePixelRatio();
	const auto logo = st::luxuryStartupLogoSize;
	auto image = LuxuryAssets::currentAppLogo().scaled(
		QSize(logo, logo) * ratio,
		Qt::IgnoreAspectRatio,
		Qt::SmoothTransformation);
	image.setDevicePixelRatio(ratio);
	_logo = Ui::PixmapFromImage(std::move(image));

	const auto &padding = st::luxuryStartupPadding;
	const auto cardHeight = padding.top()
		+ logo
		+ st::luxuryStartupTitleSkip
		+ st::luxuryStartupTitleFont->height
		+ st::luxuryStartupVersionSkip
		+ st::luxuryStartupVersionFont->height
		+ st::luxuryStartupStageSkip
		+ st::luxuryStartupStageFont->height
		+ st::luxuryStartupBarSkip
		+ st::luxuryStartupBarHeight
		+ padding.bottom();
	resize(QRect(0, 0, st::luxuryStartupWidth, cardHeight).marginsAdded(
		st::luxuryStartupShadow.extend).size());
	if (const auto screen = TargetScreen()) {
		const auto available = screen->availableGeometry();
		move(
			available.x() + (available.width() - width()) / 2,
			available.y() + (available.height() - height()) / 2);
	}
}

void Splash::setStage(const QString &text, float64 progress) {
	_stage = text;
	_progress = std::clamp(progress, 0., 1.);

	// The main thread does not reach the event loop before the main window
	// is shown, so the expose that maps this window is delivered by hand.
	QWindowSystemInterface::flushWindowSystemEvents(
		QEventLoop::ExcludeUserInputEvents);
	repaint();
}

void Splash::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	const auto card = rect().marginsRemoved(st::luxuryStartupShadow.extend);
	Ui::Shadow::paint(p, card, width(), st::luxuryStartupShadow);

	auto hq = PainterHighQualityEnabler(p);
	p.setPen(Qt::NoPen);
	p.setBrush(st::windowBg);
	p.drawRoundedRect(
		card,
		st::luxuryStartupRadius,
		st::luxuryStartupRadius);

	const auto &padding = st::luxuryStartupPadding;
	const auto left = card.x() + padding.left();
	const auto available = card.width() - padding.left() - padding.right();
	auto top = card.y() + padding.top();
	p.drawPixmap(
		card.x() + (card.width() - st::luxuryStartupLogoSize) / 2,
		top,
		_logo);
	top += st::luxuryStartupLogoSize + st::luxuryStartupTitleSkip;

	const auto line = [&](
			const style::font &font,
			const style::color &color,
			const QString &text) {
		p.setFont(font);
		p.setPen(color);
		p.drawText(
			QRect(left, top, available, font->height),
			style::al_top,
			font->elided(text, available));
		top += font->height;
	};
	line(st::luxuryStartupTitleFont, st::windowBoldFg, AppName.utf16());
	top += st::luxuryStartupVersionSkip;
	line(st::luxuryStartupVersionFont, st::windowSubTextFg, _version);
	top += st::luxuryStartupStageSkip;
	line(st::luxuryStartupStageFont, st::windowFg, _stage);
	top += st::luxuryStartupBarSkip;

	const auto bar = QRect(left, top, available, st::luxuryStartupBarHeight);
	const auto radius = bar.height() / 2.;
	p.setPen(Qt::NoPen);
	p.setBrush(st::sliderBgInactive);
	p.drawRoundedRect(bar, radius, radius);
	p.setBrush(st::sliderBgActive);
	p.drawRoundedRect(
		QRect(
			bar.x(),
			bar.y(),
			std::max(bar.height(), int(bar.width() * _progress)),
			bar.height()),
		radius,
		radius);
}

std::unique_ptr<Splash> ShowSplash() {
	if (StartsHidden()) {
		return nullptr;
	}
	auto result = std::make_unique<Splash>();
	result->show();
	DEBUG_LOG(("Application Info: startup splash shown."));
	return result;
}

} // namespace LuxuryStartup
