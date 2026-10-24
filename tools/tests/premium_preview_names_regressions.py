"""Execute actual preview index/move and icon construction; Qt/style/translation doubles only."""
from pathlib import Path
import json
import re
import tempfile
from library_regressions import cpp, source


def check(root):
    text = source('Telegram/SourceFiles/boxes/premium_preview_box.cpp')
    start = text.index('\tconst auto index =', text.index('void PreviewBox('))
    block = text[start:text.index('\n\tbuttonsParent->widthValue(', start)]
    en = source('Telegram/Resources/langs/lang.strings')
    ru = source('Telegram/SourceFiles/luxury/luxury_settings.cpp')
    labels = {}
    for suffix in ('Previous', 'Next'):
        key = 'luxury_PremiumPreview' + suffix
        labels[suffix] = [
            json.loads(re.search(r'"' + key + r'" = ("[^\n]*");', en).group(1)),
            json.loads(re.search(r'\{ "' + key + r'", ("[^\n]*") \}', ru).group(1)),
        ]
    close_label = json.loads(re.search(r'"lng_close" = ("[^\n]*");', en).group(1))
    definitions = '\n'.join(
        'QString luxury_PremiumPreview' + suffix + '(int){return language?'
        + json.dumps(values[1], ensure_ascii=False) + ':' + json.dumps(values[0]) + ';}'
        for suffix, values in labels.items())
    expected = json.dumps([labels['Previous'], labels['Next']], ensure_ascii=False)
    cpp(root, 'premium_preview_buttons', r'''
#include <algorithm>
#include <cassert>
#include <functional>
#include <iostream>
#include <memory>
#include <ranges>
#include <string>
#include <utility>
#include <vector>
using QString=std::string;
namespace tr{constexpr auto now=0;int language=0;
''' + definitions + '\nQString lng_close(int){return ' + json.dumps(close_label) + r''';}}
namespace st{constexpr int settingsPremiumTopBarClose=1,settingsPremiumMoveLeft=2,settingsPremiumMoveRight=3;}
namespace ranges{using std::ranges::find;}
enum class PremiumFeature{A,B,C,Gifts};
struct Widget{std::vector<std::unique_ptr<Widget>>children;virtual~Widget()=default;};
struct Box:Widget{Widget parent;int closes=0;struct Ptr{Widget*p;Widget*get()const{return p;}};Ptr verticalLayout(){return{&parent};}void closeBox(){++closes;}};
namespace Ui{struct IconButton:Widget{int style;QString name;std::function<void()>callback;IconButton(Widget*,int s):style(s){}void setAccessibleName(QString s){name=std::move(s);}void setClickedCallback(std::function<void()>f){callback=std::move(f);}};template<class T,class...A>T*CreateChild(Widget*p,A&&...a){auto w=std::make_unique<T>(p,std::forward<A>(a)...);auto raw=w.get();p->children.push_back(std::move(w));return raw;}}
template<class T>struct Variable{T value;T current()const{return value;}void operator=(T v){value=v;}};
struct State{Variable<PremiumFeature>selected;std::vector<PremiumFeature>order;};
int main(){int cases=0;const QString expected[2][2]=''' + expected.replace('[', '{').replace(']', '}') + r''';
using F=PremiumFeature;
for(int language:{0,1})for(const auto&order:std::vector<std::vector<F>>{{F::A,F::B,F::C},{F::C,F::A,F::B},{F::A},{F::Gifts}})for(auto selected:order){
 tr::language=language;Box ownedBox;Box*box=&ownedBox;State ownedState;State*state=&ownedState;state->selected=selected;state->order=order;
''' + block + r'''
 assert(close&&close->name==tr::lng_close(tr::now)&&close->style==1&&close->callback);
 if(selected==F::Gifts){assert(!left&&!right&&ownedBox.parent.children.size()==1);}else{
  assert(left&&right&&left->style==2&&right->style==3&&ownedBox.parent.children.size()==3);
  assert(left->name==expected[0][language]&&right->name==expected[1][language]);
  const auto before=index(selected);auto prev=left->callback;prev();assert(state->selected.current()==order[(before+order.size()-1)%order.size()]);auto next=right->callback;next();assert(state->selected.current()==selected);
  next();assert(state->selected.current()==order[(before+1)%order.size()]);prev();assert(state->selected.current()==selected);
 }
 assert(ownedBox.closes==0);++cases;
}
std::cout<<"Actual Premium Preview index/move + icon construction PASS: "<<cases<<" EN/RU x orders/selection variants, localized Close/Previous/Next, Gifts/null-arrows, single/wraparound movement, no real box/purchase/network; explicit Qt/style/translation seams\n";
}
''')


if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='luxury-premium-preview-') as directory:
        check(Path(directory))
