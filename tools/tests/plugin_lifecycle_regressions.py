"""Plugin lifecycle: install, update and recovery.

docs/plugins/ARCHITECTURE.md, recovery half. The client is not built here, so:
- plugin_install_logic.h is compiled with -Wall -Wextra -Werror against the
  real core units and checked: the .starting sentinel, the pending.json
  swap journal with its finish and rollback, the registry backup choice,
  case-insensitive repository identity, a replaced repository, downgrades,
  ref splits, rate limits, GitHub error texts, status codes, the update
  tally and the log mirror budget;
- the Qt half is checked statically: commitInstall and rollback swap
  through the journal and finish it only after the registry is saved,
  recover() runs every journal and disables a plugin whose start crashed,
  load() restores a broken registry from .bak and save() writes one,
  the engine start is fenced by the sentinel, -noplugins keeps plugins
  stopped, they start after the first window, repositories compare
  case-insensitively, downgrades need the flag and a failed check counts.
Every check is also run against a mutant it must reject.

Bounded source check; not a native acceptance.
"""
from pathlib import Path
import os
import subprocess
import tempfile
from library_regressions import CXX, ROOT, STATIC, function, run, source, standalone

SRC = 'Telegram/SourceFiles/'
PLUGINS = SRC + 'luxury/plugins/'
LOGIC = PLUGINS + 'plugin_install_logic.h'
MANAGER = PLUGINS + 'plugin_manager.cpp'
INSTALLER = PLUGINS + 'plugin_installer.cpp'
APP = SRC + 'core/application.cpp'
LAUNCHER = SRC + 'core/launcher.cpp'
SETTINGS = SRC + 'settings.h'
CORE = ('plugin_version', 'plugin_permissions', 'plugin_manifest', 'plugin_source', 'plugin_registry')

