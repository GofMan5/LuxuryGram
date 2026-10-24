"""Focused source contract for IconPicker's painted-cell accessibility model."""
from pathlib import Path


def check(root: Path):
    header = (root / "Telegram/SourceFiles/luxury/ui/components/icon_picker.h").read_text(encoding="utf-8")
    source = (root / "Telegram/SourceFiles/luxury/ui/components/icon_picker.cpp").read_text(encoding="utf-8")
    required_header = (
        "QAccessible::Role accessibilityRole() override;",
        "Qt::FocusPolicy accessibilityFocusPolicy() override;",
        "bool accessibilitySelectionList() const override;",
        "int accessibilityChildCount() const override;",
        "QString accessibilityChildName(int index) const override;",
        "QAccessible::State accessibilityChildState(int index) const override;",
        "QAccessible::Role accessibilityChildRole() const override;",
        "QRect accessibilityChildRect(int index) const override;",
        "bool accessibilityChildSupportsActions(int index) const override;",
        "quintptr accessibilityChildIdentity(int index) const override;",
        "int accessibilityChildIndexByIdentity(quintptr identity) const override;",
        "void accessibilityChildSetFocus(quintptr identity) override;",
        "void accessibilityChildActivate(quintptr identity) override;",
    )
    missing = [marker for marker in required_header if marker not in header]
    if missing:
        raise AssertionError(
            "IconPicker painted cells lack owning accessibility declarations: "
            + ", ".join(missing)
        )
    required_source = (
        "QAccessible::Role IconPicker::accessibilityRole()",
        "Qt::FocusPolicy IconPicker::accessibilityFocusPolicy()",
        "bool IconPicker::accessibilitySelectionList()",
        "int IconPicker::accessibilityChildCount()",
        "QString IconPicker::accessibilityChildName(int index)",
        "QAccessible::State IconPicker::accessibilityChildState(int index)",
        "QAccessible::Role IconPicker::accessibilityChildRole()",
        "QRect IconPicker::accessibilityChildRect(int index)",
        "bool IconPicker::accessibilityChildSupportsActions(int index)",
        "quintptr IconPicker::accessibilityChildIdentity(int index)",
        "int IconPicker::accessibilityChildIndexByIdentity(quintptr identity)",
        "void IconPicker::accessibilityChildSetFocus(quintptr identity)",
        "void IconPicker::accessibilityChildActivate(quintptr identity)",
        "accessibilityChildSelectionChanged(index)",
        "accessibilityChildFocused(index)",
        "LuxuryAssets::applyAppIcon();",
        "iconRect(index)",
    )
    missing = [marker for marker in required_source if marker not in source]
    if missing:
        raise AssertionError(
            "IconPicker painted cells lack owning accessibility behavior: "
            + ", ".join(missing)
        )
    assert "setAccessibleName(tr::luxury_AppIconPickerAccessible(tr::now));" in source
    print("IconPicker painted-cell accessibility contract PASS: list/items, names, selection/focus, stable actions")


if __name__ == "__main__":
    check(Path(__file__).resolve().parents[2])
