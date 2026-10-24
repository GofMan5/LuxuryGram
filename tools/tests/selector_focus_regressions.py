"""Actual SearchWithGroups bookmark/steal/return + Inner scoped hook + owner afterShown.
Explicit QWidget weak/focus doubles, no native Qt focus chain/search/UIA.
"""
from pathlib import Path
from library_regressions import cpp, function, source


def check(root):
    search = source('Telegram/SourceFiles/ui/controls/tabbed_search.cpp')
    selector = source('Telegram/SourceFiles/chat_helpers/tabbed_selector.cpp')
    sm = '\n'.join(function(search, sig) for sig in ('void SearchWithGroups::stealFocus()', 'void SearchWithGroups::returnFocus()'))
    exists = 'void TabbedSelector::Inner::afterShownWithSearchFocus(' in selector
    inner = function(selector, 'void TabbedSelector::Inner::afterShownWithSearchFocus(') if exists else 'void TabbedSelector::Inner::afterShownWithSearchFocus(bool){afterShown();}'
    owner = function(selector, 'void TabbedSelector::afterShown()')
    cpp(root, 'selector_focus', r'''
#include <cassert>
#include <functional>
#include <iostream>
#include <memory>
#include <utility>
template<class T>using Fn=std::function<T>;
struct QWidget{std::shared_ptr<bool>alive=std::make_shared<bool>(true);bool focused=false;virtual~QWidget(){*alive=false;}void setFocus();bool hasFocus()const{return focused;}};
struct QApplication{static QWidget*current;static QWidget*focusWidget(){return current;}};QWidget*QApplication::current=nullptr;void QWidget::setFocus(){if(QApplication::current)QApplication::current->focused=false;focused=true;QApplication::current=this;}
namespace base{struct Weak{std::weak_ptr<bool>a;explicit operator bool()const{auto p=a.lock();return p&&*p;}};template<class T>Weak make_weak(T*p){return{p->alive};}}
namespace Ui{class SearchWithGroups{public:QWidget*_field=nullptr,*_focusTakenFrom=nullptr;Fn<bool()>_allowAutoFocus;void stealFocus();void returnFocus();};}
namespace ChatHelpers{class TabbedSelector:public QWidget{public:struct Inner:QWidget{bool _allowSearchAutoFocus=true;Fn<void()>hook;void afterShown(){if(hook)hook();}bool allowSearchAutoFocus()const{return _allowSearchAutoFocus;}void afterShownWithSearchFocus(bool);};struct Tab{Inner*inner;Inner*widget(){return inner;}}tab;unsigned long long _slideGeneration=0;bool _slideFromTabs=false;struct Animation{bool active=false;bool animating()const{return active;}}_a_slide;Fn<void(int)>_afterShownCallback;int _currentTabType=0,shown=0;void showAll(){++shown;}Tab*currentTab(){return &tab;}void afterShown();};
''' + inner + '\n' + owner + r'''}
namespace Ui{''' + sm + r'''}
int main(){QWidget header,field,external;header.setFocus();ChatHelpers::TabbedSelector::Inner inner;Ui::SearchWithGroups search;search._field=&field;search._allowAutoFocus=[&]{return inner.allowSearchAutoFocus();};inner.hook=[&]{search.stealFocus();};
inner.afterShownWithSearchFocus(false);assert(QApplication::current==&header&&search._focusTakenFrom==&header&&"suppression must keep bookmark but not steal header focus");assert(inner.allowSearchAutoFocus());external.setFocus();inner.afterShownWithSearchFocus(true);assert(QApplication::current==&field&&search._focusTakenFrom==&header);search.returnFocus();assert(QApplication::current==&header&&!search._focusTakenFrom);
inner.hook=[&]{assert(!inner.allowSearchAutoFocus());inner.hook=[&]{assert(!inner.allowSearchAutoFocus());};inner.afterShownWithSearchFocus(true);assert(!inner.allowSearchAutoFocus());};inner.afterShownWithSearchFocus(false);assert(inner.allowSearchAutoFocus());
ChatHelpers::TabbedSelector owner;owner.tab.inner=&inner;owner._slideFromTabs=true;header.setFocus();inner.hook=[&]{search.stealFocus();};bool callback=false;owner._afterShownCallback=[&](int){callback=true;assert(inner.allowSearchAutoFocus());};owner.afterShown();assert(callback&&owner.shown==1&&QApplication::current==&header);search.returnFocus();assert(!search._focusTakenFrom);owner._slideFromTabs=false;owner.afterShown();assert(QApplication::current==&field);search.returnFocus();
external.setFocus();search._allowAutoFocus={};search.stealFocus();assert(QApplication::current==&field);external.setFocus();search.returnFocus();assert(QApplication::current==&external&&!search._focusTakenFrom);
{auto i=std::make_unique<ChatHelpers::TabbedSelector::Inner>();i->hook=[&]{i.reset();};i->afterShownWithSearchFocus(false);assert(!i);}
{auto o=std::make_unique<ChatHelpers::TabbedSelector>();ChatHelpers::TabbedSelector::Inner i;o->tab.inner=&i;i.hook=[&]{o.reset();};bool called=false;o->_afterShownCallback=[&](int){called=true;};o->afterShown();assert(!o&&!called);}
std::cout<<"Actual Search bookmark/suppression/return + scoped Inner nested/destructive + owner lifecycle PASS; explicitfocus/weak/widget doubles, no nativeQt/search/focus/UIA\n";}
''')


if __name__ == '__main__':
    import tempfile
    with tempfile.TemporaryDirectory(prefix='luxury-selector-focus-') as directory:
        check(Path(directory))
