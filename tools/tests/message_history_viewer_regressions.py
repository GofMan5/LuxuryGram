"""The deleted/edited message viewer keeps its items honest.

Every bubble was generated with a zero sent date, so the "Sent ..." tooltip the
viewer already supports never appeared and the original send time was lost.
OwnedItem's move assignment dropped the outgoing item without destroy().
"""
from library_regressions import function, source

ITEM = 'Telegram/SourceFiles/luxury/ui/message_history/history_item.cpp'


def check(root=None, read=source):
    text = read(ITEM)
    generate = function(text, 'void GenerateItems(')
    assert 'TimeId(message.date),' in generate
    assert ', 0, MsgId(' not in generate
    assign = function(text, 'OwnedItem &OwnedItem::operator=(OwnedItem &&other)')
    assert 'if (this != &other) {' in assign
    order = [assign.index(s) for s in (
        'const auto old = base::take(_data);',
        '_view = base::take(other._view);',
        'old->destroy();')]
    assert order == sorted(order)
    print('Message history viewer PASS: the sent date reaches the tooltip; move assignment destroys the outgoing item after its view')


if __name__ == '__main__':
    check()
