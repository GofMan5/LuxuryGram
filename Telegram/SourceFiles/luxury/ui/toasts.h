#pragma once

#include "ui/toast/toast.h"

namespace Luxury::Ui {

void ShowToastWithAction(
	::Ui::Toast::Config &&config,
	const QString &buttonText,
	Fn<void()> callback);

// Shown when a write to the LuxuryGram database failed, so the user does not
// walk away thinking a filter or an exclusion was saved.
void ShowDatabaseError();

}
