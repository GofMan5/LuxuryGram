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
assert 'if (weak && !over)' in button

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
print('Watcher source contracts: PASS (layout wiring, edit capture, reactions, controls, localization, SQL order)')
