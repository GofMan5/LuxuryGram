// This is the source code of LuxuryGram for Desktop.
// See LEGAL for license and copyright information.
#include "luxury/ui/boxes/plugin_details_box.h"

#include "base/weak_ptr.h"
#include "lang_auto.h"
#include "luxury/plugins/plugin_manager.h"
#include "luxury/ui/boxes/plugin_consent_box.h"
#include "ui/boxes/confirm_box.h"
#include "ui/layers/generic_box.h"
#include "ui/layers/show.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"

#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"

#include <QtCore/QDateTime>
#include <QtGui/QClipboard>
#include <QtGui/QGuiApplication>

#include <cmath>

namespace Luxury::Plugins {
namespace {

constexpr auto kLogLines = 50;

[[nodiscard]] QString LogsText(const QString &id) {
	const auto manager = Manager::Maybe();
	if (!manager) {
		return QString();
	}
	const auto lines = manager->logs(id);
	auto result = QStringList();
	const auto from = std::max(int(lines.size()) - kLogLines, 0);
	for (auto i = from; i < int(lines.size()); ++i) {
		result.push_back(QDateTime::fromMSecsSinceEpoch(
			lines[i].time).toString(u"HH:mm:ss "_q) + lines[i].text);
	}
	return result.join('\n');
}

void AddTitle(not_null<Ui::GenericBox*> box, rpl::producer<QString> text) {
	box->addSkip(st::boxLittleSkip);
	box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		std::move(text),
		st::defaultSubsectionTitle));
}

not_null<Ui::FlatLabel*> AddText(
		not_null<Ui::GenericBox*> box,
		TextWithEntities text,
		const style::FlatLabel &st = st::boxDividerLabel) {
	return box->addRow(
		object_ptr<Ui::FlatLabel>(box, rpl::single(std::move(text)), st));
}

not_null<Ui::SettingsButton*> AddButton(
		not_null<Ui::GenericBox*> box,
		rpl::producer<QString> text,
		Fn<void()> callback,
		const style::SettingsButton &st = st::settingsButtonNoIcon) {
	const auto button = box->addRow(
		object_ptr<Ui::SettingsButton>(box, std::move(text), st),
		style::margins());
	button->setClickedCallback(std::move(callback));
	return button;
}

void Save(const QString &id, const QString &key, nlohmann::json value) {
	if (const auto manager = Manager::Maybe()) {
		manager->setSettingValue(id, key, std::move(value));
	}
}

void AddBoolSetting(
		not_null<Ui::GenericBox*> box,
		const QString &id,
		const QString &key,
		const QString &title,
		bool value) {
	const auto button = AddButton(box, rpl::single(title), nullptr);
	button->toggleOn(rpl::single(value));
	button->toggledChanges() | rpl::on_next([=](bool checked) {
		Save(id, key, checked);
	}, button->lifetime());
}

void AddChoiceSetting(
		not_null<Ui::GenericBox*> box,
		const QString &id,
		const QString &key,
		const QString &title,
		const std::vector<std::string> &options,
		const std::string &value) {
	AddText(box, { title }, st::boxLabel);
	const auto selected = int(std::find(
		options.begin(),
		options.end(),
		value) - options.begin());
	const auto group = std::make_shared<Ui::RadiobuttonGroup>(selected);
	for (auto i = 0; i != int(options.size()); ++i) {
		box->addSkip(st::boxLittleSkip);
		box->addRow(object_ptr<Ui::Radiobutton>(
			box,
			group,
			i,
			QString::fromStdString(options[i]),
			st::defaultBoxCheckbox));
	}
	group->setChangedCallback([=](int index) {
		if (index >= 0 && index < int(options.size())) {
			Save(id, key, options[index]);
		}
	});
}

