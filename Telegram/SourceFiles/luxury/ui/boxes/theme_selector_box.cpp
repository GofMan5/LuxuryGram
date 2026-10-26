// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "luxury/ui/boxes/theme_selector_box.h"

#include "lang_auto.h"
#include "luxury/features/message_shot/message_shot_theme_state.h"
#include "luxury/ui/utils/box_buttons.h"
#include "luxury/utils/telegram_helpers.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_origin.h"
#include "data/data_session.h"
#include "main/main_session.h"
#include "settings/sections/settings_chat.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"
#include "ui/toast/toast.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "window/themes/window_theme.h"
#include "window/themes/window_themes_cloud_list.h"
#include "window/themes/window_theme_preview.h"

ThemeSelectorBox::ThemeSelectorBox(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
	: _controller(controller) {
}

rpl::producer<ShotThemeSelection> ThemeSelectorBox::themeSelected() {
	return _themes.events();
}

void ThemeSelectorBox::prepare() {
	setupContent();
}

void ThemeSelectorBox::setApplyEnabled(bool enabled) {
	if (_applyButton) {
		Luxury::Ui::SetBoxButtonEnabled(_applyButton.data(), enabled);
	}
}

void ThemeSelectorBox::setupContent() {
	using namespace Settings;

	setTitle(tr::luxury_MessageShotThemeSelectTitle());

	auto wrap2 = object_ptr<Ui::VerticalLayout>(this);
	const auto container = wrap2.data();

	setInnerWidget(object_ptr<Ui::OverrideMargins>(
		this,
		std::move(wrap2)));

	AddSubsectionTitle(container, tr::lng_settings_themes());
	AddSkip(container, st::settingsThemesTopSkip);

	Settings::SetupDefaultThemes(&_controller->window(), container);

	AddSkip(container);

	using namespace Window::Theme;
	using namespace rpl::mappers;

	const auto wrap = container->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			container,
			object_ptr<Ui::VerticalLayout>(container))
	)->setDuration(0);
	const auto inner = wrap->entity();

	AddDivider(inner);
	AddSkip(inner);

	const auto title = AddSubsectionTitle(
		inner,
		tr::lng_settings_bg_cloud_themes());
	const auto showAll = Ui::CreateChild<Ui::LinkButton>(
		inner,
		tr::lng_settings_bg_show_all(tr::now));

	rpl::combine(
		title->topValue(),
		inner->widthValue(),
		showAll->widthValue()
	) | rpl::on_next([=](int top, int outerWidth, int width)
							 {
								 showAll->moveToRight(
									 st::defaultSubsectionTitlePadding.left(),
									 top,
									 outerWidth);
							 },
							 showAll->lifetime());

	Ui::AddSkip(inner, st::settingsThemesTopSkip);

	const auto list = inner->lifetime().make_state<CloudList>(
		inner,
		_controller);
	inner->add(
		list->takeWidget(),
		style::margins(
			st::settingsButtonNoIcon.padding.left(),
			0,
			st::settingsButtonNoIcon.padding.right(),
			0));

	list->allShown(
	) | rpl::on_next([=](bool shown)
							 {
								 showAll->setVisible(!shown);
							 },
							 showAll->lifetime());

	showAll->addClickHandler([=]
	{
		list->showAll();
	});

	wrap->setDuration(0)->toggleOn(list->empty() | rpl::map(!_1));

	_controller->session().data().cloudThemes().refresh();

	LuxuryFeatures::MessageShot::themeChosen(
	) | rpl::on_next(
		[=](Data::CloudTheme theme)
		{
			const auto generation = ++_selectionGeneration;
			_selectionLifetime.destroy();
			_selection.reset();
			setApplyEnabled(false);
			if (!theme.documentId) {
				// Emoji themes have no file: document(0) would be an empty
				// DocumentData whose download never finishes.
				showToast(tr::luxury_MessageShotThemePreviewFailed(tr::now));
				return;
			}
			const auto document = _controller->session().data().document(theme.documentId);
			const auto documentView = document->createMediaView();

			document->save(
				Data::FileOriginTheme(theme.id, theme.accessHash),
				QString());

			const auto innerCallback = [=]
			{
				if (generation != _selectionGeneration) {
					return;
				}
				auto preview = Window::Theme::PreviewFromFile(
					documentView->bytes(),
					document->location().name(),
					theme);
				if (!preview) {
					showToast(
						tr::luxury_MessageShotThemePreviewFailed(
							tr::now));
					return;
				}

				_selection = ShotThemeSelection{
					.palette = preview->instance.palette.save(),
					.cloud = theme,
					.name = theme.title,
				};
				setApplyEnabled(true);
			};

			if (documentView->loaded()) {
				innerCallback();
			} else {
				DocumentLoadUpdates(
					&_controller->session(),
					document
				) | rpl::filter(
					[=]
					{
						return documentView->loaded() || !document->loading();
					}) | rpl::take(
					1 // Or every later downloader tick re-parses the theme.
					) | rpl::on_next(
					[=]
					{
						innerCallback();
					},
					_selectionLifetime);
			}
		},
		lifetime());

	LuxuryFeatures::MessageShot::paletteChosen(
	) | rpl::on_next([=](const auto &palette)
							 {
								 ++_selectionGeneration;
								 _selectionLifetime.destroy();
								 const auto type = LuxuryFeatures::MessageShot::getSelectedFromDefault();
								 const auto name = (type != Window::Theme::EmbeddedType(-1))
									 ? LuxuryFeatures::MessageShot::embeddedThemeDisplayName(type)
									 : tr::luxury_MessageShotThemeDefault(tr::now);
								 _selection = ShotThemeSelection{
									 .palette = palette.save(),
									 .embedded = type,
									 .color = LuxuryFeatures::MessageShot::getSelectedColorFromDefault(),
									 .name = name,
								 };
								 setApplyEnabled(true);
							 },
							 lifetime());

	_applyButton = addButton(tr::luxury_MessageShotThemeApply(), [=] {
		if (!_selection) {
			return;
		}
		_themes.fire_copy(*_selection);
		closeBox();
	});
	setApplyEnabled(_selection.has_value());
	boxClosing() | rpl::on_next([=] {
		++_selectionGeneration;
		_selectionLifetime.destroy();
	}, lifetime());

	setDimensionsToContent(st::boxWidth, container);
}