MAIN = r'''
#include "luxury/plugins/plugin_install_logic.h"
#include "luxury/plugins/core/plugin_registry.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>

using namespace Luxury::Plugins;
namespace fs = std::filesystem;

#define EXPECT(condition) do { if (!(condition)) { \
	std::cerr << "line " << __LINE__ << ": " #condition "\n"; std::abort(); } } while (0)

namespace {

void Write(const fs::path &path, const std::string &text) {
	fs::create_directories(path.parent_path());
	auto file = std::ofstream(path, std::ios::binary | std::ios::trunc);
	file << text;
}

std::string Read(const fs::path &path) {
	auto file = std::ifstream(path, std::ios::binary);
	auto stream = std::ostringstream();
	stream << file.rdbuf();
	return stream.str();
}

void Sentinel(const fs::path &work) {
	const auto dir = work / "sentinel";
	EXPECT(!TakeStarting(dir));
	MarkStarting(dir);
	EXPECT(fs::exists(dir / ".starting"));
	EXPECT(TakeStarting(dir));
	EXPECT(!fs::exists(dir / ".starting"));
	EXPECT(!TakeStarting(dir));
	MarkStarting(dir);
	ClearStarting(dir);
	EXPECT(!TakeStarting(dir));
}

void Swap(const fs::path &work) {
	const auto root = work / "swap";
	const auto target = root / "code";
	const auto journal = root / "pending.json";
	Write(target / "main.js", "old");
	Write(root / "staged" / "main.js", "new");

	EXPECT(SwapWithJournal(root / "staged", target, "v2", false).empty());
	EXPECT(fs::exists(journal));
	EXPECT(Read(target / "main.js") == "new");
	EXPECT(Read(root / "code.old" / "main.js") == "old");

	// The registry holds v2: the swap is finished at the next start.
	EXPECT(RecoverSwap(target, "v2") == SwapRecovery::Finished);
	EXPECT(!fs::exists(journal) && !fs::exists(root / "code.old"));
	EXPECT(Read(root / "code.prev" / "main.js") == "old");
	EXPECT(Read(target / "main.js") == "new");
	EXPECT(RecoverSwap(target, "v2") == SwapRecovery::None);

	// Died before the registry was written: the old files come back.
	Write(root / "staged" / "main.js", "newer");
	EXPECT(SwapWithJournal(root / "staged", target, "v3", false).empty());
	EXPECT(RecoverSwap(target, "v2") == SwapRecovery::RolledBack);
	EXPECT(Read(target / "main.js") == "new");
	EXPECT(!fs::exists(journal) && !fs::exists(root / "code.old"));

	// Normal path: FinishSwap after the registry is saved.
	Write(root / "staged" / "main.js", "newest");
	EXPECT(SwapWithJournal(root / "staged", target, "v4", false).empty());
	FinishSwap(target);
	EXPECT(!fs::exists(journal));
	EXPECT(Read(root / "code.prev" / "main.js") == "new");
	EXPECT(Read(target / "main.js") == "newest");

	// A fresh install that died unregistered is removed.
	const auto fresh = work / "fresh";
	Write(fresh / "staged" / "main.js", "x");
	EXPECT(SwapWithJournal(fresh / "staged", fresh / "code", "v1", true).empty());
	EXPECT(RecoverSwap(fresh / "code", "") == SwapRecovery::RolledBack);
	EXPECT(!fs::exists(fresh / "code") && !fs::exists(fresh / "pending.json"));

	// A failed swap leaves neither a journal nor moved files.
	EXPECT(!SwapWithJournal(root / "missing", target, "v5", false).empty());
	EXPECT(!fs::exists(journal) && Read(target / "main.js") == "newest");

	// A broken journal still rolls back.
	Write(root / "staged" / "main.js", "broken");
	EXPECT(SwapWithJournal(root / "staged", target, "v6", false).empty());
	Write(journal, "{not json");
	EXPECT(RecoverSwap(target, "v6") == SwapRecovery::RolledBack);
	EXPECT(Read(target / "main.js") == "newest");
}

void RegistryRecovery() {
	EXPECT(ChooseRegistry(false, false, false) == RegistryChoice::Empty);
	EXPECT(ChooseRegistry(false, false, true) == RegistryChoice::Empty);
	EXPECT(ChooseRegistry(true, true, false) == RegistryChoice::Main);
	EXPECT(ChooseRegistry(true, true, true) == RegistryChoice::Main);
	EXPECT(ChooseRegistry(true, false, true) == RegistryChoice::Backup);
	EXPECT(ChooseRegistry(true, false, false) == RegistryChoice::Empty);

	// What load() feeds it: a truncated main file and a whole backup.
	auto registry = Registry();
	auto plugin = InstalledPlugin();
	plugin.id = "autoreply";
	plugin.name = "Auto";
	plugin.version = "1.0.0";
	plugin.source.owner = "alice";
	plugin.source.repo = "autoreply";
	plugin.source.ref = "v1.0.0";
	registry.plugins.push_back(plugin);
	const auto whole = SerializeRegistry(registry);
	const auto main = ParseRegistry(whole.substr(0, whole.size() / 2));
	const auto backup = ParseRegistry(whole);
	EXPECT(!main && backup);
	EXPECT(ChooseRegistry(true, bool(main), bool(backup)) == RegistryChoice::Backup);
	EXPECT(backup->plugins.size() == 1 && backup->plugins[0].id == "autoreply");
}

void Identity() {
	EXPECT(SameName("Alice", "alice") && SameName("", ""));
	EXPECT(!SameName("alice", "alicE2") && !SameName("alice", "alicf"));
	EXPECT(SameRepo("Alice", "Repo", "alice", "repo"));
	EXPECT(SameRepo("ALICE", "my-Plugin.js", "alice", "MY-plugin.JS"));
	EXPECT(!SameRepo("alice", "repo", "alice", "repo2"));
	EXPECT(!SameRepo("bob", "repo", "alice", "repo"));

	EXPECT(RepoReplaced(1, 2));
	EXPECT(!RepoReplaced(1, 1) && !RepoReplaced(0, 2) && !RepoReplaced(1, 0));

	EXPECT(!AcceptUpdate(UpdateDecision::Downgrade, false));
	EXPECT(AcceptUpdate(UpdateDecision::Downgrade, true));
	EXPECT(AcceptUpdate(UpdateDecision::Available, false));
	EXPECT(AcceptUpdate(UpdateDecision::UpToDate, false));
	const auto stored = ResolvedRef{ Channel::Release, "v1.0.0", "" };
	EXPECT(!AcceptUpdate(DecideUpdate(stored, { Channel::Release, "v0.9.0", "" }), false));
	EXPECT(AcceptUpdate(DecideUpdate(stored, { Channel::Release, "v1.1.0", "" }), false));

	using Split = std::pair<std::string, std::string>;
	const auto splits = RefSplits("feature", "x/plugin");
	EXPECT(splits.size() == 3);
	EXPECT(splits[0] == Split("feature", "x/plugin"));
	EXPECT(splits[1] == Split("feature/x", "plugin"));
	EXPECT(splits[2] == Split("feature/x/plugin", ""));
	EXPECT(RefSplits("main", "").size() == 1);
	EXPECT(RefSplits("main", ".hidden/x").size() == 1);
}

void Github() {
	EXPECT(IsRateLimited(403, "0", ""));
	EXPECT(IsRateLimited(403, "", "30"));
	EXPECT(IsRateLimited(429, "", ""));
	EXPECT(!IsRateLimited(403, "", ""));
	EXPECT(!IsRateLimited(403, "12", ""));
	EXPECT(!IsRateLimited(404, "0", ""));

	EXPECT(RateLimitResetAt("2000", "", 1000) == 2000);
	EXPECT(RateLimitResetAt("2000", "30", 1000) == 1030);
	EXPECT(RateLimitResetAt("500", "", 1000) == 1060);
	EXPECT(RateLimitResetAt("x", "y", 1000) == 1060);
	EXPECT(RateLimitResetAt("", "", 1000) == 1060);

	EXPECT(GithubErrorText(403, R"({"message":"API rate limit exceeded"})")
		== "GitHub: API rate limit exceeded");
	EXPECT(GithubErrorText(502, "<html>Bad gateway</html>") == "GitHub: HTTP 502");
	EXPECT(GithubErrorText(500, "") == "GitHub: HTTP 500");
	EXPECT(GithubErrorText(404, R"({"message":""})") == "GitHub: HTTP 404");

	EXPECT(ClassifyStatus(0) == ErrorCode::Network);
	EXPECT(ClassifyStatus(-1) == ErrorCode::Network);
	EXPECT(ClassifyStatus(200) == ErrorCode::None);
	EXPECT(ClassifyStatus(404) == ErrorCode::NotFound);
	EXPECT(ClassifyStatus(422) == ErrorCode::NotFound);
	EXPECT(ClassifyStatus(429) == ErrorCode::RateLimited);
	EXPECT(ClassifyStatus(403) == ErrorCode::Github);
	EXPECT(ClassifyStatus(500) == ErrorCode::Github);

	EXPECT(ParseRepoId(R"({"id":4815162342,"name":"x"})") == 4815162342);
	EXPECT(!ParseRepoId(R"({"id":"7"})"));
	EXPECT(!ParseRepoId("[1]") && !ParseRepoId("nope"));
}

void Tally() {
	auto tally = UpdateTally();
	tally.add({}, ErrorCode::None, true);
	tally.add({}, ErrorCode::None, false);
	// A 403 that is not a rate limit: the text is there, the code is not.
	tally.add(GithubErrorText(403, "forbidden"), ClassifyStatus(403), false);
	tally.add("later", ErrorCode::RateLimited, false);
	EXPECT(tally.available == 1 && tally.failed == 2);
	EXPECT(tally.firstError == "GitHub: HTTP 403" && tally.firstCode == ErrorCode::Github);

	auto none = UpdateTally();
	none.add("offline", ErrorCode::None, false);
	EXPECT(none.failed == 1 && none.firstCode == ErrorCode::Github);
	auto limited = UpdateTally();
	limited.add("limit", ErrorCode::RateLimited, true);
	EXPECT(limited.failed == 1 && limited.available == 0 && limited.firstCode == ErrorCode::RateLimited);
}

void Budget() {
	const auto base = std::int64_t(1'761'523'200'000); // Epoch ms.
	auto budget = LogBudget();
	auto allowed = 0;
	for (auto i = 0; i != 150; ++i) {
		allowed += budget.allow(base + i) ? 1 : 0;
	}
	EXPECT(allowed == kLogMirrorLines);
	EXPECT(!budget.allow(base + kLogMirrorWindowMs - 1));
	EXPECT(budget.allow(base + kLogMirrorWindowMs));
	EXPECT(budget.count == 1);
	EXPECT(budget.allow(10)); // The clock went back: a new window.
}

} // namespace

int main(int argc, char **argv) {
	EXPECT(argc == 2);
	const auto work = fs::path(argv[1]);
	Sentinel(work);
	Swap(work);
	RegistryRecovery();
	Identity();
	Github();
	Tally();
	Budget();
	std::cout << "plugin lifecycle fixture ok";
}
'''

