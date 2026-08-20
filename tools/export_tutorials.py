"""Regenerate the tutorial .html and .pdf artifacts from their .md sources.

The .md files under doc/tutorials/ are the source of truth; this script rebuilds
the styled .html (VS Code markdown look, mermaid via CDN) and prints each page
to .pdf with headless Edge. Run whenever a tutorial .md changes:

    python tools/export_tutorials.py
"""

import pathlib
import re
import subprocess
import sys

import markdown

REPO = pathlib.Path(__file__).resolve().parent.parent
TUTORIAL_DIR = REPO / "doc" / "tutorials"
HEAD_TEMPLATE = (REPO / "tools" / "tutorial_export_head.html").read_text(encoding="utf-8")

EDGE_CANDIDATES = [
    r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe",
    r"C:\Program Files\Microsoft\Edge\Application\msedge.exe",
]

MERMAID_SNIPPET = """
<script src="https://unpkg.com/mermaid@9.4.0/dist/mermaid.min.js"></script>
<script>
    document.addEventListener("DOMContentLoaded", function () {
        mermaid.initialize({ startOnLoad: true, theme: "default" });
    });
</script>
"""


def convert_markdown(md_text: str) -> str:
    # Mermaid fences become <div class="mermaid"> blocks BEFORE markdown runs,
    # protected as raw HTML via the md_in_html convention.
    def mermaid_repl(match: "re.Match[str]") -> str:
        return '<div class="mermaid">\n' + match.group(1) + "\n</div>"

    md_text = re.sub(r"```mermaid\n(.*?)```", mermaid_repl, md_text, flags=re.DOTALL)
    return markdown.markdown(
        md_text,
        extensions=["fenced_code", "tables", "toc", "sane_lists", "md_in_html"],
    )


def find_edge() -> str:
    for candidate in EDGE_CANDIDATES:
        if pathlib.Path(candidate).exists():
            return candidate
    raise SystemExit("msedge.exe not found - install Edge or add its path to EDGE_CANDIDATES")


def export(md_path: pathlib.Path, edge: str) -> None:
    html_path = md_path.with_suffix(".html")
    pdf_path = md_path.with_suffix(".pdf")

    body = convert_markdown(md_path.read_text(encoding="utf-8"))
    needs_mermaid = 'class="mermaid"' in body
    html = (
        HEAD_TEMPLATE.replace("{TITLE}", md_path.name)
        + (MERMAID_SNIPPET if needs_mermaid else "")
        + "\n</head>\n<body>\n"
        + body
        + "\n</body>\n</html>\n"
    )
    html_path.write_text(html, encoding="utf-8")

    subprocess.run(
        [
            edge,
            "--headless",
            "--disable-gpu",
            "--no-pdf-header-footer",
            "--virtual-time-budget=10000",
            f"--print-to-pdf={pdf_path}",
            html_path.resolve().as_uri(),
        ],
        check=True,
        timeout=120,
    )
    print(f"exported {md_path.name} -> {html_path.name}, {pdf_path.name}")


def main() -> None:
    edge = find_edge()
    targets = sys.argv[1:] or sorted(
        p.name for p in TUTORIAL_DIR.glob("*.md")
    )
    for name in targets:
        export(TUTORIAL_DIR / name, edge)


if __name__ == "__main__":
    main()
