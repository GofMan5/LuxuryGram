"""Execute both production send-as-file init paths; no button activation, Qt, profile or network."""
from pathlib import Path
import tempfile
from library_regressions import cpp, function, source


def check(root):
    legacy = function(source('Telegram/SourceFiles/history/history_widget.cpp'),
                      'void HistoryWidget::initSendAsFileButton()')
    modern = function(source('Telegram/SourceFiles/history/view/controls/history_view_compose_controls.cpp'),
                      'void ComposeControls::initSendAsFileButton()')
    cpp(root, 'composer_file_names', r'''
#include <cassert>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
using QString=std::string;
QString operator""_cs(const char*s,size_t n){return QString(s,n);}
namespace tr{constexpr int now=0,rich=1;QString label="Send text as a file.";QString lng_send_as_file_tooltip(int){return label;}}
struct Text{QString text="Unsent draft";};struct Cursor{int position()const{return 7;}int anchor()const{return 2;}};
struct Field{Text getTextWithTags(){return{};}Cursor textCursor(){return{};}};
struct Widget{int width(){return 274;}};
struct Button:Widget{QString name;int hides=0;std::function<void()>callback;void hide(){++hides;}void setAccessibleName(const QString&s){name=s;}void setClickedCallback(std::function<void()>f){callback=f;}};
namespace HistoryView::Controls{
struct AiTooltipManager{QString text;template<class A,class C>AiTooltipManager(A*,Button*,QString value,QString,C):text(value){}void hideAndRemember(){}};
struct ComposeControls{Button storage;Button*_sendAsFile=&storage;Field field;Field*_field=&field;std::unique_ptr<Widget>_wrap=std::make_unique<Widget>();std::unique_ptr<AiTooltipManager>_sendAsFileTooltipManager;int sends=0;std::function<void()>restoreTextCallback(QString){return[]{};}void fireSendTextAsFile(QString,std::function<void()>){++sends;}void initSendAsFileButton();};
''' + modern + r'''
}
struct HistoryWidget:Widget{Button storage;Button*_sendAsFile=&storage;Field field;Field*_field=&field;std::unique_ptr<HistoryView::Controls::AiTooltipManager>_sendAsFileTooltipManager;int sends=0;void sendTextAsFile(QString,Text,int,int){++sends;}void initSendAsFileButton();};
''' + legacy + r'''
int main(){
for(const auto&label:{QString("Send text as a file."),QString("Localized file action")}){
 tr::label=label;HistoryWidget legacy;HistoryView::Controls::ComposeControls modern;
 legacy.initSendAsFileButton();modern.initSendAsFileButton();
 assert(legacy.storage.name==label&&modern.storage.name==label);
 assert(legacy.storage.hides==1&&modern.storage.hides==1);
 assert(legacy.storage.callback&&modern.storage.callback);
 assert(legacy._sendAsFileTooltipManager->text==label&&modern._sendAsFileTooltipManager->text==label);
 assert(legacy.sends==0&&modern.sends==0);
}
HistoryView::Controls::ComposeControls absent;absent._sendAsFile=nullptr;absent.initSendAsFileButton();assert(!absent._sendAsFileTooltipManager&&absent.storage.name.empty()&&!absent.storage.callback&&absent.sends==0);
std::cout<<"Actual ordinary-chat and ComposeControls send-as-file init PASS: localized names equal tooltip, hidden initial state, callbacks installed but never invoked, disabled feature null branch; explicit UI/field/tooltip doubles\n";
}
''')


if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='luxury-composer-names-') as directory:
        check(Path(directory))
