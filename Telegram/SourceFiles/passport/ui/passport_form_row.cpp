/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "passport/ui/passport_form_row.h"

#include "lang/lang_keys.h"
#include "ui/text/text_options.h"
#include "ui/painter.h"
#include "styles/style_passport.h"
#include "styles/style_layers.h"

namespace Passport::Ui {

FormRow::FormRow(QWidget *parent)
: RippleButton(parent, st::passportRowRipple)
, _title(st::boxWideWidth / 2)
, _description(st::boxWideWidth / 2) {
}

void FormRow::updateContent(
		const QString &title,
		const QString &description,
		bool ready,
		bool error,
		anim::type animated) {
	_title.setText(
		st::semiboldTextStyle,
		title,
		NameTextOptions());
	_description.setText(
		st::defaultTextStyle,
		description,
		TextParseOptions {
			TextParseMultiline,
			0,
			0,
			Qt::LayoutDirectionAuto
		});
	_ready = ready && !error;
	if (_error != error) {
		_error = error;
		if (animated == anim::type::instant) {
			_errorAnimation.stop();
		} else {
			_errorAnimation.start(
				[=] { update(); },
				_error ? 0. : 1.,
				_error ? 1. : 0.,
				st::fadeWrapDuration);
		}
	}
	const auto weak = base::make_weak(this);
	resizeToWidth(width());
	if (!weak) {
		return;
	}
	update();
	accessibilityNameChanged();
	if (!weak) {
		return;
	}
	accessibilityDescriptionChanged();
	if (weak) {
		accessibilityValueChanged();
	}
}

QString FormRow::accessibilityName() {
	return _title.toString();
}

QString FormRow::accessibilityDescription() {
	const auto description = _description.toString();
	const auto status = accessibilityValue();
	return description.isEmpty() ? status : (description + '\n' + status);
}

QString FormRow::accessibilityValue() const {
	return (_error
		? tr::luxury_PassportRowError
		: _ready
		? tr::luxury_PassportRowReady
		: tr::luxury_PassportRowMissing)(tr::now);
}

int FormRow::resizeGetHeight(int newWidth) {
	const auto availableWidth = countAvailableWidth(newWidth);
	_titleHeight = _title.countHeight(availableWidth);
	_descriptionHeight = _description.countHeight(availableWidth);
	const auto result = st::passportRowPadding.top()
		+ _titleHeight
		+ st::passportRowSkip
		+ _descriptionHeight
		+ st::passportRowPadding.bottom();
	return result;
}

int FormRow::countAvailableWidth(int newWidth) const {
	return newWidth
		- st::passportRowPadding.left()
		- st::passportRowPadding.right()
		- (_ready
			? st::passportRowReadyIcon
			: st::passportRowEmptyIcon).width()
		- st::passportRowIconSkip;
}

int FormRow::countAvailableWidth() const {
	return countAvailableWidth(width());
}

void FormRow::paintEvent(QPaintEvent *e) {
	Painter p(this);

	paintRipple(p, 0, 0);

	const auto left = st::passportRowPadding.left();
	const auto availableWidth = countAvailableWidth();
	auto top = st::passportRowPadding.top();

	const auto error = _errorAnimation.value(_error ? 1. : 0.);

	p.setPen(st::passportRowTitleFg);
	_title.drawLeft(p, left, top, availableWidth, width());
	top += _titleHeight + st::passportRowSkip;

	p.setPen(anim::pen(
		st::passportRowDescriptionFg,
		st::boxTextFgError,
		error));
	_description.drawLeft(p, left, top, availableWidth, width());
	top += _descriptionHeight + st::passportRowPadding.bottom();

	const auto &icon = _ready
		? st::passportRowReadyIcon
		: st::passportRowEmptyIcon;
	if (error > 0. && !_ready) {
		icon.paint(
			p,
			width() - st::passportRowPadding.right() - icon.width(),
			(height() - icon.height()) / 2,
			width(),
			anim::color(st::menuIconFgOver, st::boxTextFgError, error));
	} else {
		icon.paint(
			p,
			width() - st::passportRowPadding.right() - icon.width(),
			(height() - icon.height()) / 2,
			width());
	}
	paintKeyboardFocus(p);
}

} // namespace Passport::Ui
