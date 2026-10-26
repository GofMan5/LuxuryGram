"""Source contracts for the 2026-10-25 optimisation audit fixes.

Each block names the defect it pins; logic with branches has its own compiled
check elsewhere.
"""
from library_regressions import ROOT, function, source

STREAMER = 'Telegram/SourceFiles/luxury/features/streamer_mode/platform/win/streamer_mode_win.cpp'
SRC = 'Telegram/SourceFiles/'


def check(root=None, read=source):
    # Streamer mode left the window capturable on Windows 10 before 2004,
    # which rejects WDA_EXCLUDEFROMCAPTURE.
    capture = function(read(STREAMER), 'void SetWindowCaptureExcluded(')
    assert 'if (!applied && excluded) {' in capture
    assert 'SetWindowDisplayAffinity(handle, WDA_MONITOR);' in capture

    # The width slider starts at 1.0; anything below came only from a
    # hand-edited config and widened nothing.
    settings = read(SRC + 'luxury/luxury_settings.cpp')
    assert 'validateRange(_wideMultiplier, 1.0, 4.0, defaults._wideMultiplier);' in settings

    # The watcher polls and prunes both tables per peer: full scans without these.
    database = read(SRC + 'luxury/data/luxury_database.cpp')
    assert 'make_index("idx_online_event_userId_dialogId_fakeId",' in database
    assert 'make_index("idx_watch_event_userId_dialogId_fakeId",' in database

    # Deleting a filter dropped it and its exclusions in two transactions.
    assert 'deleteFilter(' not in read(SRC + 'luxury/data/luxury_database.h')
    assert 'deleteExclusionsByFilterId' not in database
    filters_list = read(SRC + 'luxury/ui/settings/filters/settings_filters_list.cpp')
    assert 'LuxuryDatabase::applyFilterChanges(' in filters_list
    assert 'deleteFilter(' not in filters_list

    # Turning filters on at runtime built the cache on the painting thread.
    infra = read(SRC + 'luxury/luxury_infra.cpp')
    warm = infra.split('LuxuryDatabase::async([] {', 1)[1].split('\n\t});', 1)[0]
    assert warm.index('LuxuryDatabase::initialize();') < warm.index('FiltersCacheController::reloadNow();') \
        < warm.index('FiltersCacheController::fireUpdate();')
    assert 'warmFilters' not in infra

    # An import with many bad patterns stacked one toast per pattern.
    prepare = function(read(SRC + 'luxury/features/filters/filters_utils.cpp'),
                       'ApplyChanges FilterUtils::prepareChanges(')
    assert prepare.count('reject(tr::') == 2
    assert prepare.count('Ui::Toast::Show(') == 1
    assert prepare.index('Ui::Toast::Show(') > prepare.rindex('reject(tr::')

    # A failed theme download never finished loading, so apply() never ran;
    # emoji themes have no document at all.
    theme_state = function(read(SRC + 'luxury/features/message_shot/message_shot_theme_state.cpp'),
                           'void applyCloudThemeAsync(')
    assert 'DocumentLoadUpdates(session, document)' in theme_state
    assert 'documentView->loaded() || !document->loading()' in theme_state
    selector = read(SRC + 'luxury/ui/boxes/theme_selector_box.cpp')
    assert selector.index('if (!theme.documentId) {') < selector.index('data().document(theme.documentId)')

    # The selector outlived the content it was subscribed on.
    shot_box = read(SRC + 'luxury/ui/boxes/message_shot_box.cpp')
    closing = shot_box.split('box->boxClosing() | rpl::on_next(', 1)[1].split('_config.controller->show', 1)[0]
    assert 'box->lifetime());' in closing and 'content->lifetime()' not in closing
    selected = shot_box.split('box->themeSelected() | rpl::on_next(', 1)[1].split('box->lifetime());', 1)[0]
    assert selected.index('if (!weak) {') < selected.index('_savedThemeLifetime.destroy();')

    # Every reaction update resolved custom reaction documents with the watcher off.
    item = read(SRC + 'history/history_item.cpp')
    reaction = item.split('void HistoryItem::updateReactions(', 1)[1].split('bool HistoryItem::changeReactions', 1)[0]
    assert reaction.index('if (LuxuryOnline::TrackingAllowed()) {') < reaction.index('LuxuryOnline::noteReactions(')

    # Media list rebuilt the row vector once per selected item and per
    # remembered identity, with or without a screen reader.
    media = read(SRC + 'info/media/info_media_list_widget.cpp')
    selection = function(media, 'void ListWidget::accessibilitySelectionChanged(')
    assert selection.index('if (!Ui::ScreenReaderModeActive()) {') < selection.index('accessibilityItems()')
    assert 'accessibilityIndexOfItem' not in selection
    rows = function(media, 'void ListWidget::refreshRows(')
    assert 'present.contains(i->first)' in rows
    assert 'accessibilityIndexOfItem(i->first' not in rows

    # The embedded theme range followed a hard-coded number, not the enum.
    assert '<= int(Window::Theme::EmbeddedType::NightGreen));' in settings

    # Copies of helpers tdesktop already has, and fields nothing read.
    assert 'QDom' not in read(SRC + 'luxury/luxury_lang.h')
    assert 'dispatchToMainThread' not in read(SRC + 'luxury/utils/telegram_helpers.h')
    assert 'PassedData' not in read(SRC + 'luxury/features/translator/implementations/base.h')
    assert 'BlendARGB' not in read(SRC + 'luxury/ui/components/saved_music.cpp')
    assert 'FileSize(' not in read(SRC + 'luxury/features/forward/luxury_forward.cpp').replace('LuxurySync::fileSize(', '')
    watcher = read(SRC + 'luxury/ui/watcher/watcher_box.cpp')
    assert 'Blended(' not in watcher and '3.14159' not in watcher

    # The watcher read the oldest deleted rows of a chat, never the newest.
    assert 'LuxuryMessages::getDeletedMessagesForDialog(' in watcher
    assert 'horizon = std::max(horizon, MessageTimestamp(list->back()));' not in watcher
    assert 'oldest = std::min(oldest, MessageTimestamp(message));' in watcher
    newest = function(database, 'std::vector<DeletedMessage> getDeletedMessagesForDialog(')
    assert 'order_by(column<DeletedMessage>(&DeletedMessage::fakeId)).desc()' in newest

    # Dead code the P3 sweep removed; these came back by copy-paste before.
    lux = SRC + 'luxury/'
    assert 'SWITCH_BUTTON' not in read(SRC + 'history/history_widget.cpp')
    assert 'AddSettingToggle' not in read(lux + 'ui/settings/settings_luxury_utils.h')
    assert 'performDrag' not in read(lux + 'ui/message_history/history_inner.h')
    assert 'L"*.lnk"' in read(lux + 'utils/windows_utils.cpp')

    # Round 2.
    # The relaunch icon rewrote the taskbar property store on every counter change.
    win = read(SRC + 'platform/win/main_window_win.cpp')
    assert 'ico != _relaunchIcoPath' in win
    # The repost ripple painted with a colour nothing ever filled.
    assert '&cache->bg2' not in read(SRC + 'media/stories/media_stories_repost_view.cpp')
    # A 400 on a phone link resolved a random user id instead of failing.
    assert 'searchUserById' not in read(SRC + 'core/phone_click_handler.cpp')
    session = read(SRC + 'data/data_session.cpp')
    edit = session.split('void Session::updateEditedMessage(', 1)[1].split('\nvoid Session::', 1)[0]
    assert 'goto ' not in edit
    presence = function(session, 'void Session::resetTrackedPresence(')
    assert presence.index('LuxuryOnline::TrackingAllowed()') < presence.index('for ')
    helpers = read(SRC + 'luxury/utils/telegram_helpers.cpp')
    assert 'Expects(isMessageSavable(' not in helpers
    assert 'kServerSearchDelay = crl::time(1000)' in read(SRC + 'boxes/peers/edit_peer_requests_box.cpp')
    assert 'luxury_OnlineHistorySince' not in read('Telegram/Resources/langs/lang.strings')
    user = function(read(SRC + 'data/data_user.cpp'), 'bool UserData::isPremium(')
    assert 'isSelf() && LuxurySettings::getInstance().localPremium()' in user
    assert 'stuckPath' in function(read(SRC + 'core/file_utilities.cpp'), 'QString ReachableFolder(')
    assert 'GitHub Releases' not in read(SRC + 'boxes/about_box.cpp')
    assert '== 4) {' not in read(SRC + 'settings/sections/settings_notifications.cpp')
    assert '_giftToChannelDiscuss' not in read(SRC + 'history/history_widget.h')
    # The un-elevated relaunch link went into a folder update() had removed,
    # so the client came back elevated and UIPI dropped files dragged from Explorer.
    updater = read(SRC + '_other/updater_win.cpp')
    relaunch = updater.split('if (writeprotected) { // run un-elevated', 1)[1]
    assert relaunch.index('CreateDirectory(L"tupdates", 0);') < relaunch.index('ppf->Save(')
    # The luxury forward path copied the per-thread loop (bot topics, comment)
    # and missed upstream fixes to it.
    share = read(SRC + 'boxes/share_box.cpp')
    assert share.count('forum->reserveNewBotTopic()') == 1
    loop = share.split('const auto luxury = LuxuryForward::', 1)[1]
    assert loop.index('message.textWithTags = comment;') < loop.index('if (luxury) {') \
        < loop.index('api.forwardMessages(') < loop.index('const auto topicRootId')
    # The hover tooltip delay was a bare 350 pasted into ten widgets.
    sources = [p for p in (ROOT / SRC).rglob('*') if p.suffix in ('.cpp', '.mm')]
    assert len(sources) > 1000
    for path in sources:
        assert 'Tooltip::Show(350' not in path.read_text(encoding='utf-8', errors='ignore'), path
    assert 'luxuryTooltipDelay: 350;' in read(SRC + 'luxury/ui/luxury_styles.style')
    # The mac menu built the ghost mode title in two places.
    mac = read(SRC + 'platform/mac/global_menu_mac.mm')
    assert mac.count('tr::luxury_DisableGhostMode(tr::now)') == 1
    # Four static caches that a slider drag invalidated anyway, and the
    # small bubble option the luxury radius rewrite stopped reading.
    radius = read(SRC + 'ui/chat/chat_style_radius.cpp')
    assert 'cachedRadius' not in radius
    assert radius.count('UseSmallMsgBubbleRadius.value()') == 2
    # The 7.3.3 merge dropped a backslash from both patterns: C++ read "\?" as
    # "?", so luxurySettings?x captured "?x" and getpremium? matched nothing.
    urls = read(SRC + 'core/local_url_handlers.cpp')
    assert r'u"^(?:luxurySettings|exteraSettings)/?\\?(.+)$"_q' in urls
    assert r'u"^getpremium(/|\\?|#|$)"_q' in urls
    # 7.3.3 added a pure virtual to ElementDelegate and the luxury history
    # viewer stayed abstract: Linux CI failed on it, nothing here compiles.
    import re
    element = read(SRC + 'history/view/history_view_element.h')
    delegate = element[element.index('class ElementDelegate'):element.index('class DefaultElementDelegate')]
    pure = re.findall(r'(\w+)\([^;]*?\)\s*(?:const\s*)?= 0;', delegate, re.S)
    assert 'elementGramReadLine' in pure
    for header in ('luxury/ui/message_history/history_inner.h',
                   'history/admin_log/history_admin_log_inner.h',
                   'history/view/history_view_list_widget.h',
                   'history/history_inner_widget.cpp'):
        text = read(SRC + header)
        missing = [name for name in pure if not re.search(
            r'\b' + name + r'\([^;{]*\)\s*(?:const\s*)?override\b', text)]
        assert not missing, (header, missing)
    # One GetKeyIndex over 11.6k generated keys hit MSVC ARM64 C1053
    # "function too large". The generator splits it into parts of a
    # descending key range, each ending in "return kKeysCount;", and
    # dispatches by each part's smallest key, the last part unconditionally.
    generator = read('Telegram/codegen/codegen/lang/generator.cpp')
    split = generator[generator.index('constexpr auto kKeysPerPart'):generator.index('header_->popNamespace().newline();', generator.index('constexpr auto kKeysPerPart'))]
    per_part = int(re.search(r'constexpr auto kKeysPerPart = (\d+);', split).group(1))
    assert 'auto parts = std::vector<std::set<QString, std::greater<>>>();' in split
    assert 'for (const auto &key : keysSet) {' in split
    assert 'parts.back().size() == kKeysPerPart' in split
    assert 'writeSetSearch(parts[i], computeKey, "kKeysCount");' in split
    smallest = re.search(r'if \(key >= QLatin1String\(\\"" << \*parts\[i\]\.(r?begin)\(\) << "\\"\)\) \{', split)
    assert smallest and smallest.group(1) == 'rbegin', 'descending part: rbegin is its smallest key'
    assert 'if (i + 1 == partsCount) {\n\t\t\tsource_->stream() << "\\treturn GetKeyIndexPart" << i << "(key);\\n";' \
        in split.replace('\r\n', '\n')
    # Brace matching cannot read writeSetSearch: its string literals open
    # switches. Each part ends in the search's own final return.
    search = generator[generator.index('void Generator::writeSetSearch('):generator.index('QString Generator::getFullKey(')]
    assert search.replace('\r\n', '\n').rstrip().endswith('return " << invalidResult << ";\\n";\n}')
    # Simulate on the keys the generator emits: each plural expands to six.
    plurals = ('zero', 'one', 'two', 'few', 'many', 'other')
    keys = set()
    for key in re.findall(r'^"([^"]+)"\s*=', read('Telegram/Resources/langs/lang.strings'), re.M):
        keys.update(key.split('#')[0] + '#' + p for p in plurals) if '#' in key else keys.add(key)
    # QString orders by UTF-16 units, QLatin1String by bytes: equal on ASCII,
    # which ValidateKey enforces.
    keys = sorted(keys, reverse=True)
    assert len(keys) > 2 * per_part
    parts = [keys[i:i + per_part] for i in range(0, len(keys), per_part)]
    assert per_part <= 2000, 'MSVC ARM64 failed on 11.6k keys and passed on 8.7k in one function'
    for part, chunk in enumerate(parts):
        for key in chunk:
            routed = next((i for i, p in enumerate(parts[:-1]) if key >= p[-1]), len(parts) - 1)
            assert routed == part, (key, routed, part)
    print('Optimisation audit contracts PASS')


if __name__ == '__main__':
    check()
