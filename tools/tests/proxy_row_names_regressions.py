"""Check the actual ProxyRow constructor wiring for its menu action name."""
from pathlib import Path
import tempfile
from library_regressions import cpp, function, source


def check(root):
    text = source('Telegram/SourceFiles/boxes/connection_box.cpp')
    method = function(text, 'ProxyRow::ProxyRow(')
    assert '_menuToggle->setAccessibleName(tr::lng_proxy_settings(tr::now));' in method
    assert 'setupControls(std::move(view));' in method
    cpp(root, 'proxy_row_name', r'''
#include <cassert>
#include <iostream>
#include <memory>
#include <string>
using QString=std::string;
namespace tr{constexpr int now=0;QString lng_proxy_settings(int){return "Proxy settings";}}
namespace st{constexpr int proxyRowRipple=1,topBarMenuToggle=2;}
struct Widget{virtual~Widget()=default;};using QWidget=Widget;struct RippleButton:Widget{RippleButton(Widget*,int){}};struct Button:Widget{QString name;Button(Widget*,int){}void setAccessibleName(QString s){name=s;}};template<class T>struct object_ptr{std::unique_ptr<T>p;object_ptr(Widget*w,int s):p(std::make_unique<T>(w,s)){}T*operator->(){return p.get();}};
struct View{};struct ProxyRow:RippleButton{object_ptr<Button> _menuToggle;int controls=0;ProxyRow(Widget*,View&&);void setupControls(View&&){++controls;}};
''' + method + r'''
int main(){Widget w;ProxyRow row(&w,{});assert(row._menuToggle->name=="Proxy settings"&&row.controls==1);std::cout<<"Actual ProxyRow menu name+setup wiring PASS; no menu open/network/native Qt\n";}
''')


if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='luxury-proxy-row-') as directory:
        check(Path(directory))