# Compile-time mutants of plugin_install_logic.h the fixture must reject.
LOGIC_MUTANTS = (
    ('if (lower(a[i]) != lower(b[i])) {', 'if (a[i] != b[i]) {'),
    ('return (decision != UpdateDecision::Downgrade) || allowDowngrade;', 'return true;'),
    ('|| (status == 403 && (remaining == "0" || !retryAfter.empty()));', '|| (status == 403);'),
    ('\t\t\tif (!failed++) {', '\t\t\tif (!failed) {'),
    ('return backupOk ? RegistryChoice::Backup : RegistryChoice::Empty;', 'return RegistryChoice::Empty;'),
    ('\tif (!mainExists) {\n\t\treturn RegistryChoice::Empty;', '\tif (false) {\n\t\treturn RegistryChoice::Empty;'),
    ('\tif (fs::exists(old, ec)) {\n\t\tfs::remove_all(target, ec);', '\tif (false) {\n\t\tfs::remove_all(target, ec);'),
    ('if (!ref.empty() && ref == registeredRef && fs::exists(target, ec)) {', 'if (fs::exists(target, ec)) {'),
    ('\treturn found;\n}', '\treturn false;\n}'),
    ('\t\tfile << text;\n\t\tfile.close();', '\t\tfile.close();'),
    ('\t\tfs::rename(old, prev, ec);', '\t\t(void)prev;'),
    ('return "GitHub: HTTP " + std::to_string(status);', 'return "GitHub: " + std::string(body);'),
    ('\t\treturn now + *seconds;', '\t\treturn *seconds;'),
    ('return ++count <= kLogMirrorLines;', 'return true;'),
    ('return storedId > 0 && resolvedId > 0 && storedId != resolvedId;', 'return storedId != resolvedId;'),
)


