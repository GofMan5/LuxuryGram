"""Run with python tools/tests/watcher_contract.py.
Source-wiring checks, NOT an application compile or native visual test.
"""
from pathlib import Path
import re
import sqlite3

root = Path(__file__).resolve().parents[2]
src = root / 'Telegram/SourceFiles'
box = (src / 'luxury/ui/watcher/watcher_box.cpp').read_text('utf-8')
components = (src / 'luxury/ui/watcher/watcher_components.cpp').read_text('utf-8')
style = (src / 'luxury/ui/luxury_styles.style').read_text('utf-8')
item = (src / 'history/history_item.cpp').read_text('utf-8')
storage = (src / 'luxury/data/messages_storage.cpp').read_text('utf-8')
session = (src / 'data/data_session.cpp').read_text('utf-8')
database = (src / 'luxury/data/luxury_database.cpp').read_text('utf-8')
lang = (root / 'Telegram/Resources/langs/lang.strings').read_text('utf-8')
ru = (src / 'luxury/luxury_settings.cpp').read_text('utf-8')
button = (root / 'Telegram/lib_ui/ui/abstract_button.cpp').read_text('utf-8')

# The layout engine clamps to minResizeWidth; the default caused clipping.
for name in ('Summary', 'Value', 'Meta'):
    block = style.split('luxuryWatcher' + name + 'Label:', 1)[1].split('\n}', 1)[0]
    assert 'minWidth: 1px;' in block
assert 'kPreviewLimit' not in box and 'ElidedPreview' not in box
assert 'text.replace(' not in box
assert '_detailStyle.maxHeight = _expanded ? 0 : previewHeight;' in box
assert 'setSelectable(true)' in box
assert '_expand->setClickedCallback' in box
assert 'EventCard::~EventCard()' in box and 'delete detail.value;' in box
assert 'class ChoiceButton final : public Ui::LinkButton' in components
assert 'mousePressEvent' not in components
assert 'setFocusPolicy(Qt::StrongFocus)' in components
assert 'st::windowActiveTextFg->c' in components
assert 'latestByMessage' not in box and 'RevisionAfterStates(_edits)' in box
assert 'luxury_WatcherRevisionUnavailable' in box
assert 'luxury_WatcherNextSavedText' in box
assert 'recordedEdits.contains({ edited.messageId, at, edited.text })' in box
assert 'event.extra = afterText.toStdString();' in storage
assert 'LuxuryMessages::addEditedMessage(existing, afterText)' in session
edit = session.split('void Session::updateEditedMessage', 1)[1]
assert edit.index('LuxuryMessages::addEditedMessage(existing, afterText)') < edit.index('existing->applyEdition')
reaction = item.split('void HistoryItem::updateReactions(', 1)[1].split('bool HistoryItem::changeReactions', 1)[0]
assert '.left(200)' not in reaction and 'context.replace' not in reaction
assert 'if (!summary.isEmpty()) {\n\t\t\tLuxuryOnline::noteReactions' not in reaction
assert 'summary.isEmpty()' not in storage.split('void noteReactions(', 1)[1].split('std::vector<WatchEvent>', 1)[0]
assert 'u"custom"' not in reaction
assert '_eventLimit += kMoreRows;' in box and 'std::min(total, _eventLimit)' in box
assert 'showPeerHistory(peer, {}, MsgId(id))' in box
assert 'class WatcherView' not in box and 'WatcherView::' not in box
assert 'Ui::ScrollArea' not in box
assert 'box->setPinnedToTopContent(' in box and 'object_ptr<WatcherHeader>(box)' in box
assert 'object_ptr<WatcherBody>(box, peer)' in box
assert box.count('box->scrollToY(0);') == 2
assert 'if (weak && !over && !isDisabled())' in button

# Same-second watch records must stay in newest-insertion order.
query = database.split('std::vector<WatchEvent> getWatchEvents(', 1)[1].split('\nvoid ', 1)[0]
assert 'multi_order_by(' in query
assert 'order_by(column<WatchEvent>(&WatchEvent::fakeId)).desc()' in query
with sqlite3.connect(':memory:') as db:
    db.execute('CREATE TABLE WatchEvent(fakeId INTEGER PRIMARY KEY, at INTEGER)')
    db.executemany('INSERT INTO WatchEvent VALUES (?, ?)', [(1, 10), (2, 10), (3, 11)])
    assert db.execute('SELECT fakeId FROM WatchEvent ORDER BY at DESC, fakeId DESC').fetchall() == [(3,), (2,), (1,)]

keys = set(re.findall(r'tr::(luxury_Watcher\w+)', box + reaction))
for key in keys:
    assert f'"{key}" =' in lang, key
    assert f'{{ "{key}",' in ru, key
    english = re.search(r'"' + key + r'" = "(.*)";', lang).group(1)
    russian = re.search(r'\{ "' + key + r'", "(.*)" \}', ru).group(1)
    assert set(re.findall(r'\{\w+\}', english)) == set(re.findall(r'\{\w+\}', russian)), key
