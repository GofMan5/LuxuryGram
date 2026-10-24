"""Execute actual wallpaper button construction; no real preview, theme change or profile."""
from pathlib import Path
import json
import re
import tempfile
from library_regressions import cpp, function, source


def check(root):
    text = source('Telegram/SourceFiles/boxes/background_preview_box.cpp')
    method = function(text, 'void BackgroundPreviewBox::rebuildButtons(')
    en = source('Telegram/Resources/langs/lang.strings')
    ru = source('Telegram/SourceFiles/luxury/luxury_settings.cpp')
    labels = {}
    for suffix in ('Light', 'Dark'):
        key = 'luxury_BackgroundPreview' + suffix
        english = re.search(r'"' + key + r'" = ("[^\n]*");', en).group(1)
        russian = re.search(r'\{ "' + key + r'", ("[^\n]*") \}', ru).group(1)
        labels[suffix] = [json.loads(english), json.loads(russian)]
    definitions = '\n'.join(
        'QString luxury_BackgroundPreview' + suffix + '(int){return language?' +
        json.dumps(values[1], ensure_ascii=False) + ':' + json.dumps(values[0]) + ';}'
        for suffix, values in labels.items())
    expected = json.dumps([labels['Dark'], labels['Light']], ensure_ascii=False)
    cpp(root, 'background_preview_buttons', r'''
#include <cassert>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>
using QString=std::string;
namespace tr{constexpr auto now=0;int language=0;
''' + definitions + r'''
QString lng_background_apply_group(){return "group";}QString lng_background_apply_channel(){return "channel";}QString lng_background_apply_button(){return "peer";}QString lng_settings_apply(){return "settings";}QString lng_cancel(){return "cancel";}QString lng_background_share(){return "share";}}
namespace st{constexpr int backgroundSwitchToLight=1,backgroundSwitchToDark=2;}
namespace Window::Theme{bool night=false;bool IsNightMode(){return night;}}
struct Widget{};
namespace Ui{struct IconButton{int style;QString name;std::function<void()>callback;IconButton(Widget*,int s):style(s){}void setAccessibleName(QString s){name=std::move(s);}void setClickedCallback(std::function<void()>f){callback=std::move(f);}};}
template<class T>struct object_ptr{std::unique_ptr<T>p;object_ptr(Widget*w,int s):p(std::make_unique<T>(w,s)){}object_ptr(object_ptr&&)=default;T*operator->(){return p.get();}};
struct Mode{bool v=false;bool current(){return v;}void operator=(bool b){v=b;}};
struct BackgroundPreviewBox:Widget{
 bool group=false,channel=false,_forPeer=false;Mode _boxDarkMode;struct{bool share=false;bool hasShareUrl(){return share;}}_paper;
 struct Style{int toggle;};Style light{3},dark{4};Style*_light=&light,*_dark=&dark;
 int applies=0,closes=0,shares=0,clears=0;std::unique_ptr<Ui::IconButton>top;std::vector<QString>bottom;
 bool forGroup(){return group;}bool forChannel(){return channel;}
 void clearButtons(){++clears;bottom.clear();top.reset();}void apply(){++applies;}void closeBox(){++closes;}void share(){++shares;}
 template<class F>void addButton(QString label,F){bottom.push_back(label);}template<class F>void addLeftButton(QString label,F){bottom.push_back(label);}
 void addTopButton(object_ptr<Ui::IconButton>button){top=std::move(button.p);}void rebuildButtons(bool);
};
''' + method + r'''
int main(){int cases=0;const QString expected[2][2]=EXPECTED;
for(int language:{0,1})for(bool night:{false,true})for(bool dark:{false,true})for(int target:{0,1,2,3})for(bool share:{false,true}){
 tr::language=language;Window::Theme::night=night;BackgroundPreviewBox b;b._boxDarkMode.v=dark;b.group=target==2;b.channel=target==3;b._forPeer=target>0;b._paper.share=share;
 b.rebuildButtons(dark);assert(b.top&&b.top->name==expected[dark][language]);const auto style=dark==night?(dark?1:2):(dark?4:3);assert(b.top->style==style);
 assert(b.bottom[0]==(b.group?"group":b.channel?"channel":b._forPeer?"peer":"settings"));assert(b.bottom[1]=="cancel"&&b.bottom.size()==(!b._forPeer&&share?3:2));
 auto callback=b.top->callback;callback();assert(b._boxDarkMode.v==!dark&&Window::Theme::night==night);b.rebuildButtons(!dark);assert(b.top->name==expected[!dark][language]);assert(b.clears==2&&b.applies==0&&b.closes==0&&b.shares==0);++cases;
}
std::cout<<"Actual BackgroundPreviewBox button construction PASS: "<<cases<<" EN/RU x app/preview mode x target/share variants, label/style rebuild, preview-only callback, no apply/share action; UI/style/translation seams, not native Qt\n";
}
'''.replace('EXPECTED', expected.replace('[', '{').replace(']', '}')))


if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='luxury-background-buttons-') as directory:
        check(Path(directory))
