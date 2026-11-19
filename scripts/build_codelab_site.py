#!/usr/bin/env python3
"""Validates PNG sketch headers for claat, exports CODELAB.md, and packages Starter Kit ZIP."""

import os
import re
import shutil
import subprocess
import zipfile
from PIL import Image


def ensure_real_png(path: str) -> None:
    """Ensures saved sketch image has a true PNG binary header so claat exports it cleanly."""
    if os.path.exists(path):
        with Image.open(path) as im:
            im.convert("RGB").save(path, "PNG")


def main() -> None:
    os.makedirs("_site", exist_ok=True)
    os.makedirs("codelab-assets", exist_ok=True)

    # 1. Ensure both saved sketch images have true PNG headers (even if downloaded as JPEG)
    ensure_real_png("codelab-assets/priority-trap-sketch.png")
    ensure_real_png("codelab-assets/pulse-memory-layout.png")

    md_raw = open("CODELAB.md", encoding="utf-8").read()
    md_fixed = md_raw.replace(".svg)", ".png)")
    if md_fixed != md_raw:
        open("CODELAB.md", "w", encoding="utf-8").write(md_fixed)

    # 2. Export CODELAB.md via claat
    claat_bin = shutil.which("claat")
    if not claat_bin:
        gopath = subprocess.check_output(["go", "env", "GOPATH"], text=True).strip()
        claat_bin = os.path.join(gopath, "bin", "claat")

    subprocess.run([claat_bin, "export", "-o", "_site", "CODELAB.md"], check=True)
    open("_site/.nojekyll", "w", encoding="utf-8").close()

    # 3. Package qnx-safety-lab-kit.zip from CODELAB.md C blocks
    blocks = re.findall(r"```c\r?\n(.*?)```", md_fixed, re.S)
    kit_dir = "qnx-safety-lab-kit"
    os.makedirs(kit_dir, exist_ok=True)

    if len(blocks) >= 6:
        open(f"{kit_dir}/lab_ipc.h", "w", encoding="utf-8").write(blocks[0])
        open(f"{kit_dir}/buggy_supervisor.c", "w", encoding="utf-8").write(blocks[1])
        open(f"{kit_dir}/client_ai.c", "w", encoding="utf-8").write(blocks[2])
        open(f"{kit_dir}/fixed_supervisor.c", "w", encoding="utf-8").write(blocks[3] + "\n" + blocks[4])
        open(f"{kit_dir}/sandboxed_ai_test.c", "w", encoding="utf-8").write(blocks[5])

    makefile = (
        "CC = qcc\n"
        "CFLAGS = -Vgcc_ntox86_64 -Wall -O2\n\n"
        "all: buggy_supervisor client_ai fixed_supervisor sandboxed_ai_test\n\n"
        "buggy_supervisor: buggy_supervisor.c lab_ipc.h\n"
        "\t$(CC) $(CFLAGS) buggy_supervisor.c -o buggy_supervisor\n\n"
        "client_ai: client_ai.c lab_ipc.h\n"
        "\t$(CC) $(CFLAGS) client_ai.c -o client_ai\n\n"
        "fixed_supervisor: fixed_supervisor.c lab_ipc.h\n"
        "\t$(CC) $(CFLAGS) fixed_supervisor.c -lm -o fixed_supervisor\n\n"
        "sandboxed_ai_test: sandboxed_ai_test.c lab_ipc.h\n"
        "\t$(CC) $(CFLAGS) sandboxed_ai_test.c -o sandboxed_ai_test\n\n"
        "clean:\n"
        "\trm -f buggy_supervisor client_ai fixed_supervisor sandboxed_ai_test\n"
    )
    open(f"{kit_dir}/Makefile", "w", encoding="utf-8").write(makefile)

    with zipfile.ZipFile("_site/qnx-safety-lab-kit.zip", "w", zipfile.ZIP_DEFLATED) as zf:
        for fn in sorted(os.listdir(kit_dir)):
            zf.write(os.path.join(kit_dir, fn), os.path.join(kit_dir, fn))

    # 4. Root redirect index.html
    index_html = (
        "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
        "<meta http-equiv=\"refresh\" content=\"0; url=./qnx8-deterministic-ipc-safety-cage/index.html\">"
        "</head><body>"
        "<a href=\"./qnx8-deterministic-ipc-safety-cage/index.html\">Open QNX 8.0 Interactive Codelab</a>"
        "</body></html>\n"
    )
    open("_site/index.html", "w", encoding="utf-8").write(index_html)
    print("[OK] Exported Codelab with hand-drawn sketches and Starter ZIP.")


if __name__ == "__main__":
    main()