// This is the source code of LuxuryGram for Desktop.
// See LEGAL for license and copyright information.
#pragma once

#include <memory>

namespace Ui {
class GenericBox;
class Show;
} // namespace Ui

namespace Luxury::Plugins {

// Description, permissions, settings, logs and actions of one plugin.
void FillPluginDetailsBox(
	not_null<Ui::GenericBox*> box,
	std::shared_ptr<Ui::Show> show,
	QString id);

} // namespace Luxury::Plugins
