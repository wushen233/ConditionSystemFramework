"""Render offline documentation with CommonMark and Markdown tables."""

import sys
from pathlib import Path

from markdown_it import MarkdownIt


def main() -> None:
    source = Path(sys.argv[1]).read_text(encoding="utf-8")
    sys.stdout.write(MarkdownIt("commonmark").enable("table").render(source))


if __name__ == "__main__":
    main()
