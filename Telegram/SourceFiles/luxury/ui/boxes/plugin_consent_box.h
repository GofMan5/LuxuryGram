// This is the source code of LuxuryGram for Desktop.
// See LEGAL for license and copyright information.
#pragma once

#include "rpl/producer.h"

#include <QtCore/QString>

#include <memory>
#include <vector>

namespace Ui {
class Show;
} // namespace Ui

namespace Luxury::Plugins {

struct InstallPlan;
enum class ErrorCode;

// The localized text of a lifecycle error, empty for None.
[[nodiscard]] QString ErrorText(ErrorCode code);
// PluginView::lastError with the client's own reasons localized; what the
// plugin threw is shown as is.
[[nodiscard]] QString LastErrorText(const QString &error);

// Installs and updates the consent box committed, while they download.
struct PendingInstall {
	QString id;
	QString name;
	bool cancelled = false;
};
[[nodiscard]] const std::vector<PendingInstall> &PendingInstalls();
[[nodiscard]] rpl::producer<> PendingInstallsChanges();
void CancelInstall(const QString &id);

// Shows the permission consent box for the plan, or commits an update that
// asks for nothing new right away. The only UI path to commitInstall.
void RequestInstall(std::shared_ptr<Ui::Show> show, InstallPlan plan);

} // namespace Luxury::Plugins
