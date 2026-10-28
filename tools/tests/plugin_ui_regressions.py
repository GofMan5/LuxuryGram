"""Plugin manager UI: strings, section registration, consent and Manager API.

The plugin UI (settings section, install, consent and details boxes) is
checked statically because the client is not built here:
- every lng_luxury_plugins_ui_ key used in code exists in English and Russian,
  with the same {tags}, and no unused key is left behind;
- the Plugins section is reachable from the LuxuryGram settings page;
- the consent box paints dangerous permissions in the attention color;
- commitInstall is reached only through the consent box (or an update that
  asks for no new permission);
- every Manager call in the UI names a method of plugin_manager.h.
Each check is also run against a mutant it must reject.

Bounded source check; not a native acceptance.
"""
from pathlib import Path
import re
import tempfile
from library_regressions import function, source


PREFIX = 'lng_luxury_plugins_ui_'
BOXES = 'Telegram/SourceFiles/luxury/ui/boxes/'
SECTION = 'Telegram/SourceFiles/luxury/ui/settings/settings_plugins.cpp'
MAIN = 'Telegram/SourceFiles/luxury/ui/settings/settings_main.cpp'
CONSENT = BOXES + 'plugin_consent_box.cpp'
INSTALL = BOXES + 'plugin_install_box.cpp'
DETAILS = BOXES + 'plugin_details_box.cpp'
UI = (SECTION, MAIN, CONSENT, INSTALL, DETAILS)
HEADER = 'Telegram/SourceFiles/luxury/plugins/plugin_manager.h'
EN = 'Telegram/Resources/langs/lang.strings'
RU = 'Telegram/SourceFiles/luxury/luxury_settings.cpp'


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
    assert 'manager->commitInstall(std::move(plan), [=](QString error) {' in commit, (
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


CHECKS = (check_strings, check_read_scope, check_section, check_danger, check_consent, check_api)

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
)


def check(root):
    files = {path: source(path) for path in UI + (HEADER, EN, RU)}
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
    print('Plugin UI PASS: strings match in EN and RU, the section is '
        'registered, dangerous permissions are highlighted, commitInstall '
        'goes through consent and Manager calls match the header')


if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='luxury-plugin-ui-') as directory:
        check(Path(directory))
