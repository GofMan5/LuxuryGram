"""Filters import from a link submits with Enter and opens focused.

Settings > LuxuryGram > Filters > Import > Import from URL: the link field
ignored Enter, so a pasted link did nothing until the mouse reached the
Import button, while upstream boxes with one field (folder invite link
label, forum topic title, contact names) submit on Enter. When the
clipboard held a link the box opened with that option chosen but the focus
was not in the field.

Bounded source check; not a native acceptance.
"""
from pathlib import Path
import re
import tempfile
from library_regressions import function, source


BOX = 'Telegram/SourceFiles/luxury/ui/boxes/import_filters_box.cpp'


def check(root):
    box = function(source(BOX), 'void FillImportFiltersBox(')
    if 'submits()' not in box:
        raise AssertionError('RED: Enter in the import link field does nothing')
    assert re.search(
        r'const auto submit = \[=\]\s*\{\s*const auto isURL = intoURL->current\(\);', box), (
        'the Import/Export action must be one named handler')
    assert re.search(
        r'box->addButton\(\s*import \? tr::luxury_FiltersMenuImport\(\) '
        r': tr::luxury_FiltersMenuExport\(\),\s*submit\);', box), (
        'the button must run the same handler')
    assert re.search(
        r'importURLField->submits\(\) \| rpl::on_next\(\[=\] \{\s*submit\(\);\s*\}, '
        r'importURLField->lifetime\(\)\);', box), (
        'Enter in the link field must run the same handler')
    assert box.index('importURLField->submits()') > box.index('const auto submit = '), (
        'the field must be wired after the handler exists')
    assert re.search(
        r'box->setFocusCallback\(\[=\] \{\s*if \(import && intoURL->current\(\)\) \{\s*'
        r'importURLField->setFocusFast\(\);\s*\} else \{\s*box->setFocus\(\);\s*\}\s*\}\);', box), (
        'a box opened on the link option must focus the link field, '
        'otherwise keep the default box focus')
    handler = box[box.index('const auto submit = '):box.index('const auto actionButton')]
    assert re.search(
        r'if \(link\.isEmpty\(\)\) \{\s*importURLField->showError\(\);\s*return;\s*\}', handler), (
        'an empty link must still show the field error and keep the box open')
    assert handler.rstrip().endswith('box->closeBox();\n\t};') or handler.rstrip().endswith('box->closeBox();\r\n\t};'), (
        'the box must close after the action starts')
    print('Import filters submit PASS: Enter and the button share one handler, '
        'link option opens focused, empty link keeps the error')


if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='luxury-import-filters-') as directory:
        check(Path(directory))
