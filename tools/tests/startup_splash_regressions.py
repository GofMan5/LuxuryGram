"""Startup shows a splash with its load stages instead of nothing.

Application::run() kept the main thread busy for about 3.2 s of a Debug start
(fonts loaded at 06.5 s, "showing" at 09.2 s) with no window on screen. The
splash is created once the styles exist, is fed a stage before each slow step,
and is gone before launch_finished, so the test agent and the main window
never meet it. A start that goes straight to the tray or the taskbar shows
nothing, the same rule MainWindow::applyInitialWorkMode() applies.

Bounded source check; not a native acceptance.
"""
from pathlib import Path
import re
import tempfile
from library_regressions import function, source


APPLICATION = 'Telegram/SourceFiles/core/application.cpp'
SPLASH = 'Telegram/SourceFiles/luxury/ui/startup_splash.cpp'
MAIN_WINDOW = 'Telegram/SourceFiles/mainwindow.cpp'
LANG = 'Telegram/Resources/langs/lang.strings'
RUSSIAN = 'Telegram/SourceFiles/luxury/luxury_settings.cpp'


def ordered(text, needles, message):
    position = -1
    for needle in needles:
        found = text.find(needle, position + 1)
        assert found > position, f'{message}: {needle!r} is missing or out of order'
        position = found


def check(root):
    run = function(source(APPLICATION), 'void Application::run(')
    if 'LuxuryStartup::ShowSplash()' not in run:
        raise AssertionError('RED: startup shows no window until the main one is ready')
    ordered(run, (
        'style::StartManager(cScale());',
        'LuxuryStartup::ShowSplash()',
        'Ui::Emoji::Init();',
        '_lastActivePrimaryWindow->finishFirstShow();',
        'splash = nullptr;',
        'Test::Fire(u"launch_finished"_q);',
    ), 'the splash must exist only between the styles and launch_finished')
    stages = re.findall(r'stage\(tr::(luxury_Startup\w+), ([0-9.]+)\)', run)
    assert len(stages) == 4, f'expected four startup stages, found {stages}'
    progress = [float(value) for _, value in stages]
    assert progress == sorted(progress) and len(set(progress)) == 4, (
        f'startup progress must only move forward: {progress}')

    splash = source(SPLASH)
    hidden = function(splash, 'bool StartsHidden(')
    work_mode = function(source(MAIN_WINDOW), 'void MainWindow::applyInitialWorkMode(')
    for needle in ('cStartInTray()', 'LaunchModeAutoStart', 'cStartMinimized()'):
        assert needle in hidden and needle in work_mode, (
            f'the splash must skip the starts the main window hides: {needle}')
    assert 'if (StartsHidden())' in function(splash, 'std::unique_ptr<Splash> ShowSplash('), (
        'ShowSplash must return nothing for a hidden start')
    for needle in ('Qt::WA_ShowWithoutActivating', 'Qt::WA_QuitOnClose, false',
            'Qt::SplashScreen', 'flushWindowSystemEvents('):
        assert needle in splash, f'splash lost {needle}'
    assert not re.search(r'\b\d+(?:\.\d+)?\s*\*\s*style::DevicePixelRatio|QMargins\(\d', splash), (
        'splash geometry must come from luxury_styles.style')

    english = source(LANG)
    russian = source(RUSSIAN)
    for key, _ in stages:
        assert f'"{key}" = "' in english, f'{key} has no English text'
        assert f'{{ "{key}", "' in russian, f'{key} has no Russian text'
    print('Startup splash PASS: four ordered stages between the styles and '
        'launch_finished, hidden starts skipped, EN and RU texts present')


if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='luxury-startup-splash-') as directory:
        check(Path(directory))
