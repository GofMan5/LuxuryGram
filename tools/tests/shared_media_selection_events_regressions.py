"""Shared-media owning selection flow, compiled from production method bodies.

Real ChangeItemSelection, flat containers and rpl; layout/provider/queue/lifetime
and QAccessible transport doubles. Not a native Qt TU, UIA/Narrator or render test.
"""
from pathlib import Path
import os
import tempfile

from library_regressions import ROOT, cpp, function, source


def check(root):
    text = (Path(os.environ['LUXURY_MEDIA_SELECTION_SOURCE']).read_text(encoding='utf-8')
            if 'LUXURY_MEDIA_SELECTION_SOURCE' in os.environ else source(
                'Telegram/SourceFiles/info/media/info_media_list_widget.cpp'))
    common = source('Telegram/SourceFiles/info/media/info_media_common.cpp')
    header = source('Telegram/SourceFiles/info/media/info_media_common.h')
    rp = source('Telegram/lib_ui/ui/rp_widget.cpp')
    signatures = (
        'void ListWidget::pushSelectedItems()',
        'bool ListWidget::hasSelected() const',
        'bool ListWidget::isSelectedItem(',
        'void ListWidget::removeItemSelection(',
        'bool ListWidget::hasSelectedText() const',
        'bool ListWidget::hasSelectedItems() const',
        'void ListWidget::applyItemSelection(',
        'void ListWidget::toggleItemSelection(',
        'void ListWidget::clearSelected()',
        'int ListWidget::accessibilityChildCount() const',
        'std::vector<not_null<BaseLayout*>> ListWidget::accessibilityItems() const',
        'BaseLayout *ListWidget::accessibilityLayoutAt(',
        'HistoryItem *ListWidget::accessibilityItemAt(',
        'int ListWidget::accessibilityIndexOfItem(',
        'QAccessible::State ListWidget::accessibilityChildState(',
        'quintptr ListWidget::accessibilityChildIdentity(',
        'int ListWidget::accessibilityChildIndexByIdentity(',
    )
    methods = '\n'.join(function(text, s) for s in signatures)
    optional = 'void ListWidget::accessibilitySelectionChanged()'
    if optional in text:
        methods += '\n' + function(text, optional)
    cpp(root, 'shared_media_selection_events', r'''
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <iterator>
#include <memory>
#include <ranges>
#include <vector>
#include <gsl/gsl>
#include <base/flat_map.h>
#include <base/flat_set.h>
#include <rpl/event_stream.h>
namespace base::assertion { void log(const char*,const char*,int){std::abort();} }
using gsl::not_null;
using quintptr=uintptr_t;
namespace ranges { using std::ranges::find; }
struct HistoryItem { int id=0; };
struct TextSelection { int from=0,to=0; friend bool operator==(TextSelection,TextSelection)=default; };
constexpr TextSelection FullSelection{0,65535};
constexpr int MaxSelectedItems=100;
''' + function(header, 'struct ListItemSelectionData') + r''';
using ListSelectedMap=base::flat_map<not_null<const HistoryItem*>,ListItemSelectionData,std::less<>>;
''' + function(common, 'bool ChangeItemSelection(') + r'''
struct RpWidget;
namespace QAccessible {
struct State { bool focusable=false,selectable=false,focused=false,selected=false; };
enum EventType { StateChanged,SelectionAdd,SelectionRemove };
}
struct QAccessibleEvent {
 RpWidget*owner=nullptr;int child=-1;QAccessible::EventType type;
 QAccessibleEvent(RpWidget*w,QAccessible::EventType t):owner(w),type(t){}
 virtual ~QAccessibleEvent()=default;void setChild(int i){child=i;}
};
struct QAccessibleStateChangeEvent:QAccessibleEvent {
 QAccessible::State fields;
 QAccessibleStateChangeEvent(RpWidget*w,QAccessible::State f):QAccessibleEvent(w,QAccessible::StateChanged),fields(f){}
};
struct AccessibilityState { bool selected=false;void writeTo(QAccessible::State&s)const{s.selected=selected;} };
struct RpWidget {
 virtual ~RpWidget()=default;virtual QAccessible::State accessibilityChildState(int)const=0;
 void accessibilityChildStateChanged(int,AccessibilityState);
 void accessibilityChildSelectionChanged(int);
};
namespace Platform { constexpr bool IsLinux(){return false;} }
struct Notice { int id=0;bool selected=false; };
std::vector<Notice> notices;
std::function<void()> eventHook;
namespace QAccessible { void updateAccessibility(QAccessibleEvent*e); }
''' + function(rp, 'void RpWidget::accessibilityChildStateChanged(') + '\n' + function(rp, 'void RpWidget::accessibilityChildSelectionChanged(') + r'''
struct BaseLayout { HistoryItem*item=nullptr;HistoryItem*getItem()const{return item;} };
struct Section { std::vector<not_null<BaseLayout*>> rows;const auto&items()const{return rows;} };
struct Provider { ListItemSelectionData computeSelectionData(const HistoryItem*,TextSelection s){return ListItemSelectionData(s);} };
namespace base {
template<class T>struct Weak { T*p=nullptr;std::weak_ptr<int> token;explicit operator bool()const{return !token.expired();}T*operator->()const{assert(*this);return p;} };
template<class T>Weak<T> make_weak(T*p){return {p,p->alive};}
}
std::vector<std::function<void()>> queue;
namespace crl { template<class T,class F>void on_main(T*p,F f){auto weak=base::make_weak(p);queue.push_back([=]{if(weak)f();});} }
void drain(){while(!queue.empty()){auto tasks=std::exchange(queue,{});for(auto&task:tasks)task();}}
struct ListWidget:RpWidget {
 using SelectedMap=ListSelectedMap;
 std::shared_ptr<int> alive=std::make_shared<int>(1);
 Provider provider;Provider*_provider=&provider;
 SelectedMap _selected;
 int _selectedLimit=MaxSelectedItems;
 int _accessibilityFocusedIndex=-1;HistoryItem*_accessibilityFocusedItem=nullptr;
 mutable base::flat_map<not_null<const HistoryItem*>,quintptr> _accessibilityIdentities;
 mutable quintptr _accessibilityIdentityCounter=0;
 base::flat_set<quintptr> _accessibilitySelectedIdentities;
 bool _accessibilitySelectionUpdateScheduled=false;
 std::vector<Section> _sections;
 rpl::event_stream<std::vector<int>> _selectedListStream;
 void update(){}void repaintItem(const HistoryItem*){}
 std::vector<int>collectSelectedItems()const{std::vector<int>r;for(const auto&[item,data]:_selected)r.push_back(item->id);return r;}
 void pushSelectedItems();bool hasSelected()const;bool isSelectedItem(const SelectedMap::const_iterator&)const;
 void removeItemSelection(const SelectedMap::const_iterator&);bool hasSelectedText()const;bool hasSelectedItems()const;
 void applyItemSelection(HistoryItem*,TextSelection);void toggleItemSelection(not_null<HistoryItem*>);void clearSelected();
 int accessibilityChildCount()const;std::vector<not_null<BaseLayout*>>accessibilityItems()const;
 BaseLayout*accessibilityLayoutAt(int)const;HistoryItem*accessibilityItemAt(int)const;int accessibilityIndexOfItem(const HistoryItem*)const;
 QAccessible::State accessibilityChildState(int)const override;
 quintptr accessibilityChildIdentity(int)const;int accessibilityChildIndexByIdentity(quintptr)const;
 void accessibilitySelectionChanged();
};
void QAccessible::updateAccessibility(QAccessibleEvent*e){
 auto owner=static_cast<ListWidget*>(e->owner);auto item=owner->accessibilityItemAt(e->child);assert(item);
 if(e->type==StateChanged){auto event=dynamic_cast<QAccessibleStateChangeEvent*>(e);assert(event&&event->fields.selected);}
 notices.push_back({item->id,owner->accessibilityChildState(e->child).selected});
 if(eventHook){auto callback=std::exchange(eventHook,{});callback();}
}
''' + methods + r'''
int main(){
 HistoryItem item{1};BaseLayout layout{&item};ListWidget owner;owner._sections={Section{{&layout}}};
 owner.applyItemSelection(&item,FullSelection);drain();
 assert(notices.size()==1&&notices[0].id==1&&notices[0].selected&&"mouse selection must announce once through the owning model");
 notices.clear();owner.clearSelected();drain();
 assert(notices.size()==1&&notices[0].id==1&&!notices[0].selected&&"clearing selection must announce deselection");
 notices.clear();owner.applyItemSelection(&item,TextSelection{2,4});drain();assert(notices.size()==1&&notices[0].selected);
  notices.clear();owner.clearSelected();drain();
  assert(notices.size()==1&&!notices[0].selected&&"clearing text selection must also announce deselection");
  notices.clear();owner.applyItemSelection(&item,FullSelection);owner.clearSelected();drain();
  assert(notices.empty()&&"coalesced selection changes must not announce a transient state");
  notices.clear();owner.applyItemSelection(&item,FullSelection);drain();
  assert(notices.size()==1&&notices[0].selected);
  notices.clear();owner.clearSelected();drain();
  assert(notices.size()==1&&!notices[0].selected);
   HistoryItem second{2};BaseLayout secondLayout{&second};owner._sections={Section{{&layout,&secondLayout}}};
  notices.clear();owner.applyItemSelection(&item,FullSelection);owner.applyItemSelection(&second,FullSelection);drain();
  std::ranges::sort(notices,{},&Notice::id);
  assert(notices.size()==2&&notices[0].id==1&&notices[1].id==2&&notices[0].selected&&notices[1].selected);
  notices.clear();owner.removeItemSelection(owner._selected.find(&item));drain();
  assert(notices.size()==1&&notices[0].id==1&&!notices[0].selected);
  notices.clear();owner.clearSelected();drain();
  assert(notices.size()==1&&notices[0].id==2&&!notices[0].selected);
    HistoryItem third{3};HistoryItem fourth{4};BaseLayout thirdLayout{&third};BaseLayout fourthLayout{&fourth};auto reentrant=std::make_unique<ListWidget>();reentrant->_sections={Section{{&thirdLayout,&fourthLayout}}};
  notices.clear();eventHook=[&]{reentrant.reset();};reentrant->applyItemSelection(&third,FullSelection);reentrant->applyItemSelection(&fourth,FullSelection);drain();
  assert(notices.size()==1&&(notices[0].id==3||notices[0].id==4)&&notices[0].selected&&"reentrant destruction must stop delivery safely");
  std::cout<<"Shared-media extracted production PASS: select/clear through real ChangeItemSelection + owner + RpWidget event routing; Qt/queue/provider doubles, NOT native UIA/render acceptance\n";
}
''', extra=('-I'+str(ROOT/'Telegram/lib_rpl'),
            '-I'+str(ROOT/'Telegram/lib_base'),
            '-I'+str(ROOT/'Telegram/ThirdParty/GSL/include')))


if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='luxury-media-selection-') as directory:
        check(Path(directory))
