"""Create the UTF-8 license resource shipped inside the single executable."""
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]


def embed(root=ROOT):
    version = (root / "VERSION").read_text(encoding="utf-8").strip()
    sections = [f"wShell {version} — Licenses and copyright notices\n"]
    # WindowsPath compares names without case; PosixPath does not. Use the same
    # explicit ordering on both hosts, including a stable tie-breaker.
    licenses = sorted((root / "licenses").glob("*.txt"), key=lambda p: (p.name.casefold(), p.name))
    for path in [root / "LICENSE", root / "THIRD_PARTY_NOTICES.md", *licenses]:
        sections.append(f"\n{'=' * 72}\n{path.name}\n{'=' * 72}\n\n{path.read_text(encoding='utf-8')}")
    (root / "build").mkdir(exist_ok=True)
    (root / "build/legal-notices.txt").write_text("\n".join(sections), encoding="utf-8", newline="\n")


if __name__ == "__main__":
    embed()
