#!/usr/bin/env python3
"""Validate OBS locale syntax.

OBS's text lookup parser stops an unquoted value at the first whitespace.
Locale values that contain spaces (or a comment marker) must therefore be
double-quoted, otherwise labels silently lose everything after the first word.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

LOCALE = Path(__file__).resolve().parents[1] / "data" / "locale" / "en-US.ini"


def main() -> int:
    errors: list[str] = []
    for number, line in enumerate(LOCALE.read_text(encoding="utf-8").splitlines(), 1):
        stripped = line.lstrip()
        if not stripped or stripped.startswith(("#", ";")) or "=" not in line:
            continue
        key, value = line.split("=", 1)
        if not key.strip():
            errors.append(f"{LOCALE}:{number}: empty key")
            continue
        needs_quotes = bool(re.search(r"\s", value)) or "#" in value
        quoted = value.startswith('"') and value.endswith('"')
        if needs_quotes and not quoted:
            errors.append(
                f"{LOCALE}:{number}: {key} contains whitespace or '#' but is not quoted"
            )
        if quoted:
            inner = value[1:-1]
            if re.search(r'(?<!\\)"', inner):
                errors.append(f"{LOCALE}:{number}: {key} has an unescaped quote")
    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1
    print(f"locale: {LOCALE.name} syntax ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