// Text, Int and Number: saved on submit, focus loss and box close; a
// number out of [min, max] or a malformed color is refused with the field
// error state.
void AddTextSetting(
		not_null<Ui::GenericBox*> box,
		const QString &id,
		const QString &key,
		const QString &title,
		const SettingDescriptor &setting,
		const QString &initial) {
	const auto type = setting.type;
	const auto min = setting.min;
	const auto max = setting.max;
	const auto minNumber = setting.minNumber;
	const auto maxNumber = setting.maxNumber;
	const auto field = box->addRow(object_ptr<Ui::InputField>(
		box,
		st::defaultInputField,
		rpl::single(title),
		initial));
	const auto shown = [](const auto &limit) {
		return limit ? QString::number(*limit) : u"-"_q;
	};
	if ((type == SettingType::Int && (min || max))
		|| (type == SettingType::Number && (minNumber || maxNumber))) {
		const auto isInt = (type == SettingType::Int);
		AddText(box, { tr::lng_luxury_plugins_ui_setting_range(
			tr::now,
			lt_from,
			isInt ? shown(min) : shown(minNumber),
			lt_to,
			isInt ? shown(max) : shown(maxNumber)) });
	}
	const auto last = std::make_shared<QString>(initial);
	const auto commit = [=](bool focus) {
		const auto text = field->getLastText().trimmed();
		const auto refuse = [&] {
			if (focus) {
				field->showError();
			} else {
				field->showErrorNoFocus();
			}
		};
		auto ok = true;
		auto value = nlohmann::json(text.toStdString());
		if (text == *last) {
			return;
		} else if (type == SettingType::Int) {
			const auto number = text.toLongLong(&ok);
			ok = ok && (!min || number >= *min) && (!max || number <= *max);
			value = std::int64_t(number);
		} else if (type == SettingType::Number) {
			const auto number = text.toDouble(&ok);
			ok = ok
				&& std::isfinite(number)
				&& (!minNumber || number >= *minNumber)
				&& (!maxNumber || number <= *maxNumber);
			value = number;
		} else if (type == SettingType::Color) {
			ok = IsValidColor(text.toStdString());
		}
		if (!ok) {
			refuse();
			return;
		}
		*last = text;
		Save(id, key, std::move(value));
	};
	field->submits() | rpl::on_next([=] {
		commit(true);
	}, field->lifetime());
	field->focusedChanges() | rpl::filter(
		!rpl::mappers::_1
	) | rpl::on_next([=] {
		commit(false);
	}, field->lifetime());
	box->boxClosing() | rpl::on_next([=] {
		commit(false);
	}, field->lifetime());
}

void AddSetting(
		not_null<Ui::GenericBox*> box,
		const QString &id,
		const SettingDescriptor &setting) {
	const auto manager = Manager::Maybe();
	if (!manager) {
		return;
	}
	const auto key = QString::fromStdString(setting.key);
	const auto title = QString::fromStdString(setting.title.empty()
		? setting.key
		: setting.title);
	const auto value = manager->settingValue(id, key);
	const auto string = value.is_string()
		? value.get<std::string>()
		: setting.defaultString;
	switch (setting.type) {
	case SettingType::Bool:
		AddBoolSetting(box, id, key, title, value.is_boolean()
			? value.get<bool>()
			: setting.defaultBool);
		break;
	case SettingType::Choice:
	case SettingType::Select:
		AddChoiceSetting(box, id, key, title, setting.options, string);
		break;
	case SettingType::String:
	case SettingType::Color:
	case SettingType::Secret:
		AddTextSetting(
			box,
			id,
			key,
			title,
			setting,
			QString::fromStdString(string));
		break;
	case SettingType::Int:
		AddTextSetting(
			box,
			id,
			key,
			title,
			setting,
			QString::number(value.is_number_integer()
				? value.get<std::int64_t>()
				: setting.defaultInt));
		break;
	case SettingType::Number:
		AddTextSetting(
			box,
			id,
			key,
			title,
			setting,
			QString::number(value.is_number()
				? value.get<double>()
				: setting.defaultNumber));
		break;
	}
}

} // namespace

void FillPluginDetailsBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Ui::Show> show,
		QString id) {
	const auto manager = Manager::Maybe();
	const auto found = manager
		? manager->plugin(id)
		: std::optional<PluginView>();
	box->setWidth(st::boxWideWidth);
	box->addButton(tr::lng_close(), [=] { box->closeBox(); });
	if (!found) {
		box->setTitle(tr::lng_luxury_plugins_ui_title());
		AddText(box, { tr::lng_luxury_plugins_ui_unavailable(tr::now) });
		return;
	}
	const auto &plugin = *found;
	const auto name = plugin.name;
	box->setTitle(name);

	AddText(box, { plugin.version + u" · "_q + plugin.sourceLabel });
	if (!plugin.author.isEmpty()) {
		AddText(box, { plugin.author });
	}
	if (!plugin.description.isEmpty()) {
		box->addSkip(st::boxLittleSkip);
		AddText(box, { plugin.description }, st::boxLabel);
	}
	if (!plugin.homepage.isEmpty()) {
		box->addSkip(st::boxLittleSkip);
		AddText(box, Ui::Text::Link(plugin.homepage, plugin.homepage));
	}
	if (!plugin.lastError.isEmpty()) {
		box->addSkip(st::boxLittleSkip);
		AddText(box, { plugin.lastError }, st::boxAttentionDividerLabel);
	}

	AddTitle(box, tr::lng_luxury_plugins_ui_granted_title());
	if (plugin.granted.empty()) {
		AddText(box, { tr::lng_luxury_plugins_ui_granted_none(tr::now) });
	}
	for (auto i = 0; i != kPermissionCount; ++i) {
		const auto permission = static_cast<Permission>(i);
		if (plugin.granted.has(permission)) {
			AddText(
				box,
				{ PermissionTitle(permission) },
				IsDangerous(permission)
					? st::boxAttentionDividerLabel
					: st::boxDividerLabel);
		}
	}

	if (!plugin.settings.empty()) {
		AddTitle(box, tr::lng_luxury_plugins_ui_settings_title());
		for (const auto &setting : plugin.settings) {
			AddSetting(box, id, setting);
			box->addSkip(st::boxLittleSkip);
		}
	}

	AddTitle(box, tr::lng_luxury_plugins_ui_logs_title());
	const auto logs = AddText(box, {});
	logs->setSelectable(true);
	const auto refreshLogs = [=] {
		const auto text = LogsText(id);
		logs->setMarkedText(text.isEmpty()
			? TextWithEntities{ tr::lng_luxury_plugins_ui_logs_empty(tr::now) }
			: Ui::Text::Code(text));
	};
	refreshLogs();
	AddButton(box, tr::lng_luxury_plugins_ui_logs_copy(), [=] {
		QGuiApplication::clipboard()->setText(LogsText(id));
		show->showToast(tr::lng_text_copied(tr::now));
	});
	AddButton(box, tr::lng_luxury_plugins_ui_logs_clear(), [=] {
		if (const auto manager = Manager::Maybe()) {
			manager->clearLogs(id);
		}
		refreshLogs();
	});

	box->addSkip(st::boxLittleSkip);
	const auto updating = std::make_shared<bool>(false);
	AddButton(box, tr::lng_luxury_plugins_ui_update(), [=] {
		const auto manager = Manager::Maybe();
		if (!manager || *updating) {
			return;
		}
		*updating = true;
		box->showLoading(true);
		manager->prepareUpdate(id, crl::guard(box, [=](
				InstallPlan plan,
				QString error) {
			*updating = false;
			box->showLoading(false);
			if (!error.isEmpty()) {
				show->showToast(error);
				return;
			}
			box->closeBox();
			RequestInstall(show, std::move(plan));
		}));
	});
	AddButton(box, tr::lng_luxury_plugins_ui_reload(), [=] {
		if (const auto manager = Manager::Maybe()) {
			manager->reload(id);
			show->showToast(tr::lng_luxury_plugins_ui_reloaded(
				tr::now,
				lt_name,
				name));
		}
		refreshLogs();
	});
	const auto weak = base::make_weak(box);
	AddButton(box, tr::lng_luxury_plugins_ui_remove(), [=] {
		show->showBox(Ui::MakeConfirmBox({
			.text = tr::lng_luxury_plugins_ui_remove_sure(
				tr::now,
				lt_name,
				name),
			.confirmed = [=](Fn<void()> &&close) {
				if (const auto manager = Manager::Maybe()) {
					manager->remove(id);
				}
				close();
				if (const auto strong = weak.get()) {
					strong->closeBox();
				}
			},
			.confirmText = tr::lng_box_remove(),
			.confirmStyle = &st::attentionBoxButton,
		}));
	}, st::settingsAttentionButton);
}

} // namespace Luxury::Plugins
