#include "luxury/ui/utils/box_buttons.h"

#include "ui/effects/animation_value.h"
#include "ui/widgets/buttons.h"

namespace Luxury::Ui {

void SetBoxButtonEnabled(not_null<::Ui::RoundButton*> button, bool enabled) {
	if (!enabled) {
		button->clearState();
	}
	button->setDisabled(!enabled);
	button->setAttribute(Qt::WA_TransparentForMouseEvents, !enabled);
	button->setTextFgOverride(enabled
		? std::optional<QColor>()
		: anim::with_alpha(button->st().textFg->c, 0.5));
}

} // namespace Luxury::Ui
