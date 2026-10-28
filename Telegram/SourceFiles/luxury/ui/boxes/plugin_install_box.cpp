// This is the source code of LuxuryGram for Desktop.
// See LEGAL for license and copyright information.
#include "luxury/ui/boxes/plugin_install_box.h"

#include "lang_auto.h"
#include "luxury/plugins/plugin_manager.h"
#include "luxury/ui/boxes/plugin_consent_box.h"
#include "ui/layers/generic_box.h"
#include "ui/layers/show.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/slide_wrap.h"

#include "styles/style_boxes.h"
#include "styles/style_layers.h"

namespace Luxury::Plugins {

void FillPluginInstallBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Ui::Show> show) {
	box->setWidth(st::boxWideWidth);
	box->setTitle(tr::lng_luxury_plugins_ui_install_title());

	box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		tr::lng_luxury_plugins_ui_install_about(),
		st::boxDividerLabel));
	const auto field = box->addRow(object_ptr<Ui::InputField>(
		box,
		st::defaultInputField,
		tr::lng_luxury_plugins_ui_install_placeholder()));
	const auto error = box->addRow(
		object_ptr<Ui::SlideWrap<Ui::FlatLabel>>(
			box,
			object_ptr<Ui::FlatLabel>(
				box,
				QString(),
				st::boxAttentionDividerLabel)));
	error->hide(anim::type::instant);
	box->setFocusCallback([=] { field->setFocusFast(); });

	const auto loading = std::make_shared<bool>(false);
	const auto submit = [=] {
		const auto link = field->getLastText().trimmed();
		const auto manager = Manager::Maybe();
		if (*loading) {
			return;
		} else if (link.isEmpty()) {
			field->showError();
			return;
		} else if (!manager) {
			error->entity()->setText(
				tr::lng_luxury_plugins_ui_unavailable(tr::now));
			error->show(anim::type::normal);
			return;
		}
		*loading = true;
		error->hide(anim::type::normal);
		box->showLoading(true);
		manager->prepareInstall(link, crl::guard(box, [=](
				InstallPlan plan,
				QString text) {
			*loading = false;
			box->showLoading(false);
			if (!text.isEmpty()) {
				error->entity()->setText(ErrorText(plan.errorCode, text));
				error->show(anim::type::normal);
				field->showError();
				return;
			}
			box->closeBox();
			RequestInstall(show, std::move(plan));
		}));
	};
	field->submits() | rpl::on_next(submit, field->lifetime());
	box->addButton(tr::lng_luxury_plugins_ui_install(), submit);
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

} // namespace Luxury::Plugins