def build_core(root, read):
    tree = root / 'plugin_lifecycle'
    objects = []
    for unit in CORE:
        for ext in ('.h', '.cpp'):
            path = tree / 'luxury/plugins/core' / (unit + ext)
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(read(PLUGINS + 'core/' + unit + ext), encoding='utf-8', newline='\n')
        obj = tree / (unit + '.o')
        run([CXX, '-std=c++20', '-O0', '-c', *flags(tree), str(tree / 'luxury/plugins/core' / (unit + '.cpp')), '-o', str(obj)], root)
        objects.append(str(obj))
    return tree, objects


def flags(tree):
    # The copied tree comes first: core files are read through `read`.
    return ['-Wall', '-Wextra', '-Werror', '-I', str(tree), '-I', str(ROOT / SRC)]


def fixture(root, tree, objects, logic, name):
    (tree / 'luxury/plugins/plugin_install_logic.h').write_text(logic, encoding='utf-8', newline='\n')
    main = root / (name + '.cpp')
    main.write_text(MAIN, encoding='utf-8')
    exe = root / (name + ('.exe' if os.name == 'nt' else ''))
    run([CXX, '-std=c++20', '-O0', *flags(tree), str(main), *objects, *STATIC, '-o', str(exe)], root)
    standalone(exe)
    work = root / (name + '_fs')
    work.mkdir()
    result = subprocess.run([str(exe), str(work)], cwd=root, capture_output=True, text=True, errors='replace')
    return result.returncode, result.stdout + result.stderr


