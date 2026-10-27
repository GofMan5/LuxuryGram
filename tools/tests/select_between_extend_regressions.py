"""Select messages between extends a selected range from its nearest edge.

After Select messages between filled a range, selecting one more message
above or below it and choosing Select messages between again should select
everything between the range and that message. The limit check counted the
messages already selected inside the outermost pair as new ones, so with a
40-message range and a pick 30 messages below it the action vanished, though
the result (70) stays under the 100 limit.

Select up to this message shares the collectors. Its downward limit was one
short (Max - count + 0 against + 1 upward, though in both directions only the
nearest selected endpoint is already counted), so going down it refused a
range landing exactly on the limit.

Compiles the real HistoryInner collectBetween lambda and select-up-to block and
the real ListWidget::collectBetween()/selectionBetween()/selectionUpTo() over a
flat message list with item/view/album/selection doubles (album parts are
hidden in HistoryInner, which needs every part selected to skip an album);
the menu itself is not exercised. Built with -ftrapv, the INT_MAX case of No
Selection Limit traps on an overflow in Max - count + k.
Run: python tools/tests/select_between_extend_regressions.py
"""
from pathlib import Path
import re
import tempfile

from library_regressions import CXX, ROOT, function, run, source

INNER = 'Telegram/SourceFiles/history/history_inner_widget.cpp'
LIST = 'Telegram/SourceFiles/history/view/history_view_list_widget.cpp'

