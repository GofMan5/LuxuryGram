"""A filters update does not rebuild the settings page inside the click that fired it.

Lifting a ban from Filters -> Shadow Ban calls removeShadowBan() from the
row's own rowClicked(), and that fires the filters update. The settings wrap
rebuilt the open page right inside that call, deleting the list controller
whose rowClicked() then went on to read its delegate and session controller.

Bounded source check; not a native acceptance.
"""
from pathlib import Path
import tempfile
from library_regressions import function, source


LIST = 'Telegram/SourceFiles/luxury/ui/settings/filters/per_dialog_filter.cpp'
SETTINGS = 'Telegram/SourceFiles/luxury/luxury_settings.cpp'
CACHE = 'Telegram/SourceFiles/luxury/features/filters/filters_cache_controller.cpp'
WRAP = 'Telegram/SourceFiles/info/info_wrap_widget.cpp'


def premise():
    remove = function(source(SETTINGS), 'void LuxurySettings::removeShadowBan(')
    drop = function(source(CACHE), 'void dropResults(')
    assert 'dropResults()' in remove and 'fireUpdate()' in drop, (
        'removeShadowBan no longer fires the filters update')
    clicked = function(source(LIST), 'void PerDialogFiltersListController::rowClicked(')
    branch = clicked[clicked.index('if (_mode == Mode::ShadowBan)'):]
    branch = branch[:branch.index('} else if (_mode == Mode::Watched)')]
    after = branch[branch.index('removeShadowBan('):]
    assert 'delegate()' in after and '_controller->' in after, (
        'rowClicked no longer touches itself after removeShadowBan')


def check(root):
    premise()
    wrap = source(WRAP)
    handler = wrap[wrap.index('FiltersCacheController::updates()'):]
    handler = handler[:handler.index('}, _content->lifetime());')]
    rebuild = handler.index('showInternal(')
    queued = handler.find('crl::on_main(_content.data(),')
    if queued < 0 or queued > rebuild:
        raise AssertionError(
            'RED: a filters update rebuilds the settings page synchronously, '
            'deleting the shadow-ban list inside its own rowClicked()')
    assert handler.index('createMemento()') > queued, (
        'the page state must be taken when the rebuild runs, not when queued')
    print('Filters update PASS: the settings page is rebuilt after the click '
        'that fired the update returns, guarded by the content it replaces')


if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='luxury-shadow-ban-row-') as directory:
        check(Path(directory))
