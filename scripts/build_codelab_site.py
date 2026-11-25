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

    # 1. Ensure all 3 whiteboard sketches have true PNG headers for claat
    for sketch in (
        "codelab-assets/priority-trap-sketch.png",
        "codelab-assets/pulse-memory-layout.png",
        "codelab-assets/ability-jail-sketch.png",
    ):
        ensure_real_png(sketch)

    # 2. Export CODELAB.md via claat
    claat_bin = shutil.which("claat")
    if not claat_bin:
        gopath = subprocess.check_output(["go", "env", "GOPATH"], text=True).strip()
        claat_bin = os.path.join(gopath, "bin", "claat")

    subprocess.run([claat_bin, "export", "-o", "_site", "CODELAB.md"], check=True)
    open("_site/.nojekyll", "w", encoding="utf-8").close()

    # 3. Extract tagged /* FILE: ... */ C blocks cleanly into qnx-safety-lab-kit.zip
    md_text = open("CODELAB.md", encoding="utf-8").read()
    kit_dir = "qnx-safety-lab-kit"
    os.makedirs(kit_dir, exist_ok=True)

    for match in re.finditer(r"```c\r?\n/\*\s*FILE:\s*([a-zA-Z0-9_.-]+)\s*\*/\r?\n(.*?)```", md_text, re.S):
        fname, code = match.group(1), match.group(2)
        open(os.path.join(kit_dir, fname), "w", encoding="utf-8").write(code)

    makefile = (
        "CC = qcc\n"
        "CFLAGS = -Vgcc_ntox86_64 -Wall -O2\n\n"
        "TARGETS = buggy_supervisor client_ai priority_locked_supervisor fixed_supervisor sandboxed_ai_test\n\n"
        "all: $(TARGETS)\n\n"
        "buggy_supervisor: buggy_supervisor.c lab_ipc.h\n"
        "\t$(CC) $(CFLAGS) buggy_supervisor.c -o buggy_supervisor\n\n"
        "client_ai: client_ai.c lab_ipc.h\n"
        "\t$(CC) $(CFLAGS) client_ai.c -o client_ai\n\n"
        "priority_locked_supervisor: priority_locked_supervisor.c lab_ipc.h\n"
        "\t$(CC) $(CFLAGS) priority_locked_supervisor.c -o priority_locked_supervisor\n\n"
        "fixed_supervisor: fixed_supervisor.c lab_ipc.h\n"
        "\t$(CC) $(CFLAGS) fixed_supervisor.c -lm -o fixed_supervisor\n\n"
        "sandboxed_ai_test: sandboxed_ai_test.c lab_ipc.h\n"
        "\t$(CC) $(CFLAGS) sandboxed_ai_test.c -o sandboxed_ai_test\n\n"
        "clean:\n"
        "\trm -f $(TARGETS)\n"
    )
    open(os.path.join(kit_dir, "Makefile"), "w", encoding="utf-8").write(makefile)

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
    print("[OK] Exported 10-step Codelab with 3 whiteboard sketches and complete Starter ZIP.")


if __name__ == "__main__":
    main()