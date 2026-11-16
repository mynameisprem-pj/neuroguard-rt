#!/usr/bin/env python3
"""
NeuroGuard-RT: Host Surgical Robotics Cockpit Server (HTTP :3000 <-> QNX TCP :8080)
Zero external pip dependencies. Automatically streams live QNX 8.0 hardware frames
from 127.0.0.1:8080 when QEMU is active, or falls back to a deterministic physics
mirror when developing/testing UI locally without QEMU running.
"""

import json
import math
import os
import random
import socket
import threading
import time
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse

QNX_HOST = os.environ.get("QNX_TARGET_HOST", "127.0.0.1")
QNX_PORT = int(os.environ.get("QNX_TARGET_PORT", "8080"))
HTTP_PORT = int(os.environ.get("PORT", "3000"))
DASHBOARD_DIR = os.path.dirname(os.path.abspath(__file__))

SAFE_RADIUS_MM = 18.00


class TelemetryState:
    def __init__(self):
        self.lock = threading.Lock()
        self.qnx_connected = False
        self.qnx_sock = None
        self.boot_monotonic = time.monotonic()

        # Fallback mirror state (used only when QNX TCP :8080 is unreachable)
        self.fault_mode = 0       # 0=NONE, 1=STARVE_AI, 2=UNSAFE_OUTPUT
        self.state = 0            # 0=NORMAL, 2=SAFE_CLAMPED
        self.fault_reason = 0
        self.seq = 0
        self.missed_deadlines = 0
        self.reset_seq = 0
        self.filt_x = 11.2
        self.filt_y = 0.0
        self.safe_x = 11.2
        self.safe_y = 0.0
        self.last_hb_ts = time.monotonic()
        self.react_window = [1.42] * 64
        self.latest_frame = {}

    def send_command(self, cmd_str: str):
        cmd_upper = cmd_str.strip().upper()
        with self.lock:
            if "STARVE=1" in cmd_upper or "BOMB=1" in cmd_upper:
                self.fault_mode = 1
            elif "UNSAFE=1" in cmd_upper:
                self.fault_mode = 2
            elif "RESET" in cmd_upper or "BOMB=0" in cmd_upper:
                self.fault_mode = 0
                self.state = 0
                self.fault_reason = 0
                self.missed_deadlines = 0
                self.reset_seq += 1
                self.last_hb_ts = time.monotonic()

            sock = self.qnx_sock
            if sock is not None:
                try:
                    sock.sendall((cmd_upper + "\n").encode("utf-8"))
                except OSError:
                    self.qnx_sock = None
                    self.qnx_connected = False

    def step_local_mirror(self):
        """High-fidelity mirror matching safety_supervisor.c + ai_tremor_engine.c"""
        now = time.monotonic()
        t = now - self.boot_monotonic
        with self.lock:
            self.seq += 1
            base_r = 11.2 + 2.4 * math.sin(t * 0.9)
            angle = t * 1.35

            tremor_x = 1.55 * math.sin(2.0 * math.pi * 10.0 * t)
            tremor_y = 1.55 * math.cos(2.0 * math.pi * 10.0 * t)
            raw_x = base_r * math.cos(angle) + tremor_x
            raw_y = base_r * math.sin(angle) + tremor_y

            self.filt_x = 0.24 * raw_x + 0.76 * self.filt_x
            self.filt_y = 0.24 * raw_y + 0.76 * self.filt_y

            ai_x, ai_y = self.filt_x, self.filt_y
            ai_lat_us = 12.1 + random.uniform(-0.8, 1.1)
            hb_age_ms = 0.8 + random.uniform(-0.15, 0.25)

            if self.fault_mode == 1:  # FAULT_STARVE_AI
                ai_lat_us = 3420.0 + random.uniform(-180.0, 290.0)
                hb_age_ms = 18.4 + random.uniform(-1.1, 2.2)
                if self.state != 2:
                    self.state = 2
                    self.fault_reason = 1  # REASON_HEARTBEAT_MISS
                    self.missed_deadlines += 1
                elif self.seq % 8 == 0:
                    self.missed_deadlines += 1
            elif self.fault_mode == 2:  # FAULT_UNSAFE_OUTPUT
                ai_x = 25.4 * math.cos(angle)
                ai_y = 25.4 * math.sin(angle)
                if self.state != 2:
                    self.state = 2
                    self.fault_reason = 2  # REASON_BOUNDARY_BREACH
                self.missed_deadlines += 1

            r_prop = math.hypot(ai_x, ai_y)
            if self.state == 2 or r_prop > SAFE_RADIUS_MM:
                target_x = ai_x if self.fault_mode == 2 else self.safe_x
                target_y = ai_y if self.fault_mode == 2 else self.safe_y
                tr = math.hypot(target_x, target_y)
                if tr < 1e-6:
                    self.safe_x, self.safe_y = SAFE_RADIUS_MM, 0.0
                else:
                    self.safe_x = target_x * (SAFE_RADIUS_MM / tr)
                    self.safe_y = target_y * (SAFE_RADIUS_MM / tr)
                radius_mm = SAFE_RADIUS_MM
                offset_mm = 0.00
                tip_force_n = 0.00
            else:
                self.safe_x, self.safe_y = ai_x, ai_y
                radius_mm = r_prop
                offset_mm = max(0.0, SAFE_RADIUS_MM - radius_mm)
                tip_force_n = 0.12 + 0.02 * abs(math.sin(t * 2.1))

            react_us = 1.44 + random.uniform(-0.16, 0.24)
            self.react_window.append(react_us)
            if len(self.react_window) > 128:
                self.react_window.pop(0)
            sorted_r = sorted(self.react_window)
            p99_us = sorted_r[min(len(sorted_r) - 1, int(len(sorted_r) * 0.99))]

            jitter_signed_us = random.uniform(-0.68, 0.69)

            self.latest_frame = {
                "source": "LOCAL_TWIN_MIRROR",
                "qnx_live": False,
                "state": self.state,
                "fault_mode": self.fault_mode,
                "fault_reason": self.fault_reason,
                "seq": self.seq,
                "raw_x": round(raw_x, 2),
                "raw_y": round(raw_y, 2),
                "ai_x": round(ai_x, 2),
                "ai_y": round(ai_y, 2),
                "x": round(self.safe_x, 2),
                "y": round(self.safe_y, 2),
                "radius_mm": round(radius_mm, 2),
                "offset_mm": round(offset_mm, 2),
                "safe_radius_mm": SAFE_RADIUS_MM,
                "tip_force_n": round(tip_force_n, 2),
                "ai_latency_us": round(ai_lat_us, 2),
                "reaction_us": round(react_us, 2),
                "p99_reaction_us": round(p99_us, 2),
                "jitter_ns": int(react_us * 1000),
                "osc_sample_us": round(jitter_signed_us, 3),
                "p2p_jitter_us": 1.37,
                "rms_jitter_us": 0.23,
                "heartbeat_age_ms": round(hb_age_ms, 2),
                "missed_deadlines": self.missed_deadlines,
                "interventions": self.missed_deadlines,
                "bomb": 1 if self.fault_mode != 0 else 0,
                "sup_pid": 294931,
                "ai_pid": 307220,
                "resmgr_pid": 282642,
                "uptime_s": int(t),
            }


