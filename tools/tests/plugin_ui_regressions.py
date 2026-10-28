"""Plugin manager UI: strings, section registration, consent and Manager API.

The plugin UI (settings section, install, consent and details boxes) is
checked statically because the client is not built here:
- every lng_luxury_plugins_ui_ key used in code exists in English and Russian,
  with the same {tags}, and no unused key is left behind;
- the Plugins section is reachable from the LuxuryGram settings page;
- the consent box paints dangerous permissions in the attention color;
- commitInstall is reached only through the consent box (or an update that
  asks for no new permission);
- every Manager call in the UI names a method of plugin_manager.h;
- every lifecycle ErrorCode has a translated text and no raw error text
  reaches a toast or label; the codes whose text is localized or precise
  (manifest field, rate limit time, conflict, version) keep it;
- the consent box warns about combined permissions, marks new hosts and
  names the update; an install in progress has Cancel;
- Update shows only with an update, reload and remove report back, the
  details box follows the log and masks secret settings in a wrap row
  (MaskedInputField is not an RpWidget), rows update in place, the pending
  rows rebuild off the Cancel click and Settings > Plugins counts the
  updates;
- plugin_consent_logic.h is compiled with -Wall -Wextra -Werror and run.
Each check is also run against a mutant it must reject.

Bounded source check; not a native acceptance.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile
from library_regressions import CXX, ROOT, STATIC, function, run, source, standalone


PREFIX = 'lng_luxury_plugins_ui_'
BOXES = 'Telegram/SourceFiles/luxury/ui/boxes/'
SECTION = 'Telegram/SourceFiles/luxury/ui/settings/settings_plugins.cpp'
MAIN = 'Telegram/SourceFiles/luxury/ui/settings/settings_main.cpp'
CONSENT = BOXES + 'plugin_consent_box.cpp'
INSTALL = BOXES + 'plugin_install_box.cpp'
DETAILS = BOXES + 'plugin_details_box.cpp'
UI = (SECTION, MAIN, CONSENT, INSTALL, DETAILS)
HEADER = 'Telegram/SourceFiles/luxury/plugins/plugin_manager.h'
LOGIC = 'Telegram/SourceFiles/luxury/plugins/plugin_install_logic.h'
MANAGER = 'Telegram/SourceFiles/luxury/plugins/plugin_manager.cpp'
HOST_API = 'Telegram/SourceFiles/luxury/plugins/plugin_host_api.cpp'
EN = 'Telegram/Resources/langs/lang.strings'
RU = 'Telegram/SourceFiles/luxury/luxury_settings.cpp'
CONSENT_LOGIC = 'luxury/ui/boxes/plugin_consent_logic.h'


def tags(text):
    return sorted(re.findall(r'\{(\w+)\}', text))


def check_strings(files):
    used = set()
    for path in UI:
        used |= set(re.findall(r'tr::(' + PREFIX + r'\w+)\(', files[path]))
    en = dict(re.findall(
        r'^"(' + PREFIX + r'\w+)" = "((?:[^"\\]|\\.)*)";',
        files[EN],
        re.M))
    ru = dict(re.findall(
        r'\{ "(' + PREFIX + r'\w+)", "([^"]*)" \},',
        files[RU]))
    assert used, 'no plugin UI strings found in code'
    assert used <= set(en), f'keys missing in lang.strings: {sorted(used - set(en))}'
    assert used <= set(ru), f'keys missing in the RU table: {sorted(used - set(ru))}'
    assert set(en) <= used, f'unused EN keys: {sorted(set(en) - used)}'
    assert set(ru) <= used, f'unused RU keys: {sorted(set(ru) - used)}'
    for key in used:
        assert tags(en[key]) == tags(ru[key]), f'{key}: EN and RU tags differ'


def check_section(files):
    main = files[MAIN]
    assert '#include "luxury/ui/settings/settings_plugins.h"' in main, (
        'settings_main does not include the plugins section')
    assert '.targetSection = LuxuryPlugins::Id(),' in function(
        main, 'void BuildCategories('), 'the plugins section has no button'
    assert '.parentId = LuxuryMain::Id(),' in files[SECTION], (
        'the plugins section is not parented to the LuxuryGram page')


def check_danger(files):
    box = function(files[CONSENT], 'void FillConsentBox(')
    assert 'const auto dangerous = IsDangerous(permission);' in box, (
        'the consent box does not ask which permission is dangerous')
    assert re.search(
        r'dangerous \? std::optional<QColor>\(attention\) : std::nullopt',
        box), 'dangerous permissions are not painted in the attention color'
    assert 'const auto dangerous = asked.anyDangerous();' in box
    assert 'tr::lng_luxury_plugins_ui_consent_danger(' in box, (
        'the consent box has no danger warning')


def check_consent(files):
    for path in UI:
        count = files[path].count('commitInstall(')
        expected = 1 if path == CONSENT else 0
        assert count == expected, f'{path}: commitInstall bypasses the consent box'
    text = files[CONSENT]
    commit = function(text, 'void CommitInstall(')
    assert 'manager->commitInstall(std::move(plan), [=](QString error, ErrorCode code) {' in commit, (
        'the consent box must not pass its own grant: the manager grants what was shown')
    box = function(text, 'void FillConsentBox(')
    assert re.search(
        r'const auto asked = plan\.update\s*\? plan\.newPermissions\s*'
        r': manifest\.permissions;', box), (
        'an update must list only the new permissions')
    assert 'const auto hostsShown = asked.has(Permission::Network) || widened;' in box, (
        'widened network hosts are not shown for consent')
    assert 'if (hostsShown) {' in box, 'the host list is not tied to hostsShown'
    rest = text.replace(commit, '')
    calls = [m.start() for m in re.finditer(r'\bCommitInstall\(', rest)]
    assert len(calls) == 2, 'CommitInstall must be called from Allow and RequestInstall only'
    allow = box[box.index('tr::lng_luxury_plugins_ui_consent_allow()'):]
    allow = allow[:allow.index('});')]
    assert 'allowed.consented = true;' in allow and (
        'CommitInstall(show, std::move(allowed));' in allow), (
        'the consent Allow button does not commit a consented plan')
    assert 'allowed.shownPermissions = asked;' in allow, (
        'Allow does not record the permissions the box listed')
    assert re.search(
        r'if \(hostsShown\) \{\s*allowed\.shownHosts = allowed\.manifest\.network;', allow), (
        'Allow does not record the hosts the box listed')
    request = function(text, 'void RequestInstall(')
    assert re.search(
        r'if \(plan\.update && plan\.newPermissions\.empty\(\) '
        r'&& !HostsWidened\(plan\)\) \{\s*'
        r'CommitInstall\(', request), (
        'RequestInstall commits without consent outside a no-new-permission update')
    for path in (INSTALL, DETAILS):
        assert 'RequestInstall(show, std::move(plan));' in files[path], (
            f'{path}: a prepared plan does not go through RequestInstall')


def manager_methods(header):
    body = header[header.index('class Manager final {'):]
    body = body[:body.index('\nprivate:')]
    return set(re.findall(r'\b(\w+)\(', body)) - {'Manager'}


def check_api(files):
    methods = manager_methods(files[HEADER])
    assert {'prepareInstall', 'commitInstall', 'plugins', 'logs'} <= methods
    for path in UI:
        calls = set(re.findall(r'manager->(\w+)\(', files[path]))
        calls |= set(re.findall(r'Manager::(\w+)\(', files[path]))
        assert calls <= methods, f'{path}: not in plugin_manager.h: {sorted(calls - methods)}'


def check_read_scope(files):
    # messages.read gets every incoming update, not only open chats: the
    # consent text must not undersell it.
    en = re.search(r'"lng_luxury_plugins_ui_perm_messages_read_about" = "([^"]*)";', files[EN])
    ru = re.search(r'\{ "lng_luxury_plugins_ui_perm_messages_read_about", "([^"]*)" \}', files[RU])
    assert en and 'every incoming message' in en.group(1), (
        'EN messages.read text does not say the plugin sees every incoming message')
    assert ru and 'все входящие сообщения' in ru.group(1), (
        'RU messages.read text does not say the plugin sees every incoming message')


def check_errors(files):
    # Every ErrorCode but None has its own text in both languages.
    enum = function(files[LOGIC], 'enum class ErrorCode {')
    codes = re.findall(r'^\t(\w+),', enum, re.M)
    assert 'None' in codes and 'Consent' in codes, 'ErrorCode enum not found'
    texts = function(files[CONSENT], 'QString ErrorText(ErrorCode code, const QString &detail) {')
    assert re.search(
        r'^\tif \(!detail\.isEmpty\(\) && KeepsErrorDetail\(code\)\) \{\n\t\treturn detail;',
        texts, re.M), 'ErrorText drops the localized or precise detail'
    for code in codes:
        if code == 'None':
            continue
        found = re.search(
            r'case ErrorCode::' + code + r':\n\t\treturn tr::(lng_\w+)\(tr::now\);',
            texts)
        assert found, f'ErrorCode::{code} has no translated text'
        key = found.group(1)
        assert re.search(r'^"' + key + '" = ', files[EN], re.M), f'{key} missing in lang.strings'
        assert '{ "' + key + '", ' in files[RU], f'{key} missing in the RU table'
    # The texts the host and the manager store in lastError are mapped.
    last = function(files[CONSENT], 'QString LastErrorText(const QString &error) {')
    for literal, path in (
            ('The plugin used too much CPU time.', HOST_API),
            ('code/plugin.json is missing or broken.', MANAGER)):
        stored = f'u"{literal}"_q'
        assert stored in files[path], f'{path} no longer stores: {literal}'
        assert re.search(
            re.escape(stored) + r'\) \{\n\t\treturn tr::lng_\w+\(tr::now\);', last), (
            f'LastErrorText does not translate: {literal}')
    # Raw English or Qt error text never reaches the user.
    for path in UI:
        raw = re.findall(r'(?:showToast|setText)\(error\)', files[path])
        assert not raw, f'{path}: raw error text shown'
    assert 'error->entity()->setText(ErrorText(plan.errorCode, text));' in files[INSTALL], (
        'the install box does not route the prepareInstall error through ErrorText')
    assert 'show->showToast(ErrorText(plan.errorCode, error));' in files[DETAILS], (
        'Update does not route the prepareUpdate error through ErrorText')
    assert 'ErrorText(code, error)' in function(files[CONSENT], 'void CommitInstall('), (
        'a failed install does not route the commitInstall error through ErrorText')
    assert 'Plugins::LastErrorText(plugin.lastError)' in function(
        files[SECTION], 'QString StateText('), 'a plugin row shows the raw lastError'


def check_consent_texts(files):
    box = function(files[CONSENT], 'void FillConsentBox(')
    assert 'for (const auto combined : CombinedDangers(asked, manifest.permissions)) {' in box, (
        'the consent box does not warn about combined permissions')
    for key in ('combined_messages', 'combined_account', 'combined_clipboard'):
        assert f'tr::{PREFIX}{key}(tr::now)' in box, f'{key} is not shown'
    assert 'NewHosts(old, manifest.network)' in box and (
        'hosts.append(Ui::Text::Bold(text));' in box), 'new hosts are not marked'
    assert re.search(
        r'box->addButton\(plan\.update\s*\? tr::lng_luxury_plugins_ui_consent_allow_update\(\)',
        box), 'an update is not allowed with "Allow and update"'
    assert 'installed->version + ' in box, 'an update does not show the old version'
    assert 'manifest.description' in box, 'the consent box has no description'


def check_progress(files):
    commit = function(files[CONSENT], 'void CommitInstall(')
    assert 'Pending().list.push_back(' in commit and 'RemovePending(id)' in commit, (
        'commitInstall is not listed as pending')
    cancel = function(files[CONSENT], 'void CancelInstall(const QString &id) {')
    assert 'manager->cancelInstall(id);' in cancel, 'Cancel does not reach the manager'
    pending = function(files[SECTION], 'void FillPending(')
    assert 'tr::lng_luxury_plugins_ui_install_cancel()' in pending and (
        'Plugins::CancelInstall(id);' in pending), 'an install in progress has no Cancel'
    build = function(files[SECTION], 'void BuildList(')
    assert re.search(
        r'Plugins::PendingInstallsChanges\(\s*\) \| rpl::on_next\(\[=\] \{\s*'
        r'if \(!std::exchange\(\*pendingScheduled, true\)\) \{\s*'
        r'crl::on_main\(pending, \[=\] \{\s*\*pendingScheduled = false;\s*refillPending\(\);',
        build), 'the pending rows are rebuilt under the Cancel click'
    manager = function(
        files[MANAGER],
        'void Manager::commitInstall(\n\t\tInstallPlan plan,\n'
        '\t\tFn<void(QString error, ErrorCode code)> done) {')
    write = manager.index('WriteFile(staging + u"/code/plugin.json"_q, raw)')
    checks = [m.start() for m in re.finditer(
        r'else if \(d->cancelled\.contains\(id\)\) \{\n\t+'
        r'finish\(u"The install was cancelled\."_q, ErrorCode::Busy\);',
        manager)]
    assert len(checks) == 2 and checks[-1] < write, (
        'a cancel is not checked before the files are written')
    assert 'd->cancelled.erase(id);' in manager, 'a cancel outlives its install'
    actions = function(files[SECTION], 'void BuildActions(')
    assert 'manager->checkUpdatesDetailed(' in actions and re.search(
        r'if \(result\.failed\) \{\s*const auto failed = tr::lng_luxury_plugins_ui_check_failed\(',
        actions) and re.search(
            r'Plugins::ErrorText\(\s*result\.firstCode,\s*result\.firstError\)',
            actions), (
        'a failed update check is not reported')


def check_details(files):
    details = files[DETAILS]
    assert re.search(
        r'if \(plugin\.updateAvailable\) \{\s*AddButton\(box, tr::lng_luxury_plugins_ui_update\(\)',
        details), 'Update is shown without an available update'
    assert details.count('tr::lng_luxury_plugins_ui_update()') == 1
    assert re.search(r'manager->logAdded\(\) \| rpl::filter\(\s*rpl::mappers::_1 == id', details), (
        'the log does not follow logAdded')
    assert 'LogsText(id, level->current())' in details and 'line.level >= minLevel' in details, (
        'the log has no level filter')
    reload = details[details.index('tr::lng_luxury_plugins_ui_reload(),'):]
    reload = reload[:reload.index('\n\t});')]
    assert 'tr::lng_luxury_plugins_ui_reload_failed(' in reload and (
        'tr::lng_luxury_plugins_ui_reloaded(' in reload), 'reload does not report back'
    assert 'tr::lng_luxury_plugins_ui_removed(' in details, 'remove does not report back'
    secret = function(details, 'void AddSecretSetting(')
    # MaskedInputField is RpWidgetBase<QLineEdit>, not RpWidget: addRow
    # does not take it, it lives in a FixedHeightWidget row.
    assert 'object_ptr<Ui::PasswordInput>(' not in secret and re.search(
        r'const auto wrap = box->addRow\(object_ptr<Ui::FixedHeightWidget>\(\s*box,',
        secret) and 'Ui::CreateChild<Ui::PasswordInput>(\n\t\twrap,' in secret and re.search(
        r'wrap->widthValue\(\) \| rpl::on_next\(\[=\]\(int width\) \{\s*'
        r'field->move\(0, 0\);\s*field->resize\(width, field->height\(\)\);\s*'
        r'wrap->resize\(width, field->height\(\)\);', secret) and (
        'Save(id, key, text.toStdString());' in secret), 'a secret setting is not a masked field'
    setting = function(details, 'void AddSetting(')
    assert re.search(r'if \(setting\.type == SettingType::Secret\) \{\s*AddSecretSetting\(', setting), (
        'a secret setting is not routed to the masked field')
    assert 'FindLocale(' in setting and 'AddText(box, { description });' in setting, (
        'locales or the description of a setting are not shown')


def check_rows(files):
    section = files[SECTION]
    state = function(section, 'void AddStateLabel(')
    assert 'labelSt->maxHeight = ' in state, 'a long error is not elided'
    assert 'toggle->setAccessibleDescription(text);' in state, 'the state is not accessible'
    rows = function(section, 'void FillList(')
    assert 'AddStateLabel(list, toggle, value());' in rows, (
        'the state label does not update in place')
    categories = function(files[MAIN], 'void BuildCategories(')
    assert re.search(r'CreateRightLabel\(\s*plugins,', categories) and (
        'p.updateAvailable' in categories), 'Settings > Plugins has no update badge'


CHECKS = (check_strings, check_read_scope, check_section, check_danger, check_consent, check_api,
    check_errors, check_consent_texts, check_progress, check_details, check_rows)

MUTANTS = (
    (check_strings, EN, '"lng_luxury_plugins_ui_title" = ', '"lng_luxury_plugins_ui_titlex" = '),
    (check_strings, RU, '{ "lng_luxury_plugins_ui_title", ', '{ "lng_luxury_plugins_ui_titlex", '),
    (check_strings, RU, 'Доступно обновлений: {number}', 'Доступно обновлений: {count}'),
    (check_section, MAIN, '.targetSection = LuxuryPlugins::Id(),', '.targetSection = LuxuryOther::Id(),'),
    (check_danger, CONSENT, 'dangerous ? std::optional<QColor>(attention)', 'false ? std::optional<QColor>(attention)'),
    (check_consent, INSTALL, 'RequestInstall(show, std::move(plan));', 'manager->commitInstall(std::move(plan), nullptr);'),
    (check_consent, CONSENT, 'if (plan.update && plan.newPermissions.empty() && !HostsWidened(plan)) {', 'if (plan.update && plan.newPermissions.empty()) {'),
    (check_consent, CONSENT, 'allowed.shownPermissions = asked;', 'allowed.shownPermissions = allowed.manifest.permissions;'),
    (check_consent, CONSENT, 'allowed.shownHosts = allowed.manifest.network;', 'allowed.shownHosts = {};'),
    (check_consent, CONSENT, '? plan.newPermissions\n', '? manifest.permissions\n'),
    (check_consent, CONSENT, 'const auto hostsShown = asked.has(Permission::Network) || widened;', 'const auto hostsShown = asked.has(Permission::Network);'),
    (check_section, SECTION, '.parentId = LuxuryMain::Id(),', '.parentId = LuxuryOther::Id(),'),
    (check_read_scope, EN, 'Sees every incoming message of this account', 'Sees the messages'),
    (check_read_scope, RU, 'Видит все входящие сообщения этого аккаунта', 'Видит сообщения'),
    (check_api, DETAILS, 'manager->reload(id);', 'manager->restart(id);'),
    (check_errors, CONSENT, '\tcase ErrorCode::Files:\n\t\treturn tr::lng_luxury_plugins_ui_error_files(tr::now);\n', ''),
    (check_errors, EN, '"lng_luxury_plugins_ui_error_files" = ', '"lng_luxury_plugins_ui_error_filez" = '),
    (check_errors, RU, '{ "lng_luxury_plugins_pinned", ', '{ "lng_luxury_plugins_pinnex", '),
    (check_errors, INSTALL, 'error->entity()->setText(ErrorText(plan.errorCode, text));', 'error->entity()->setText(text);'),
    (check_errors, INSTALL, 'error->entity()->setText(ErrorText(plan.errorCode, text));', 'error->entity()->setText(ErrorText(plan.errorCode));'),
    (check_errors, DETAILS, 'show->showToast(ErrorText(plan.errorCode, error));', 'show->showToast(error);'),
    (check_errors, DETAILS, 'show->showToast(ErrorText(plan.errorCode, error));', 'show->showToast(ErrorText(plan.errorCode));'),
    (check_errors, CONSENT, 'ErrorText(code, error)', 'ErrorText(code)'),
    (check_errors, CONSENT, '\tif (!detail.isEmpty() && KeepsErrorDetail(code)) {\n\t\treturn detail;', '\tif (false) {\n\t\treturn detail;'),
    (check_errors, CONSENT, 'return tr::lng_luxury_plugins_ui_error_cpu(tr::now);', 'return error;'),
    (check_consent_texts, CONSENT, 'for (const auto combined : CombinedDangers(asked, manifest.permissions)) {', 'for (const auto combined : std::vector<CombinedDanger>()) {'),
    (check_consent_texts, CONSENT, '? tr::lng_luxury_plugins_ui_consent_allow_update()', '? tr::lng_luxury_plugins_ui_consent_allow()'),
    (check_consent_texts, CONSENT, 'hosts.append(Ui::Text::Bold(text));', 'hosts.append(text);'),
    (check_progress, SECTION, 'Plugins::CancelInstall(id);', '(void)id;'),
    (check_progress, SECTION, 'tr::lng_luxury_plugins_ui_install_cancel(),', 'tr::lng_cancel(),'),
    (check_progress, MANAGER, '\t\t\t} else if (d->cancelled.contains(id)) {', '\t\t\t} else if (false) {'),
    (check_progress, SECTION, 'if (result.failed) {', 'if (false) {'),
    (check_progress, SECTION, '\t\t\t\t\t\t\tresult.firstError));', '\t\t\t\t\t\t\tQString()));'),
    (check_progress, SECTION, ') | rpl::on_next([=] {\n\t\t\tif (!std::exchange(*pendingScheduled, true)) {', ') | rpl::on_next(refillPending, pending->lifetime());\n\t\t(void)[=] {\n\t\t\tif (!std::exchange(*pendingScheduled, true)) {'),
    (check_details, DETAILS, 'if (plugin.updateAvailable) {', 'if (true) {'),
    (check_details, DETAILS, 'Ui::CreateChild<Ui::PasswordInput>(\n\t\twrap,', 'box->addRow(object_ptr<Ui::PasswordInput>(\n\t\tbox,'),
    (check_details, DETAILS, 'Ui::CreateChild<Ui::PasswordInput>(', 'Ui::CreateChild<Ui::InputField>('),
    (check_details, DETAILS, '\t\tfield->resize(width, field->height());\n\t\twrap->resize', '\t\twrap->resize'),
    (check_details, DETAILS, 'if (setting.type == SettingType::Secret) {', 'if (false) {'),
    (check_details, DETAILS, 'line.level >= minLevel', 'minLevel >= 0'),
    (check_details, DETAILS, 'manager->logAdded() | rpl::filter(', 'manager->changes() | rpl::filter('),
    (check_rows, SECTION, 'toggle->setAccessibleDescription(text);', '(void)text;'),
    (check_rows, MAIN, 'p.updateAvailable', 'p.enabled'),
)

LOGIC_MAIN = r'''
#include "luxury/ui/boxes/plugin_consent_logic.h"

#include <cstdlib>
#include <iostream>

using namespace Luxury::Plugins;

#define EXPECT(condition) do { if (!(condition)) { \
	std::cerr << "line " << __LINE__ << ": " #condition "\n"; std::abort(); } } while (0)

namespace {

PermissionSet Set(std::initializer_list<Permission> list) {
	auto result = PermissionSet();
	for (const auto permission : list) {
		result.add(permission);
	}
	return result;
}

using D = std::vector<CombinedDanger>;

} // namespace

int main() {
	using P = Permission;
	const auto all = Set({ P::MessagesRead, P::TelegramApi, P::Clipboard, P::Network });
	// A fresh install asks for all of it: every pair is complete.
	EXPECT((CombinedDangers(all, all) == D{
		CombinedDanger::MessagesOut,
		CombinedDanger::AccountOut,
		CombinedDanger::ClipboardOut }));
	// No network, no way out.
	const auto local = Set({ P::MessagesRead, P::TelegramApi, P::Clipboard });
	EXPECT(CombinedDangers(local, local).empty());
	// The update stream counts as reading messages.
	const auto updates = Set({ P::TelegramUpdates, P::Network });
	EXPECT(CombinedDangers(updates, updates) == D{ CombinedDanger::MessagesOut });
	// An update adding Network completes every pair already granted.
	EXPECT(CombinedDangers(Set({ P::Network }), all).size() == 3);
	// An update adding the clipboard to a networked plugin completes that pair.
	EXPECT(CombinedDangers(Set({ P::Clipboard }), all) == D{ CombinedDanger::ClipboardOut });
	// An update adding neither half warns about nothing.
	EXPECT(CombinedDangers(Set({ P::Ui }), all).empty());

	EXPECT((NewHosts({ "a.com" }, { "a.com", "b.com" }) == std::vector<std::string>{ "b.com" }));
	EXPECT(NewHosts({ "*" }, { "a.com" }).empty());
	EXPECT(NewHosts({ "a.com", "b.com" }, { "b.com" }).empty());

	auto locales = Locales();
	locales["ru"] = { "Имя", "Описание" };
	locales["pt-br"] = { "Nome", "" };
	EXPECT(FindLocale(locales, "ru") && FindLocale(locales, "ru")->name == "Имя");
	EXPECT(FindLocale(locales, "RU") == FindLocale(locales, "ru"));
	EXPECT(FindLocale(locales, "ru-RU") == FindLocale(locales, "ru"));
	EXPECT(FindLocale(locales, "pt-BR") && FindLocale(locales, "pt-BR")->name == "Nome");
	EXPECT(FindLocale(locales, "pt") == nullptr);
	EXPECT(FindLocale(locales, "en") == nullptr);

	// Localized or field-precise texts are kept, transport noise is not.
	for (const auto code : { ErrorCode::Manifest, ErrorCode::RateLimited,
			ErrorCode::Conflict, ErrorCode::Incompatible, ErrorCode::Downgrade,
			ErrorCode::Pinned, ErrorCode::UpToDate }) {
		EXPECT(KeepsErrorDetail(code));
	}
	for (const auto code : { ErrorCode::None, ErrorCode::Link,
			ErrorCode::Network, ErrorCode::NotFound, ErrorCode::Github,
			ErrorCode::TooLarge, ErrorCode::Busy, ErrorCode::Files,
			ErrorCode::Consent }) {
		EXPECT(!KeepsErrorDetail(code));
	}
	std::cout << "plugin consent logic ok\n";
	return 0;
}
'''

LOGIC_MUTANTS = (
    ('\tif (!all.has(Permission::Network)) {\n\t\treturn result;', '\tif (false) {\n\t\treturn result;'),
    ('&& (asked.has(source) || asked.has(Permission::Network))) {', '&& (true || asked.has(Permission::Network))) {'),
    ('|| asked.has(Permission::Network))) {', ')) {'),
    ('{ Permission::MessagesRead, Permission::TelegramUpdates });', '{ Permission::MessagesRead });'),
    ('if (std::find(old.begin(), old.end(), "*") != old.end()) {', 'if (false) {'),
    ('return char(std::tolower(static_cast<unsigned char>(c)));', 'return c;'),
    ('\tif (dash == std::string::npos) {\n\t\treturn nullptr;', '\tif (true) {\n\t\treturn nullptr;'),
    ('\tcase ErrorCode::RateLimited:\n\tcase ErrorCode::Conflict:', '\tcase ErrorCode::Conflict:'),
    ('\tcase ErrorCode::Manifest:\n', ''),
    ('\tdefault:\n\t\treturn false;', '\tdefault:\n\t\treturn true;'),
)


def logic_fixture(root, logic, name):
    tree = root / name
    header = tree / CONSENT_LOGIC
    header.parent.mkdir(parents=True)
    header.write_text(logic, encoding='utf-8', newline='\n')
    main = root / (name + '.cpp')
    main.write_text(LOGIC_MAIN, encoding='utf-8')
    exe = root / (name + ('.exe' if os.name == 'nt' else ''))
    run([CXX, '-std=c++20', '-O0', '-Wall', '-Wextra', '-Werror',
        '-I', str(tree), '-I', str(ROOT / 'Telegram/SourceFiles'),
        str(main), *STATIC, '-o', str(exe)], root)
    standalone(exe)
    result = subprocess.run(
        [str(exe)], cwd=root, capture_output=True, text=True, errors='replace')
    return result.returncode, result.stdout + result.stderr


def check_logic(root):
    logic = source('Telegram/SourceFiles/' + CONSENT_LOGIC)
    code, out = logic_fixture(root, logic, 'plugin_consent_logic')
    assert code == 0 and out.strip() == 'plugin consent logic ok', out
    for index, (before, after) in enumerate(LOGIC_MUTANTS):
        assert logic.count(before) == 1, f'logic mutant anchor moved: {before}'
        try:
            code, out = logic_fixture(
                root, logic.replace(before, after), f'plugin_consent_logic_m{index}')
        except AssertionError:
            continue  # Does not compile: rejected as well.
        assert code != 0, f'the consent logic fixture accepts the mutant: {after}'


def check(root):
    files = {path: source(path)
        for path in UI + (HEADER, LOGIC, MANAGER, HOST_API, EN, RU)}
    for test in CHECKS:
        test(files)
    for test, path, before, after in MUTANTS:
        assert before in files[path], f'mutant anchor moved: {before}'
        mutated = dict(files)
        mutated[path] = files[path].replace(before, after, 1)
        try:
            test(mutated)
        except AssertionError:
            continue
        raise AssertionError(f'{test.__name__} accepts the mutant: {after}')
    check_logic(root)
    print('Plugin UI PASS: strings match in EN and RU, the section is '
        'registered, dangerous permissions are highlighted, commitInstall '
        'goes through consent and Manager calls match the header; every '
        'ErrorCode is translated, consent warns about combined permissions '
        'and new hosts, an install in progress has Cancel, Update shows only '
        'with an update, the log is live and filtered, secrets are masked, '
        'rows update in place and Settings counts the updates')


if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='luxury-plugin-ui-') as directory:
        check(Path(directory))