PROGRAM = r'''
#include <algorithm>
#include <cassert>
#include <compare>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <map>
#include <ranges>
#include <set>
#include <vector>
#include "gsl/pointers"
using gsl::not_null;
namespace ranges { using std::ranges::find; }
int Limit = 10;
int LuxuryMaxSelectedItems() { return Limit; }
using int64 = long long;
struct MsgId {
	long long bare = 0;
	friend auto operator<=>(const MsgId &, const MsgId &) = default;
};
struct FullMsgId {
	MsgId msg;
	friend auto operator<=>(const FullMsgId &, const FullMsgId &) = default;
};
struct HistoryItem {
	long long id = 0;
	bool selectable = true;
	FullMsgId fullId() const { return { { id } }; }
	bool canBeSelected() const { return selectable; }
};
using HistoryItemsList = std::vector<not_null<HistoryItem*>>;
struct Element {
	HistoryItem *item = nullptr;
	HistoryItem *data() const { return item; }
};
std::vector<HistoryItem> Items;
std::vector<Element> Views;
// Album leader id -> every part id, leader first.
std::map<long long, std::vector<long long>> Albums;
void Build(
		int count,
		std::set<long long> service,
		std::map<long long, std::vector<long long>> albums = {}) {
	Albums = albums;
	Items.assign(count, {});
	Views.assign(count, {});
	for (auto i = 0; i != count; ++i) {
		Items[i] = { i + 1, !service.contains(i + 1) };
		Views[i].item = &Items[i];
	}
}
HistoryItem *Message(long long id) { return &Items[id - 1]; }
const std::vector<long long> *AlbumOf(long long id) {
	for (const auto &[leader, parts] : Albums) {
		if (ranges::find(parts, id) != parts.end()) {
			return &parts;
		}
	}
	return nullptr;
}
struct Group { HistoryItemsList items; };
struct Groups {
	mutable Group group;
	Group *find(not_null<HistoryItem*> item) const {
		const auto parts = AlbumOf(item->id);
		if (!parts) {
			return nullptr;
		}
		group.items.clear();
		for (const auto id : *parts) {
			group.items.push_back(Message(id));
		}
		return &group;
	}
};
HistoryItem *groupLeaderOrSelf(HistoryItem *item) {
	const auto parts = AlbumOf(item->id);
	return parts ? Message(parts->front()) : item;
}

struct HistoryInner {
	using SelectedItems = std::set<HistoryItem*>;
	SelectedItems _selected;
	Element *viewByItem(HistoryItem *item) const {
		return &Views[item->id - 1];
	}
	// Album parts after the leader are hidden by the group.
	Element *nextItem(Element *view) const {
		for (auto index = view - Views.data() + 1;
				index < long(Views.size());
				++index) {
			const auto id = Views[index].item->id;
			if (groupLeaderOrSelf(Message(id))->id == id) {
				return &Views[index];
			}
		}
		return nullptr;
	}
	bool isSelectedAsGroup(SelectedItems *items, HistoryItem *item) const {
		if (const auto parts = AlbumOf(item->id)) {
			for (const auto id : *parts) {
				if (!items->contains(Message(id))) {
					return false;
				}
			}
			return true;
		}
		return items->contains(item);
	}
	// selectedState.count, fromItem and toItem as the menu computes them.
	HistoryItemsList between() {
		struct { int count; } selectedState{ int(_selected.size()) };
		auto fromItem = *_selected.begin();
		auto toItem = *_selected.rbegin();
		INNER_COLLECT
		const auto left = LEFT;
		auto result = collectBetween(fromItem, toItem, left);
		return (result.size() <= 2) ? HistoryItemsList() : result;
	}
	// The Select up to this message block, empty when not offered.
	HistoryItemsList upTo(HistoryItem *item) {
		struct { int count; } selectedState{ int(_selected.size()) };
		INNER_COLLECT
		UP_TO_BLOCK
		return collectBetween(start, end, left);
	}
};

struct Delegate {
	bool listIsItemGoodForSelection(not_null<HistoryItem*> item) {
		return item->selectable;
	}
};
struct SelectedMap : std::map<FullMsgId, bool> {
	const value_type &front() const { return *begin(); }
	const value_type &back() const { return *rbegin(); }
};
struct Data {
	Groups g;
	HistoryItem *message(FullMsgId id) const { return Message(id.msg.bare); }
	const Groups &groups() const { return g; }
};
struct Session { Data d; const Data &data() const { return d; } };
struct ListWidget {
	std::vector<not_null<Element*>> _items;
	SelectedMap _selected;
	Delegate delegate;
	Delegate *_delegate = &delegate;
	Session s;
	const Session &session() const { return s; }
	Element *viewForItem(const HistoryItem *item) const {
		return &Views[item->id - 1];
	}
	std::vector<not_null<HistoryItem*>> collectBetween(
		not_null<HistoryItem*> from,
		not_null<HistoryItem*> to,
		int max) const;
	std::vector<not_null<HistoryItem*>> selectionBetween() const;
	std::vector<not_null<HistoryItem*>> selectionUpTo(
		not_null<HistoryItem*> item) const;
};
LIST_COLLECT
LIST_SELECTION
LIST_UP_TO

std::vector<long long> Ids(const HistoryItemsList &list) {
	auto result = std::vector<long long>();
	for (const auto &item : list) {
		result.push_back(item->id);
	}
	std::sort(result.begin(), result.end());
	return result;
}
HistoryInner MakeInner(const std::vector<long long> &selected) {
	auto inner = HistoryInner();
	for (const auto id : selected) {
		inner._selected.insert(Message(id));
	}
	return inner;
}
ListWidget MakeList(const std::vector<long long> &selected) {
	auto list = ListWidget();
	for (auto &view : Views) {
		list._items.push_back(&view);
	}
	for (const auto id : selected) {
		list._selected.emplace(FullMsgId{ { id } }, true);
	}
	return list;
}
std::vector<long long> Inner(std::vector<long long> selected) {
	return Ids(MakeInner(selected).between());
}
std::vector<long long> List(std::vector<long long> selected) {
	const auto result = MakeList(selected).selectionBetween();
	return Ids((result.size() <= 2) ? HistoryItemsList() : result);
}
using Ids_ = std::vector<long long>;
void Check(Ids_ selected, Ids_ expected) {
	assert(Inner(selected) == expected);
	assert(List(selected) == expected);
}
// Select up to this message: what gets selected on top of `selected`.
void CheckUpTo(Ids_ selected, long long to, Ids_ expected) {
	assert(Ids(MakeInner(selected).upTo(Message(to))) == expected);
	assert(Ids(MakeList(selected).selectionUpTo(Message(to))) == expected);
}
int main() {
	Build(20, { 7 });
	// A fresh pair still fills the gap, bounds included.
	Check({ 3, 6 }, { 3, 4, 5, 6 });
	// Range 3..6 plus a pick below: extends from 6, skipping service 7.
	Check({ 3, 4, 5, 6, 12 }, { 3, 8, 9, 10, 11, 12 });
	// Exactly the limit: 5 selected + 5 new = 10.
	Check({ 3, 4, 5, 6, 13 }, { 3, 8, 9, 10, 11, 12, 13 });
	// One past the limit: nothing, the menu item stays hidden.
	Check({ 3, 4, 5, 6, 14 }, {});
	// A pick above the range extends from its top edge.
	Check({ 5, 10, 11, 12, 13 }, { 5, 6, 8, 9, 13 });
	Check({ 2, 10, 11, 12, 13 }, {});
	// A contiguous range has nothing between: no action.
	Check({ 3, 4, 5, 6 }, {});
	// Select up to this message, limit 10, 4 selected: 6 new fit.
	// Down from 6: exactly the limit is offered (it was one short).
	CheckUpTo({ 3, 4, 5, 6 }, 12, { 6, 8, 9, 10, 11, 12 });
	CheckUpTo({ 3, 4, 5, 6 }, 13, { 6, 8, 9, 10, 11, 12, 13 });
	CheckUpTo({ 3, 4, 5, 6 }, 14, {});
	// Up from 12: the same bound.
	CheckUpTo({ 12, 13, 14, 15 }, 5, { 5, 6, 8, 9, 10, 11, 12 });
	CheckUpTo({ 12, 13, 14, 15 }, 4, {});

	// No Selection Limit: the real INT_MAX, -ftrapv catches an overflow.
	Limit = std::numeric_limits<int>::max();
	Check({ 3, 6 }, { 3, 4, 5, 6 });
	Check({ 3, 4, 5, 6, 20 }, { 3, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20 });
	CheckUpTo({ 3 }, 20, { 3, 4, 5, 6, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20 });
	CheckUpTo({ 20 }, 1, { 1, 2, 3, 4, 5, 6, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20 });

	// Album 15-17 (leader 15). HistoryInner walks the leader only and
	// skips it only when every part is selected; ListWidget walks parts.
	Build(20, { 7 }, { { 15, { 15, 16, 17 } } });
	Limit = 20;
	// Leader selected, 16 and 17 not: the album is still new.
	assert(Inner({ 3, 4, 5, 6, 15, 19 })
		== (Ids_{ 3, 8, 9, 10, 11, 12, 13, 14, 15, 18, 19 }));
	assert(List({ 3, 4, 5, 6, 15, 19 })
		== (Ids_{ 3, 8, 9, 10, 11, 12, 13, 14, 16, 17, 18, 19 }));
	// The whole album selected: skipped by both.
	Check({ 3, 4, 5, 6, 15, 16, 17, 19 }, { 3, 8, 9, 10, 11, 12, 13, 14, 18, 19 });
	// The partial album leader counts: 11 collected fit 15 - 6 + 2 only.
	Limit = 15;
	assert(!Inner({ 3, 4, 5, 6, 15, 19 }).empty());
	Limit = 14;
	assert(Inner({ 3, 4, 5, 6, 15, 19 }).empty());
	std::puts("select between extend PASS: range grows from its nearest edge up to the limit, inclusive bounds, already selected not counted, partial albums counted, up-to exact limit both ways, INT_MAX without overflow; HistoryInner and ListWidget");
}
'''


