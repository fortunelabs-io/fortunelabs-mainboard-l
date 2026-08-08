#!/usr/bin/env python3
"""
Sisipkan header SPDX-License-Identifier ke file sumber asli.

- main/**/*.c, main/**/*.h di firmware  -> Apache-2.0
- components/ssd1306/*.c, *.h           -> MIT (diturunkan dari nopnop2002/esp-idf-ssd1306)
- c2c/**/*.py, scripts/**/*.py, tests/**/*.py di cache-2-cache-lite -> Apache-2.0

Aman dijalankan berulang: file yang sudah punya baris SPDX di-skip, tidak ditumpuk dobel.
"""
import sys
from pathlib import Path

APACHE_C = "// SPDX-License-Identifier: Apache-2.0\n"
MIT_C = (
    "// SPDX-License-Identifier: MIT\n"
    "// Derived from https://github.com/nopnop2002/esp-idf-ssd1306\n"
)
APACHE_PY = "# SPDX-License-Identifier: Apache-2.0\n"

def already_tagged(text: str) -> bool:
    return "SPDX-License-Identifier" in text[:400]

def prepend(path: Path, header: str):
    text = path.read_text(encoding="utf-8")
    if already_tagged(text):
        return False
    path.write_text(header + "\n" + text, encoding="utf-8")
    return True

def process_dir(root: Path, patterns, header, exclude_substr=None):
    touched = []
    for pat in patterns:
        for f in sorted(root.rglob(pat)):
            if ".pio" in f.parts or ".git" in f.parts:
                continue
            if exclude_substr and exclude_substr in str(f):
                continue
            if prepend(f, header):
                touched.append(str(f))
    return touched

def main(mainboard_root: str, c2c_root: str):
    mb = Path(mainboard_root)
    c2c = Path(c2c_root)

    changed = []

    # 1. Mainboard firmware: main/ tree, Apache-2.0, but SKIP components/ssd1306 handled separately
    main_dir = mb / "firmware" / "main"
    changed += process_dir(main_dir, ["*.c", "*.h"], APACHE_C)

    # 2. Vendored ssd1306 component: MIT
    ssd_dir = mb / "firmware" / "components" / "ssd1306"
    changed += process_dir(ssd_dir, ["*.c", "*.h"], MIT_C)

    # 3. cache-2-cache-lite: c2c/, scripts/, tests/ -> Apache-2.0
    for sub in ["c2c", "scripts", "tests"]:
        changed += process_dir(c2c / sub, ["*.py"], APACHE_PY)

    print(f"Total file diberi header: {len(changed)}")
    for c in changed:
        print(" -", c)

if __name__ == "__main__":
    mainboard_root = sys.argv[1] if len(sys.argv) > 1 else "mainboard_extract/fortunelabs-mainboard-l-main"
    c2c_root = sys.argv[2] if len(sys.argv) > 2 else "c2c_extract/cache-2-cache-lite-main"
    main(mainboard_root, c2c_root)
