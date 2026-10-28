// This is the source code of LuxuryGram for Desktop.
// See LEGAL for license and copyright information.
#pragma once

#include <memory>

namespace Ui {
class Show;
} // namespace Ui

namespace Luxury::Plugins {

struct InstallPlan;

// Shows the permission consent box for the plan, or commits an update that
// asks for nothing new right away. The only UI path to commitInstall.
void RequestInstall(std::shared_ptr<Ui::Show> show, InstallPlan plan);

} // namespace Luxury::Plugins
