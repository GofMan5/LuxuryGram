"""Actual saved-music reuse/restore + controller identity predicate; explicit memento/UI seams."""
from pathlib import Path
import tempfile
from library_regressions import cpp, function, source


def check(root):
    text = source('Telegram/SourceFiles/info/saved/info_saved_music_widget.cpp')
    methods = '\n'.join(function(text, signature) for signature in (
        'bool MusicWidget::showInternal(', 'void MusicWidget::restoreState(',
        'void MusicInner::restoreState('))
    validation = function(source('Telegram/SourceFiles/info/info_controller.cpp'),
                          'bool Controller::validateMementoPeer(')
    routing = function(source('Telegram/SourceFiles/info/info_wrap_widget.cpp'),
                       'bool WrapWidget::showInternal(')
    cpp(root, 'saved_music_navigation', r'''
#include <cassert>
#include <iostream>
#include <memory>
#include <vector>
template<class T>struct not_null{T p;not_null(T v):p(v){assert(v);}T get()const{return p;}T operator->()const{return p;}operator T()const{return p;}};
namespace Window{struct SectionMemento{virtual~SectionMemento()=default;};struct SectionShow{enum class Way{Forward,Backward,ClearStack};Way way=Way::Forward;};}
namespace Info{
struct Identity{int peerValue=0,musicValue=10;int peer()const{return peerValue;}int musicPeer()const{return musicValue;}int topic()const{return 0;}int sublist()const{return 0;}int migratedPeerId()const{return 0;}int settingsSelf()const{return 0;}int storiesPeer()const{return 0;}struct Tag{int peer=0;};Tag statisticsTag()const{return{};}int starrefPeer()const{return 0;}int starrefType()const{return 0;}};
struct ContentMemento:Identity{virtual~ContentMemento()=default;};
struct Controller:Identity{bool validateMementoPeer(not_null<ContentMemento*>)const;};
''' + validation + r'''
namespace Media{struct Memento{int anchor=51,window=32,shift=7;};}
namespace Saved{
struct MusicMemento:ContentMemento{Media::Memento state;int scroll=90;int scrollTop()const{return scroll;}Media::Memento&media(){return state;}};
struct OtherMemento:ContentMemento{};
struct List{Media::Memento*seen=nullptr;int restores=0;void restoreState(Media::Memento*m){seen=m;++restores;}};
struct MusicInner{List list;List*_list=&list;void restoreState(not_null<MusicMemento*>);};
struct MusicWidget{Controller c;MusicInner inner;MusicInner*_inner=&inner;int scroll=11,restores=0;Controller*controller(){return&c;}void scrollTopRestore(int v){assert(inner.list.restores==restores+1);scroll=v;++restores;}bool showInternal(not_null<ContentMemento*>);void restoreState(not_null<MusicMemento*>);};
''' + methods + r'''
}
struct Memento:Window::SectionMemento{ContentMemento*p;int count=1;explicit Memento(ContentMemento*v):p(v){}int stackSize(){return count;}ContentMemento*content(){return p;}};
struct WrapWidget{Controller*_controller;Saved::MusicWidget*_content;bool _mementoTaken=false,stack=false;int replaces=0,returns=0;bool hasStackHistory(){return stack;}bool returnToFirstStackFrame(ContentMemento*,const Window::SectionShow&){++returns;return false;}void showNewContent(ContentMemento*,const Window::SectionShow&){++replaces;}bool showInternal(not_null<Window::SectionMemento*>,const Window::SectionShow&);};
''' + routing + r'''
}
int main(){using namespace Info;Saved::MusicWidget widget;Saved::MusicMemento same;
assert(widget.showInternal(&same));assert(widget.restores==1&&widget.scroll==90&&widget.inner.list.seen==&same.state&&widget.inner.list.seen->anchor==51);
same.scroll=0;same.state.anchor=0;assert(widget.showInternal(&same));assert(widget.restores==2&&widget.scroll==0&&widget.inner.list.seen->anchor==0);
Saved::MusicMemento different;different.musicValue=20;assert(!widget.showInternal(&different));assert(widget.restores==2);
Saved::OtherMemento profile;profile.peerValue=10;profile.musicValue=0;assert(!widget.showInternal(&profile));assert(widget.restores==2);
Saved::OtherMemento sameIdentityNonMusic;assert(!widget.showInternal(&sameIdentityNonMusic));assert(widget.restores==2);
WrapWidget wrap{&widget.c,&widget};Memento section{&same};assert(wrap.showInternal(&section,{}));assert(widget.restores==3&&wrap.replaces==0&&wrap.returns==0);
Memento profileSection{&profile};assert(wrap.showInternal(&profileSection,{}));assert(wrap.replaces==1&&widget.restores==3);
wrap.stack=true;assert(wrap.showInternal(&section,{Window::SectionShow::Way::ClearStack}));assert(wrap.replaces==2&&widget.restores==3);
section.count=2;assert(!wrap.showInternal(&section,{}));section.count=1;wrap._mementoTaken=true;assert(!wrap.showInternal(&section,{}));assert(wrap.replaces==2&&widget.restores==3);
std::cout<<"Actual MusicWidget showInternal/restoreState + MusicInner delegation + Controller identity + WrapWidget routing PASS: reuse inner/outer state, default reset, foreign peer/profile/type rejection, ClearStack/taken/stack guards; UI replacement seams, no Qt navigation runtime\n";
}
''')


if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='luxury-music-navigation-') as directory:
        check(Path(directory))
