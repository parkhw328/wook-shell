"""Create the UTF-8 license resource shipped inside the single executable."""
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
sections = ["wShell 0.4.1 — Licenses and copyright notices\n"]
for path in [ROOT / "LICENSE", ROOT / "THIRD_PARTY_NOTICES.md", *sorted((ROOT / "licenses").glob("*.txt"))]:
    sections.append(f"\n{'=' * 72}\n{path.name}\n{'=' * 72}\n\n{path.read_text(encoding='utf-8')}")
(ROOT / "build").mkdir(exist_ok=True)
(ROOT / "build/legal-notices.txt").write_text("\n".join(sections), encoding="utf-8")
