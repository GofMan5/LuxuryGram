"""Authoritative group-call member reload through the actual prepareRows owner.

The full participant reload suppresses participantUpdated events, so prepareRows
must refresh the existing MembersRow state as well as its sounding index. This
is extracted C++20 production behavior with lightweight peer/list doubles, not
native Qt, rendering, audio, transport, or profile QA.
"""
from pathlib import Path
import os
import tempfile

from library_regressions import cpp, function, source


def check(root):
    members = (Path(os.environ['LUXURY_CALL_MEMBERS_SOURCE']).read_text(encoding='utf-8')
               if 'LUXURY_CALL_MEMBERS_SOURCE' in os.environ else source(
                   'Telegram/SourceFiles/calls/group/calls_group_members.cpp'))
    prepare = function(members, 'void Members::Controller::prepareRows(')
    prepare = prepare.replace('void Members::Controller::prepareRows(',
                              'void prepareRows(')
    cpp(root, 'calls_group_members_reload', r'''
#include <cassert>
#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

template <class T> using not_null = T;
using uint32 = std::uint32_t;

struct PeerData {
    int id = 0;
};

namespace Data {
struct GroupCallParticipant {
    PeerData *peer = nullptr;
    int state = 0;
};

class GroupCall {
public:
    PeerData *self = nullptr;
    std::vector<GroupCallParticipant> values;

    PeerData *joinAs() const { return self; }
    const std::vector<GroupCallParticipant> &participants() const { return values; }
    const GroupCallParticipant *participantByPeer(PeerData *peer) const {
        for (const auto &value : values) {
            if (value.peer == peer) {
                return &value;
            }
        }
        return nullptr;
    }
};
} // namespace Data

struct Row {
    enum class State { Active, Muted, Invited };
    PeerData *value = nullptr;
    State state = State::Active;
    PeerData *peer() const { return value; }
};

struct RowHandle {
    Row *value = nullptr;
    Row *get() const { return value; }
};

struct Delegate {
    std::vector<std::unique_ptr<Row>> rows;
    int refreshes = 0;
    int updates = 0;
    int peerListFullRowsCount() const { return int(rows.size()); }
    RowHandle peerListRowAt(int index) { return { rows[index].get() }; }
    void peerListAppendRow(std::unique_ptr<Row> row) { rows.push_back(std::move(row)); }
    void peerListRefreshRows() { ++refreshes; }
    void peerListUpdateRow(Row *) { ++updates; }
};

class Controller {
public:
    explicit Controller(Data::GroupCall *call, Delegate *delegate)
    : _call(call), _delegate(delegate) {}

    Delegate *delegate() const { return _delegate; }
    bool isMe(not_null<PeerData *> participantPeer) const {
        return (_call->joinAs() == participantPeer);
    }

    void removeRowFromSoundingMap(not_null<Row *>) {}
    void updateRowInSoundingMap(
            not_null<Row *>,
            bool,
            uint32,
            uint32,
            const Data::GroupCallParticipant *) {}
    void removeRow(not_null<Row *> row) {
        for (auto i = _delegate->rows.begin(); i != _delegate->rows.end(); ++i) {
            if (i->get() == row) {
                _delegate->rows.erase(i);
                return;
            }
        }
    }
    std::unique_ptr<Row> createRow(const Data::GroupCallParticipant &participant) {
        for (const auto &row : _delegate->rows) {
            if (row->peer() == participant.peer) {
                return nullptr;
            }
        }
        auto row = std::make_unique<Row>();
        row->value = participant.peer;
        row->state = participant.state == 1 ? Row::State::Muted : Row::State::Active;
        return row;
    }
    std::unique_ptr<Row> createRowForMe() {
        auto row = std::make_unique<Row>();
        row->value = _call->joinAs();
        return row;
    }
    bool appendWithAccessUsers() { return false; }
    bool appendInvitedUsers() { return false; }

    // This is the production state-refresh seam. The pre-fix owner never called it
    // for an existing row after FullReloaded suppressed participantUpdated events.
    void updateRow(
            not_null<Row *> row,
            const std::optional<Data::GroupCallParticipant> &,
            const Data::GroupCallParticipant *participant,
            Row::State noParticipantState = Row::State::Invited) {
        if (participant) {
            row->state = participant->state == 1
                ? Row::State::Muted
                : Row::State::Active;
            ++_delegate->updates;
        } else {
            row->state = noParticipantState;
        }
    }

    Data::GroupCall *_call = nullptr;
    Delegate *_delegate = nullptr;

    ''' + prepare + r'''
};

int main() {
    PeerData me{1};
    PeerData other{2};
    PeerData removed{3};
    Data::GroupCall call;
    call.self = &me;
    call.values = { { &me, 0 }, { &other, 0 } };
    Delegate delegate;
    auto meRow = std::make_unique<Row>();
    meRow->value = &me;
    meRow->state = Row::State::Active;
    delegate.rows.push_back(std::move(meRow));
    auto otherRow = std::make_unique<Row>();
    otherRow->value = &other;
    otherRow->state = Row::State::Muted;
    delegate.rows.push_back(std::move(otherRow));
    auto removedRow = std::make_unique<Row>();
    removedRow->value = &removed;
    removedRow->state = Row::State::Muted;
    delegate.rows.push_back(std::move(removedRow));

    Controller controller(&call, &delegate);
    controller.prepareRows(&call);
    assert(delegate.rows.size() == 2 && "authoritative reload removes stale member rows");
    assert(delegate.rows[1]->state == Row::State::Active
        && "authoritative reload must refresh an existing member row state");
    assert(delegate.updates == 2
        && "existing authoritative member rows must use the state owner once each");

    call.values[1].state = 1;
    controller.prepareRows(&call);
    assert(delegate.rows[1]->state == Row::State::Muted
        && "a later authoritative reload must apply muted state too");
    assert(delegate.updates == 4);
    std::cout << "Group-call members extracted production PASS: same-identity state replacement, muted replacement, stale-row removal; actual prepareRows owner with list/peer/model doubles, not native Qt/call/audio QA\n";
}
''', extra=())


if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='luxury-group-members-reload-') as directory:
        check(Path(directory))