user = (src / 'data/data_user.cpp').read_text('utf-8')
updates = (src / 'api/api_updates.cpp').read_text('utf-8')
menu = (src / 'luxury/ui/context_menu/context_menu.cpp').read_text('utf-8')
assert 'result->updateServerLastseen(*status)' in session
assert 'user->updateServerLastseen(d.vstatus())' in updates
server = user.split('bool UserData::updateServerLastseen(', 1)[1].split('void UserData::resetTrackedPresence', 1)[0]
assert '_serverLastseen = LastseenFromMTP(status, _serverLastseen)' in server
assert '_trackedPresence.observe(' in server and 'LuxuryOnline::TrackingAllowed()' in server
assert 'LuxuryOnline' not in user.split('void UserData::madeAction(', 1)[1].split('\nvoid ', 1)[0]
assert 'trackOnlineHistoryChanges(' in session and 'passcodeLockChanges(' in session
assert 'noteServerLastseen' not in storage
clear = storage.split('void clearHistory(', 1)[1]
assert 'LastOffline.erase' in clear and 'resetTrackedPresence()' in clear
online = database.split('std::vector<OnlineEvent> getOnlineEvents(', 1)[1].split('\nvoid ', 1)[0]
assert 'order_by(column<OnlineEvent>(&OnlineEvent::fakeId)).desc()' in online
assert 'LuxuryDatabase::async([=]' in box and 'crl::on_main(' in box and 'if (!weak)' in box
assert 'SameRecordIds(_events, events)' in box
assert 'user->trackedOnlineStart() == session.start' in box
assert 'session.startId == _events.front().fakeId' in box
assert 'user->serverLastseen().isOnline' in box
assert '_expandedEvents.contains(key)' in box
# A focused row no longer freezes refresh forever: changed rows defer only
# while pressed, and clock ticks flush that rebuild after release.
assert 'QApplication::mouseButtons()' in box
assert 'const auto defer = changed && QApplication::mouseButtons();' in box
assert 'if (_rebuildPending && !QApplication::mouseButtons())' in box
assert 'state->clockTimer.callEach(1000)' in box
assert 'state->refreshTimer.callEach(kRefreshInterval)' in box
assert 'base::make_unique_q<Ui::Menu::Toggle>' in box
assert 'setter(!getter())' in box
assert 'itemPadding: margins(17px, 9px, 60px, 8px);' in style
assert 'itemToggleShift: 43px;' in style
assert 'body->rebuilds(' in box and 'box->scrollToY(state->scrollTop)' in box
assert 'luxury/features/watch/presence_history.h' in (root / 'Telegram/CMakeLists.txt').read_text('utf-8')
assert 'const auto exact = online || exactOffline;' in server
assert 'enabled && exact' in server  # Approximate/hidden presence opens a gap, not an invented logout.
assert 'if (watcherAvailable)' in menu and 'trackOnlineHistory = settings.trackOnlineHistory()' not in menu
assert updates.count('LuxuryOnline::noteProfileChange(') == 2
with sqlite3.connect(':memory:') as db:
    db.execute('CREATE TABLE OnlineEvent(fakeId INTEGER PRIMARY KEY, at INTEGER, online INTEGER)')
    db.executemany('INSERT INTO OnlineEvent VALUES (?, ?, ?)', [(1, 10, 1), (2, 10, 0), (3, 9, 1)])
    assert db.execute('SELECT fakeId FROM OnlineEvent ORDER BY fakeId DESC').fetchall() == [(3,), (2,), (1,)]
assert 'if (user->updateServerLastseen(data.vstatus()))' in (root / 'Telegram/SourceFiles/apiwrap.cpp').read_text('utf-8')
assert 'for (const auto &[index, account] : _domain->accounts())' in (root / 'Telegram/SourceFiles/core/application.cpp').read_text('utf-8')
assert 'session->updates().checkLastUpdate(adjusted);' in (root / 'Telegram/SourceFiles/core/application.cpp').read_text('utf-8')
assert 'void Session::resetTrackedPresence()' in session
assert 'session().data().resetTrackedPresence();' in updates
assert 'session->data().resetTrackedPresence();' in (root / 'Telegram/SourceFiles/main/main_account.cpp').read_text('utf-8')
assert 'uint64 _rebuildGeneration = 0;' in box
assert 'const auto generation = _rebuildGeneration;' in box
assert 'generation != _rebuildGeneration' in box
assert 'const auto weak = base::make_weak(this);' in box
assert 'if (!weak) {' in box
card = box.split('void SessionCard::setActive(',1)[1].split('void SessionCard::tick()',1)[0]
assert card.index('setToolTip(') < card.index('updateAccessibleName()')
tick = box.split('void SessionCard::tick()',1)[1].split('void SessionCard::computeLayout',1)[0]
assert tick.index('computeLayout(width())') < tick.index('updateAccessibleName()')
assert 'crl::on_main(this' in (root / 'Telegram/SourceFiles/luxury/ui/watcher/watcher_components.cpp').read_text('utf-8')
titles = box.split('void WatcherBody::rebuild()', 1)[1].split('_eventEmpty->show();', 1)[0]
assert titles.index('const auto counted = !_loading || !sessionList.empty();') < titles.index('_sessionsTitle->setText(counted') \
    < titles.index('QString::number(_sessions.size()))\n\t\t\t: tr::luxury_OnlineHistorySessions(tr::now));'.replace('\n', '\r\n' if '\r\n' in box else '\n')), \
    'a loading Sessions title must not claim zero'
events_title = titles.split('auto title = tr::luxury_OnlineHistoryEvents(tr::now);', 1)[1]
assert events_title.index('if (!_loading || !eventList.empty()) {') < events_title.index('QString::number(eventList.size())'), \
    'a loading Events title must not claim zero'
print('Watcher source contracts: PASS (layout, recording paths, privacy gates, async reads, live state, localization, SQL order, contacts callback, connection/sleep boundaries, reactive refresh, rebuild generations, callback guards)')
