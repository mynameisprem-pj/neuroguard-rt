#!/usr/bin/env python3
"""Generates crisp PNG architecture diagrams, exports CODELAB.md via claat, and packages Starter Kit ZIP."""

import os
import re
import shutil
import subprocess
import zipfile
from PIL import Image, ImageDraw, ImageFont


def get_font(size: int, bold: bool = False):
    candidates = (
        [
            "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf" if bold else "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
            "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf" if bold else "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
            "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf" if bold else "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        ]
    )
    for path in candidates:
        if os.path.exists(path):
            return ImageFont.truetype(path, size)
    return ImageFont.load_default()


def draw_priority_trap_png(out_path: str) -> None:
    img = Image.new("RGB", (1400, 520), "#0b1220")
    d = ImageDraw.Draw(img)
    f_h1 = get_font(23, True)
    f_b = get_font(19, True)
    f_r = get_font(17, False)

    # Left Card: Buggy Default Channel
    d.rounded_rectangle((30, 30, 680, 490), radius=16, fill="#131c2e", outline="#ef4444", width=3)
    d.text((55, 58), "[BUG] DEFAULT CHANNEL: SILENT PRIORITY DROP", fill="#f87171", font=f_h1)
    d.text((55, 92), "ChannelCreate(0) inherits incoming client priority", fill="#94a3b8", font=f_r)

    d.rounded_rectangle((55, 155, 295, 265), radius=12, fill="#1e293b", outline="#fbbf24", width=2)
    d.text((75, 182), "AI Client Thread", fill="#fde68a", font=f_b)
    d.text((75, 215), "Priority: 20r (RR)", fill="#cbd5e1", font=f_r)

    d.line((300, 210, 395, 210), fill="#fbbf24", width=4)
    d.text((305, 180), "MsgSend()", fill="#fbbf24", font=f_r)

    d.rounded_rectangle((400, 155, 655, 265), radius=12, fill="#3b1219", outline="#ef4444", width=3)
    d.text((418, 182), "Safety Supervisor", fill="#fecaca", font=f_b)
    d.text((418, 215), "250f -> DROPS TO 20!", fill="#f87171", font=f_b)

    d.rounded_rectangle((110, 330, 600, 445), radius=12, fill="#450a0a", outline="#f87171", width=2)
    d.text((135, 355), "Rogue CPU Hog Thread (Priority 35r)", fill="#fca5a5", font=f_b)
    d.text((135, 392), "Preempts Supervisor (35 > 20) -> DEADLINE MISSED!", fill="#fecaca", font=f_r)

    # Right Card: Fixed-Priority + 1kHz Watchdog
    d.rounded_rectangle((720, 30, 1370, 490), radius=16, fill="#131c2e", outline="#22c55e", width=3)
    d.text((745, 58), "[FIX] FIXED-PRIORITY + 1kHz PULSE CAGE", fill="#4ade80", font=f_h1)
    d.text((745, 92), "_NTO_CHF_FIXED_PRIORITY + SIGEV_PULSE_INIT", fill="#94a3b8", font=f_r)

    d.rounded_rectangle((745, 155, 985, 265), radius=12, fill="#1e293b", outline="#38bdf8", width=2)
    d.text((765, 182), "Sandboxed AI", fill="#bae6fd", font=f_b)
    d.text((765, 215), "Prio 20r (Locked)", fill="#94a3b8", font=f_r)

    d.line((990, 210, 1085, 210), fill="#22d3ee", width=4)
    d.text((995, 180), "MsgSend()", fill="#22d3ee", font=f_r)

    d.rounded_rectangle((1090, 155, 1345, 265), radius=12, fill="#052e16", outline="#22c55e", width=3)
    d.text((1108, 182), "Safety Supervisor", fill="#bbf7d0", font=f_b)
    d.text((1108, 215), "LOCKED AT 250f!", fill="#4ade80", font=f_b)

    d.rounded_rectangle((795, 330, 1300, 445), radius=12, fill="#082f49", outline="#22d3ee", width=2)
    d.text((820, 355), "1,000 Hz Kernel Timer Pulse (Priority 250)", fill="#67e8f9", font=f_b)
    d.text((820, 392), "Wakes Supervisor every 1.0 ms -> Clamps in < 1 us", fill="#e0f2fe", font=f_r)

    img.save(out_path, "PNG")


def draw_pulse_layout_png(out_path: str) -> None:
    img = Image.new("RGB", (1400, 380), "#0b1220")
    d = ImageDraw.Draw(img)
    f_h1 = get_font(23, True)
    f_b = get_font(20, True)
    f_r = get_font(17, False)

    d.text((45, 35), "Multiplexed QNX Receive Buffer Layout (lab_msg_t) - struct _pulse First", fill="#22d3ee", font=f_h1)

    d.text((50, 95), "Byte 0", fill="#94a3b8", font=f_r)
    d.text((515, 95), "Byte 16", fill="#94a3b8", font=f_r)
    d.text((755, 95), "Byte 20", fill="#94a3b8", font=f_r)
    d.text((995, 95), "Byte 24 (to 32)", fill="#94a3b8", font=f_r)

    d.rounded_rectangle((45, 125, 505, 245), radius=10, fill="#082f49", outline="#22d3ee", width=3)
    d.text((70, 155), "struct _pulse hdr", fill="#67e8f9", font=f_b)
    d.text((70, 192), "code | priority | sival_int | scoid", fill="#bae6fd", font=f_r)

    d.rounded_rectangle((515, 125, 745, 245), radius=10, fill="#1e293b", outline="#94a3b8", width=2)
    d.text((535, 155), "uint16_t type", fill="#f8fafc", font=f_b)
    d.text((535, 192), "+ 2B align pad", fill="#94a3b8", font=f_r)

    d.rounded_rectangle((755, 125, 985, 245), radius=10, fill="#1e293b", outline="#94a3b8", width=2)
    d.text((775, 155), "uint32_t seq", fill="#f8fafc", font=f_b)
    d.text((775, 192), "1kHz heartbeat", fill="#94a3b8", font=f_r)

    d.rounded_rectangle((995, 125, 1355, 245), radius=10, fill="#052e16", outline="#22c55e", width=3)
    d.text((1020, 155), "double target_mm", fill="#4ade80", font=f_b)
    d.text((1020, 192), "Clamped |x| <= 18.00 mm", fill="#bbf7d0", font=f_r)

    d.text((45, 280), "* rcvid == 0 (Kernel Pulse): Microkernel writes 16 bytes into hdr; check msg.hdr.code == PULSE_CODE_HEARTBEAT.", fill="#4ade80", font=f_r)
    d.text((45, 318), "* rcvid > 0  (Client MsgSend): Microkernel copies full 32-byte lab_msg_t; validate & MsgReply(rcvid, ...).", fill="#38bdf8", font=f_r)

    img.save(out_path, "PNG")


def main() -> None:
    os.makedirs("_site", exist_ok=True)
    os.makedirs("codelab-assets", exist_ok=True)

    # 1. Generate crisp PNG diagrams directly via Pillow
    draw_priority_trap_png("codelab-assets/priority-trap-sketch.png")
    draw_pulse_layout_png("codelab-assets/pulse-memory-layout.png")

    # Ensure CODELAB.md references the .png assets cleanly
    md_raw = open("CODELAB.md", encoding="utf-8").read()
    md_fixed = md_raw.replace(".svg)", ".png)")
    if md_fixed != md_raw:
        open("CODELAB.md", "w", encoding="utf-8").write(md_fixed)

    # 2. Locate claat and export CODELAB.md
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
    print("[OK] Built PNG diagrams, Codelab HTML, and Starter ZIP cleanly.")


if __name__ == "__main__":
    main()