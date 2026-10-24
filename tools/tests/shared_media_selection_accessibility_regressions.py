"""Shared-media list exposes its existing multi-selection semantics to UIA."""
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / 'Telegram/SourceFiles/info/media/info_media_list_widget.h'
SOURCE = ROOT / 'Telegram/SourceFiles/info/media/info_media_list_widget.cpp'


def check():
    header = HEADER.read_text(encoding='utf-8')
    source = SOURCE.read_text(encoding='utf-8')
    assert (
        'Ui::AccessibilityState accessibilityState() const override;' in header
    ), 'RED: shared-media owner does not expose its list selection state'
    state_start = source.index(
        'Ui::AccessibilityState ListWidget::accessibilityState() const {')
    state_end = source.index(
        'QAccessible::State ListWidget::accessibilityChildState(int index) const {',
        state_start,
    )
    state = source[state_start:state_end]
    assert '.extSelectable = true' in state
    assert '.multiSelectable = true' in state
    child_start = source.index(
        'QAccessible::State ListWidget::accessibilityChildState(int index) const {')
    child_end = source.index(
        'QAccessible::Role ListWidget::accessibilityChildRole() const {',
        child_start,
    )
    child_state = source[child_start:child_end]
    assert 'state.selectable = true;' in child_state


if __name__ == '__main__':
    check()
    print('Shared-media selection accessibility GREEN contract PASS')
