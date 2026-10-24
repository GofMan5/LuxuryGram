"""Dialog accessibility must expose painted ID-search rows.

The filtered dialog surface paints _idSearchResults between filter and peer
results and keyboard navigation already traverses that cohort. This bounded
source contract prevents the owning accessibility model from silently
omitting those visible rows; it is not native Qt/UIA acceptance.
"""
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / "Telegram/SourceFiles/dialogs/dialogs_inner_widget.h"
SOURCE = ROOT / "Telegram/SourceFiles/dialogs/dialogs_inner_widget.cpp"


def check():
    header = HEADER.read_text(encoding="utf-8")
    source = SOURCE.read_text(encoding="utf-8")
    if "IdSearch" not in header:
        raise AssertionError(
            "RED: dialog accessibility cohort lacks painted ID-search rows")
    required = (
        "_idSearchResults.size()",
        "AccessibilityCohort::IdSearch",
        "idSearchOffset() + ref->local * st::dialogsRowHeight",
        "_idSearchResults[ref->local]",
    )
    missing = [marker for marker in required if marker not in source]
    if missing:
        raise AssertionError(
            "RED: dialog accessibility omits painted ID-search mapping: "
            + ", ".join(missing))


if __name__ == "__main__":
    try:
        check()
    except AssertionError as error:
        print(error)
        raise SystemExit(1)
    print(
        "Dialog ID-search accessibility mapping PASS; "
        "source-only, not native Qt/UIA acceptance")