# --- Static checks of the Qt half.

def body(text, signature):
    return function(text, signature)


def ordered(text, *parts):
    at = -1
    for part in parts:
        found = text.find(part, at + 1)
        assert found > at, f'missing or out of order: {part}'
        at = found


def check_swap(files):
    text = files[MANAGER]
    commit = body(text, 'void Manager::commitInstall(')
    assert 'SwapDirectory(' not in commit, 'commitInstall swaps without a journal'
    ordered(commit, 'return SwapWithJournal(', 'if (d->save()) {\n\t\t\t\tFinishSwap(FsPath(target));')
    rollback = body(text, 'void Manager::rollback(')
    assert 'SwapDirectory(' not in rollback, 'rollback swaps without a journal'
    ordered(rollback, 'SwapWithJournal(', 'code + u".prev"_q', 'if (_private->save()) {\n\t\tFinishSwap(FsPath(code));')
    recover = body(text, 'bool Manager::Private::recover()')
    ordered(recover, 'RecoverSwap(', 'plugin ? plugin->source.ref : std::string()')
    load = body(text, 'void Manager::Private::load()')
    ordered(load, 'ChooseRegistry(', 'if (recover() || changed) {')


def check_quarantine(files):
    text = files[MANAGER]
    recover = body(text, 'bool Manager::Private::recover()')
    ordered(recover,
            'if (TakeStarting(FsPath(PluginRoot(id))) && plugin->enabled) {',
            'plugin->enabled = false;',
            'plugin->lastError = tr::lng_luxury_plugins_quarantined(',
            'changed = true;')
    start = body(text, 'void Manager::Private::start(')
    ordered(start, 'cNoPlugins()', 'MarkStarting(directory);', 'if (!raw->start()) {',
            'ClearStarting(directory);', 'crl::on_main(this, [=] {\n\t\tClearStarting(directory);')
    start_all = body(text, 'void Manager::startAll()')
    ordered(start_all, 'if (cNoPlugins()) {', 'return;', '_private->start(')
    ctor = body(text, 'Manager::Manager()')
    assert 'start(' not in ctor.replace('_private->load();', ''), 'the manager starts plugins before the window'
    app = files[APP]
    ordered(app, '_private->plugins = std::make_unique<Luxury::Plugins::Manager>();',
            '_lastActivePrimaryWindow->finishFirstShow();', 'splash = nullptr;',
            '_private->plugins->startAll();')
    ordered(files[LAUNCHER], '{ "-noplugins"', 'gNoPlugins = parseResult.contains("-noplugins");')
    assert 'DeclareSetting(bool, NoPlugins);' in files[SETTINGS]


def check_registry(files):
    text = files[MANAGER]
    load = body(text, 'void Manager::Private::load()')
    backup = load[load.index('case RegistryChoice::Backup:'):load.index('case RegistryChoice::Empty:')]
    ordered(backup, 'registry = std::move(*restored);', 'changed = true;', 'break;')
    ordered(load, 'auto restored = (exists && !primary && QFile::exists(backup))', 'read(backup)')
    ordered(load, 'if (exists && !primary) {', 'QFile::rename(path, path + u".bad"_q);')
    save = body(text, 'bool Manager::Private::save()')
    ordered(save, 'QFile::copy(path, backup);', 'WriteFile(')


