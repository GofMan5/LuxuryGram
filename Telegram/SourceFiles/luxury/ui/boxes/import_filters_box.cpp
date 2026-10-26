// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "luxury/ui/boxes/import_filters_box.h"

#include "lang_auto.h"
#include "luxury/features/filters/filters_utils.h"
#include "ui/layers/generic_box.h"
#include "ui/toast/toast.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/rect.h"
#include "ui/rp_widget.h"

#include "styles/style_giveaway.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"

#include <QtGui/QClipboard>
#include <QtGui/QGuiApplication>

namespace Ui {

void FillImportFiltersBox(not_null<Ui::GenericBox*> box, bool import) {
	box->setStyle(st::giveawayGiftCodeBox);
	box->setNoContentMargin(true);
	box->setTitle(import ? tr::luxury_FiltersMenuImport() : tr::luxury_FiltersMenuExport());
	box->verticalLayout()->resizeToWidth(box->width());

	const auto container = box->verticalLayout();

	const auto skip = st::settingsSendTypeSkip;
	auto wrap = object_ptr<Ui::VerticalLayout>(container);
	const auto inner = wrap.data();
	container->add(
		object_ptr<Ui::OverrideMargins>(
			container,
			std::move(wrap),
			QMargins(0, skip, 0, skip)));

	Ui::InputField *importURLField = nullptr;
	Ui::SlideWrap<Ui::VerticalLayout> *importURLWrap = nullptr;
	const auto clipboardText = QGuiApplication::clipboard()->text().trimmed();
	const auto clipboardHasUrl = import && clipboardText.startsWith("http");

	const auto intoURL = std::make_shared<RadioenumGroup<bool>>(clipboardHasUrl);
	const auto addOption = [&](bool value, const QString &text)
	{
		inner->add(
			object_ptr<Ui::Radioenum<bool>>(
				inner,
				intoURL,
				value,
				text,
				st::settingsSendType),
			st::settingsSendTypePadding);

		if (import && value) {
			importURLWrap = inner->add(
				object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
					inner,
					object_ptr<Ui::VerticalLayout>(inner),
					st::giveawayGiftCodeBox.buttonPadding
				)
			);
			importURLField = importURLWrap->entity()->add(
				object_ptr<Ui::InputField>(
					container,
					st::defaultInputField,
					tr::lng_formatting_link_url(),
					clipboardHasUrl ? clipboardText : QString()
				)
			);
			if (!clipboardHasUrl) {
				importURLWrap->hide(anim::type::instant);
			}
		}
	};
	addOption(false, import ? tr::luxury_FiltersImportClipboard(tr::now) : tr::luxury_FiltersExportClipboard(tr::now));
	addOption(true, import ? tr::luxury_FiltersImportURL(tr::now) : tr::luxury_FiltersExportURL(tr::now));

	intoURL->setChangedCallback([=](bool value)
	{
		if (import) {
			importURLWrap->toggle(value, anim::type::normal);
			if (value) {
				importURLField->setFocusFast();
			}
		}
	});

	const auto submit = [=]
	{
		const auto isURL = intoURL->current();

		if (import) {
			if (isURL) {
				const auto link = importURLField->getLastText().trimmed();
				if (link.isEmpty()) {
					importURLField->showError();
					return;
				}
				FilterUtils::getInstance().importFromLink(link);
			} else {
				FilterUtils::getInstance().importFromJson(
					QGuiApplication::clipboard()->text().toUtf8());
			}
		} else {
			if (isURL) {
				FilterUtils::getInstance().publishFilters();
			} else {
				const auto data = FilterUtils::getInstance().exportFilters();
				QGuiApplication::clipboard()->setText(data);

				Toast::Show(tr::lng_text_copied(tr::now));
			}
		}
		box->closeBox();
	};
	const auto actionButton = box->addButton(
		import ? tr::luxury_FiltersMenuImport() : tr::luxury_FiltersMenuExport(),
		submit);
	if (importURLField) {
		importURLField->submits() | rpl::on_next([=] {
			submit();
		}, importURLField->lifetime());
	}
	box->setFocusCallback([=] {
		if (import && intoURL->current()) {
			importURLField->setFocusFast();
		} else {
			box->setFocus();
		}
	});
	const auto buttonWidth = box->width()
		- rect::m::sum::h(st::giveawayGiftCodeBox.buttonPadding);
	actionButton->widthValue() | rpl::filter([=]
	{
		return (actionButton->widthNoMargins() != buttonWidth);
	}) | rpl::on_next([=]
							  {
								  actionButton->resizeToWidth(buttonWidth);
							  },
							  actionButton->lifetime());

	box->addTopButton(st::boxTitleClose, [=] { box->closeBox(); });
}

} // namespace Ui
