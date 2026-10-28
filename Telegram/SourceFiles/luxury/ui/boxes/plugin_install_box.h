// This is the source code of LuxuryGram for Desktop.
// See LEGAL for license and copyright information.
#pragma once

#include <memory>

namespace Ui {
class GenericBox;
class Show;
} // namespace Ui

namespace Luxury::Plugins {

// Asks for a GitHub link, resolves it and passes the plan to the consent box.
void FillPluginInstallBox(
	not_null<Ui::GenericBox*> box,
	std::shared_ptr<Ui::Show> show);

} // namespace Luxury::Plugins