def check_identity(files):
    text = files[MANAGER]
    installed_from = body(text, 'InstalledPlugin *Manager::Private::installedFrom(')
    assert 'SameRepo(owner, repo, plugin.source.owner, plugin.source.repo)' in installed_from
    assert 'owner ==' not in installed_from and 'repo ==' not in installed_from
    commit = body(text, 'void Manager::commitInstall(')
    ordered(commit, '!SameRepo(', '} else if (other && other != installed) {',
            'RepoReplaced(installed->source.repoId, plan.repoId)')
    plan = body(text, 'void Manager::Private::buildPlan(')
    ordered(plan, '!SameRepo(', '} else if (other && other != installed) {', 'RepoReplaced(storedId, identity.repoId)')


def check_downgrade(files):
    text = files[MANAGER]
    commit = body(text, 'void Manager::commitInstall(')
    ordered(commit, '&& !AcceptUpdate(\n\t\t\tDecideUpdate(StoredRef(installed->source), plan.resolved),\n'
            '\t\t\tplan.allowDowngrade)) {', 'done(tr::lng_luxury_plugins_downgrade(tr::now));\n\t\treturn;',
            'return SwapWithJournal(')
    plan = body(text, 'void Manager::Private::buildPlan(')
    ordered(plan, 'if (plan.downgrade && !updating.isEmpty()) {', 'Failed(ErrorCode::Downgrade)', 'return;')


def check_updates(files):
    text = files[MANAGER]
    check = body(text, 'void Manager::checkUpdatesDetailed(')
    ordered(check, 'waiters.push_back(std::move(done));', 'if (waiters.size() > 1) {', 'return;')
    ordered(check, 'if (!latest.error.isEmpty()) {', 'state->tally.add(\n\t\t\t\t\tlatest.error.toStdString(),',
            'latest.code,', '} else {', 'state->tally.add({}, ErrorCode::None, found);',
            'if (!--state->left) {')
    ordered(check, '.failed = tally.failed,', '.firstError = Text(tally.firstError),')
    assert 'if (plugin.pinned) {' in check, 'pinned plugins are checked'
    installer = files[INSTALLER]
    ordered(installer, 'request.setRawHeader("If-None-Match"', '} else if (IsRateLimited(',
            'View(header("X-RateLimit-Remaining")),', 'RateLimitResetAt(',
            'tr::lng_luxury_plugins_rate_limited(', 'const auto etag = header("ETag");')
    assert 'GithubErrorText(status, View(body))' in installer


