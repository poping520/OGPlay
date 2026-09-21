"""Export layout.js from layout.json; no image assets are generated."""
import json
from pathlib import Path


def main():
    assets = Path(__file__).resolve().parent / "assets"
    layout = json.loads((assets / "layout.json").read_text(encoding="utf-8"))
    (assets / "layout.js").write_text(
        "window.DOCK_LAYOUT = " + json.dumps(layout, ensure_ascii=False) + ";\n",
        encoding="utf-8",
    )
    print("Updated assets/layout.js (vector-only page)")


if __name__ == "__main__":
    main()
