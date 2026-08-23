// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "luxury/ui/settings/filters/edit_filter.h"

#include "lang_auto.h"
#include "luxury/luxury_settings.h"
#include "luxury/data/luxury_database.h"
#include "luxury/features/filters/filters_cache_controller.h"
#include "luxury/ui/toasts.h"
#include "base/event_filter.h"
#include "base/platform/base_platform_info.h"
#include "boxes/delete_messages_box.h"
#include "core/mime_type.h"
#include "lang/lang_text_entity.h"
#include "media/audio/media_audio.h"
#include "media/view/media_view_pip.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"
#include "styles/style_window.h"
#include "ui/ui_utility.h"
#include "ui/boxes/confirm_box.h"
#include "ui/text/text.h"
#include "ui/toast/toast.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"

#include <QtCore/QUuid>

#include <memory>

namespace Settings {
namespace {

bool validateRegex(const icu::UnicodeString& pattern, std::string& errorMsg) {
	UErrorCode status = U_ZERO_ERROR;
	UParseError parseError{};

	const auto regexPattern = std::unique_ptr<icu::RegexPattern>(
		icu::RegexPattern::compile(
			pattern,
			0,
			parseError,
			status));

	if (U_FAILURE(status)) {
		auto errorCodeNormalized = std::string(u_errorName(status));
		const auto prefix = errorCodeNormalized.starts_with("U_REGEX_")
			? 8
			: errorCodeNormalized.starts_with("U_")
			? 2
			: 0;
		errorCodeNormalized.erase(0, prefix);
		std::ranges::transform(
			errorCodeNormalized,
			errorCodeNormalized.begin(),
			[](unsigned char c) -> char
			{
				return (c == '_') ? ' ' : char(std::tolower(c));
			});
		if (errorCodeNormalized.empty()) {
			errorCodeNormalized = "Invalid regular expression";
		} else {
			errorCodeNormalized[0] = char(std::toupper(
				static_cast<unsigned char>(errorCodeNormalized[0])));
		}
		errorMsg = errorCodeNormalized + " at " + std::to_string(parseError.offset);

		if (parseError.preContext[0] != 0 || parseError.postContext[0] != 0) {
			icu::UnicodeString pre(parseError.preContext);
			icu::UnicodeString post(parseError.postContext);
			std::string preStr, postStr;
			pre.toUTF8String(preStr);
			post.toUTF8String(postStr);
			errorMsg += " (near: '" + preStr + "' -> '" + postStr + "')";
		}

		return false;
	}

	return true;
}

not_null<Ui::SlideWrap<Ui::FlatLabel>*> AddError(
	not_null<Ui::VerticalLayout*> content,
	Ui::InputField *input) {

	std::string errorText;
	validateRegex(icu::UnicodeString::fromUTF8("("), errorText);

	const auto error = content->add(
		object_ptr<Ui::SlideWrap<Ui::FlatLabel>>(
			content,
			object_ptr<Ui::FlatLabel>(
				content,
				QString::fromStdString(errorText) + QString::fromStdString(errorText),
				st::settingLocalPasscodeError), st::settingsCheckboxPadding));
	error->hide(anim::type::instant);
	if (input) {
		input->changes() | rpl::on_next(
			[=]
			{
				error->hide(anim::type::normal);
			},
			input->lifetime());
	}
	return error;
}

void RegexEditBuilder(
	not_null<Ui::GenericBox*> box,
	RegexFilter *filter,
	std::optional<long long> dialogId,
	bool showToast
) {
	RegexFilter data;

	if (filter) {
		box->setTitle(showToast ? tr::luxury_RegexFiltersAdd() : tr::luxury_RegexFiltersEdit());
		data = *filter;
	} else {
		box->setTitle(tr::luxury_RegexFiltersAdd());
		data.enabled = true;
		data.caseInsensitive = true;
		data.reversed = false;
	}

	const auto regexValue = box->addRow(
		object_ptr<Ui::InputField>(
			box->verticalLayout(),
			st::windowFilterNameInput,
			Ui::InputField::Mode::MultiLine,
			tr::luxury_RegexFiltersPlaceholder()),
		st::markdownLinkFieldPadding);
	const auto errorText = AddError(box->verticalLayout(), regexValue);
	regexValue->setMaxLength(FiltersCacheController::kMaxPatternLength);
	const auto enabled = box->addRow(
		object_ptr<Ui::Checkbox>(
			box,
			tr::luxury_EnableExpression(tr::now),
			data.enabled,
			st::defaultBoxCheckbox),
		st::settingsCheckboxPadding);
	const auto caseInsensitive = box->addRow(
		object_ptr<Ui::Checkbox>(
			box,
			tr::luxury_CaseInsensitiveExpression(tr::now),
			data.caseInsensitive,
			st::defaultBoxCheckbox),
		st::settingsCheckboxPadding);
	const auto reversed = box->addRow(
		object_ptr<Ui::Checkbox>(
			box,
			tr::luxury_ReversedExpression(tr::now),
			data.reversed,
			st::defaultBoxCheckbox),
		st::settingsCheckboxPadding);

	regexValue->setText(QString::fromStdString(data.text));

	auto saveAndClose = [=, id = data.id]
	{
		const auto text = regexValue->getTextWithTags().text;
		if (text.isEmpty()) {
			return;
		}

		std::string error;
		if (!validateRegex(icu::UnicodeString::fromUTF8(text.toStdString()), error)) {
			errorText->entity()->setText(QString::fromStdString(error));
			errorText->show(anim::type::normal);
			return;
		}

		RegexFilter newFilter;
		newFilter.text = regexValue->getTextWithTags().text.toStdString();
		newFilter.enabled = enabled->checked();
		newFilter.caseInsensitive = caseInsensitive->checked();
		newFilter.reversed = reversed->checked();

		if (!showToast && dialogId.has_value()) {
			newFilter.dialogId = dialogId;
		}

		if (!id.empty()) {
			newFilter.id = id;
		} else {
			const auto bytes = QUuid::createUuid().toRfc4122();
			newFilter.id.assign(bytes.cbegin(), bytes.cend());
		}

		box->closeBox();

		LuxuryDatabase::async([=]
		{
			const auto saved = LuxuryDatabase::addRegexFilter(newFilter);
			if (saved) {
				// Already off the main thread.
				FiltersCacheController::reloadNow();
			}

			crl::on_main([=]
			{
				if (!saved) {
					Luxury::Ui::ShowDatabaseError();
					return;
				}

				FiltersCacheController::fireUpdate();

				if (showToast) {
					auto config = Ui::Toast::Config{
						.text = tr::luxury_RegexFilterBulletinText(
							tr::now,
							tr::rich),
						.adaptive = true,
					};
					if (dialogId.has_value()) {
						Luxury::Ui::ShowToastWithAction(
							std::move(config),
							tr::luxury_RegexFilterBulletinAction(tr::now),
							[=]() mutable {
								newFilter.dialogId = dialogId;

								LuxuryDatabase::async([=] {
									const auto scoped =
										LuxuryDatabase::updateRegexFilter(
											newFilter);
									if (scoped) {
										// Already off the main thread.
										FiltersCacheController::reloadNow();
									}
									crl::on_main([=] {
										if (!scoped) {
											Luxury::Ui::ShowDatabaseError();
											return;
										}
										FiltersCacheController::fireUpdate();
									});
								});
							});
					} else {
						Ui::Toast::Show(std::move(config));
					}
				}
			});
		});
	};

	regexValue->submits() | rpl::on_next(saveAndClose, regexValue->lifetime());
	box->addButton(tr::lng_settings_save(), saveAndClose);
	box->addButton(tr::lng_cancel(),
				   [=]
				   {
					   box->closeBox();
				   });
	box->setFocusCallback([=] {
		regexValue->setFocusFast();
	});

	errorText->entity()->resizeToWidth(box->width());
	errorText->resizeToWidth(box->width());
}

} // namespace

object_ptr<Ui::GenericBox> RegexEditBox(RegexFilter *filter,
										std::optional<long long> dialogId,
										bool showToast) {
	return Box(RegexEditBuilder, filter, dialogId, showToast);
}

} // namespace Settings