CHECKS = (check_swap, check_quarantine, check_registry, check_identity, check_downgrade, check_updates)
MUTANTS = (
    # A swap without a journal.
    (check_swap, MANAGER, 'return SwapWithJournal(\n\t\t\t\t\t\tFsPath(staging', 'return SwapDirectory(\n\t\t\t\t\t\tFsPath(staging'),
    (check_swap, MANAGER, '\t\t\tif (d->save()) {\n\t\t\t\tFinishSwap(FsPath(target));', '\t\t\tFinishSwap(FsPath(target));\n\t\t\tif (d->save()) {'),
    (check_swap, MANAGER, '\tif (_private->save()) {\n\t\tFinishSwap(FsPath(code));', '\tFinishSwap(FsPath(code));\n\tif (_private->save()) {'),
    (check_swap, MANAGER, 'plugin ? plugin->source.ref : std::string()', 'std::string()'),
    # A sentinel that never disables the plugin.
    (check_quarantine, MANAGER, '\t\t\tplugin->enabled = false;\n\t\t\tplugin->lastError = tr::lng_luxury_plugins_quarantined(',
     '\t\t\tplugin->lastError = tr::lng_luxury_plugins_quarantined('),
    (check_quarantine, MANAGER, '\tMarkStarting(directory);\n\tif (!raw->start()) {', '\tif (!raw->start()) {'),
    (check_quarantine, MANAGER, '\tif (cNoPlugins()) {\n\t\tLOG(("Luxury Plugins: not started', '\tif (false) {\n\t\tLOG(("Luxury Plugins: not started'),
    (check_quarantine, APP, '\t_private->plugins->startAll();\n', ''),
    (check_quarantine, LAUNCHER, 'gNoPlugins = parseResult.contains("-noplugins");', 'gNoPlugins = false;'),
    # Mixed-case Owner/Repo creating a duplicate.
    (check_identity, MANAGER, 'if (SameRepo(owner, repo, plugin.source.owner, plugin.source.repo)',
     'if (owner == plugin.source.owner && repo == plugin.source.repo'),
    (check_identity, MANAGER, '\t} else if (installed\n\t\t&& RepoReplaced(installed->source.repoId, plan.repoId)) {',
     '\t} else if (false) {'),
    # A downgrade accepted without the flag.
    (check_downgrade, MANAGER, '&& !AcceptUpdate(\n\t\t\tDecideUpdate(StoredRef(installed->source), plan.resolved),\n\t\t\tplan.allowDowngrade)) {',
     '&& !AcceptUpdate(\n\t\t\tDecideUpdate(StoredRef(installed->source), plan.resolved),\n\t\t\ttrue)) {'),
    (check_downgrade, MANAGER, 'if (plan.downgrade && !updating.isEmpty()) {', 'if (false) {'),
    # checkUpdates reporting failed=0 on a 403.
    (check_updates, MANAGER, '\t\t\t\tstate->tally.add(\n\t\t\t\t\tlatest.error.toStdString(),\n\t\t\t\t\tlatest.code,\n\t\t\t\t\tfalse);\n', ''),
    (check_updates, MANAGER, '.failed = tally.failed,', '.failed = 0,'),
    (check_updates, MANAGER, '\tif (waiters.size() > 1) {', '\tif (false) {'),
    (check_updates, INSTALLER, '} else if (IsRateLimited(', '} else if (false && IsRateLimited('),
    (check_updates, INSTALLER, 'request.setRawHeader("If-None-Match"', '(void)("If-None-Match"'),
    # A corrupt registry not restored from .bak.
    (check_registry, MANAGER, '\t\tregistry = std::move(*restored);\n', ''),
    (check_registry, MANAGER, '\t\tQFile::copy(path, backup);\n', ''),
)


def crlf(text):
    return text.replace('\r\n', '\n')


def check(root, read=source):
    files = {path: crlf(read(path)) for path in (MANAGER, INSTALLER, APP, LAUNCHER, SETTINGS)}
    for test in CHECKS:
        test(files)
    for test, path, before, after in MUTANTS:
        assert before in files[path], f'mutant anchor moved: {before}'
        mutated = dict(files)
        mutated[path] = files[path].replace(before, after, 1)
        try:
            test(mutated)
        except (AssertionError, ValueError):
            continue
        raise AssertionError(f'{test.__name__} accepts the mutant: {after}')

    tree, objects = build_core(root, read)
    logic = crlf(read(LOGIC))
    code, out = fixture(root, tree, objects, logic, 'plugin_lifecycle')
    assert code == 0 and out.strip() == 'plugin lifecycle fixture ok', out
    for index, (before, after) in enumerate(LOGIC_MUTANTS):
        assert logic.count(before) == 1, f'logic mutant anchor moved: {before}'
        try:
            code, out = fixture(root, tree, objects, logic.replace(before, after), f'plugin_lifecycle_m{index}')
        except AssertionError:
            continue  # Did not compile: rejected as well.
        if code == 0:
            raise AssertionError(f'the lifecycle fixture accepts the mutant: {after}')
    print('Plugin lifecycle PASS: .starting quarantine, pending.json swap journal with finish and '
          'rollback, registry.json.bak restore, case-insensitive repository identity, replaced '
          'repository, downgrade flag, ref splits, rate limits, GitHub error texts, update tally, '
          'log mirror budget; -noplugins, start after the first window, ETag')


if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='luxury-plugin-lifecycle-') as directory:
        check(Path(directory))