def check(root, inner=None, listing=None):
    inner = inner if inner is not None else source(INNER).replace('\r\n', '\n')
    listing = listing if listing is not None else source(LIST).replace('\r\n', '\n')
    start = inner.index('\tconst auto collectBetween = [=](')
    collect = inner[start:inner.index('\n\t};\n', start) + 4]
    menu = inner[inner.index('// Select messages between two selected.'):]
    left = re.search(r'const auto left = (LuxuryMaxSelectedItems\(\)\s*- selectedState\.count\s*\+ 2);', menu)
    assert left, 'Select messages between no longer allows for its two selected ends'
    assert 'collectBetween(fromItem, toItem, left).size() <= 2' in menu
    up_menu = inner[inner.index('// Select up to this message.'):]
    up_start = up_menu.index('const auto toItem = groupLeaderOrSelf(item);')
    up_end = re.search(r'const auto left = LuxuryMaxSelectedItems\(\)[^;]*;', up_menu)
    assert up_end and 'collectBetween(start, end, left).empty()' in up_menu[up_end.end():up_end.end() + 120]
    up_to = up_menu[up_start:up_end.end()].replace('return;', 'return {};')
    program = (PROGRAM
        .replace('LEFT', left.group(1))
        .replace('UP_TO_BLOCK', up_to)
        .replace('LIST_UP_TO', function(listing, 'std::vector<not_null<HistoryItem*>> ListWidget::selectionUpTo('))
        .replace('INNER_COLLECT', collect)
        .replace('LIST_COLLECT', function(listing, 'std::vector<not_null<HistoryItem*>> ListWidget::collectBetween('))
        .replace('LIST_SELECTION', function(listing, 'std::vector<not_null<HistoryItem*>> ListWidget::selectionBetween()')))
    path = root / 'select_between.cpp'
    path.write_text(program, encoding='utf-8')
    exe = root / 'select_between.exe'
    run([CXX, '-std=c++20', '-O0', '-g', '-Wno-deprecated', '-ftrapv', '-I', str(ROOT / 'Telegram/ThirdParty/GSL/include'), str(path), '-o', str(exe)], root)
    print(run([str(exe)], root).strip())


if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='luxury-select-between-') as directory:
        check(Path(directory))