STATE = TelemetryState()


def qnx_poller_loop():
    buf = ""
    while True:
        try:
            sock = socket.create_connection((QNX_HOST, QNX_PORT), timeout=1.2)
            sock.settimeout(1.0)
            with STATE.lock:
                STATE.qnx_sock = sock
                STATE.qnx_connected = True
            print(f"[bridge_server] Connected to live QNX 8.0 target at {QNX_HOST}:{QNX_PORT}")

            while True:
                chunk = sock.recv(4096)
                if not chunk:
                    raise ConnectionError("QNX socket EOF")
                buf += chunk.decode("utf-8", errors="replace")
                while "\n" in buf:
                    line, buf = buf.split("\n", 1)
                    line = line.strip()
                    if not line.startswith("{"):
                        continue
                    try:
                        parsed = json.loads(line)
                        # Compute centered signed oscilloscope sample around 1.45 us mean
                        react = float(parsed.get("reaction_us", 1.45))
                        parsed["osc_sample_us"] = round(max(-1.95, min(1.95, react - 1.45)), 3)
                        parsed["qnx_live"] = True
                        parsed["source"] = "QNX_SDP_8.0_MICROKERNEL"
                        with STATE.lock:
                            STATE.latest_frame = parsed
                    except json.JSONDecodeError:
                        pass
        except OSError:
            with STATE.lock:
                STATE.qnx_connected = False
                STATE.qnx_sock = None
            STATE.step_local_mirror()
            time.sleep(0.033)


class CockpitHandler(SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=DASHBOARD_DIR, **kwargs)

    def log_message(self, fmt, *args):
        pass  # Keep console clean during real-time 30Hz polling

    def _send_json(self, payload: dict, code: int = 200):
        raw = json.dumps(payload).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)

    def do_GET(self):
        parsed = urlparse(self.path)
        if parsed.path == "/api/telemetry":
            if not STATE.qnx_connected:
                STATE.step_local_mirror()
            with STATE.lock:
                frame = dict(STATE.latest_frame)
            self._send_json(frame)
            return
        return super().do_GET()

    def do_POST(self):
        parsed = urlparse(self.path)
        length = int(self.headers.get("Content-Length", "0"))
        body_raw = self.rfile.read(length).decode("utf-8", errors="replace") if length > 0 else "{}"
        try:
            data = json.loads(body_raw)
        except json.JSONDecodeError:
            data = {"cmd": body_raw}

        if parsed.path in ("/api/fault", "/api/control", "/api/bomb"):
            cmd = str(data.get("cmd", "")).strip()
            if not cmd and "bomb" in data:
                cmd = "STARVE=1" if int(data["bomb"]) == 1 else "RESET"
            STATE.send_command(cmd)
            self._send_json({"ok": True, "applied": cmd})
            return

        self._send_json({"ok": False, "error": "unknown endpoint"}, 404)


def main():
    t = threading.Thread(target=qnx_poller_loop, daemon=True)
    t.start()
    server = ThreadingHTTPServer(("0.0.0.0", HTTP_PORT), CockpitHandler)
    print(f"[bridge_server] QNX Surgical Robotics Cockpit live at http://localhost:{HTTP_PORT}")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()