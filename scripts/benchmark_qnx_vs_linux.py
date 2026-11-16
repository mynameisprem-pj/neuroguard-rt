#!/usr/bin/env python3
"""
NeuroGuard-RT: Automated Reaction-Time & Jitter Benchmark Logger
Profiles QNX Priority-250f Safety Supervisor reaction times (Latest + p99),
Peak-to-Peak / RMS watchdog jitter, and R <= 18.00 mm clamping accuracy across
NORMAL, STARVE_AI, and UNSAFE_OUTPUT modes, logging raw samples to CSV.
"""

import csv
import json
import os
import time
import urllib.request

BASE_URL = os.environ.get("COCKPIT_URL", "http://127.0.0.1:3000")
SAMPLES_PER_PHASE = 35
SAMPLE_INTERVAL_S = 0.045  # ~22 Hz sampling


def post_cmd(cmd: str):
    req = urllib.request.Request(
        f"{BASE_URL}/api/fault",
        data=json.dumps({"cmd": cmd}).encode("utf-8"),
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    with urllib.request.urlopen(req, timeout=2.0) as resp:
        return json.loads(resp.read().decode("utf-8"))


def get_telemetry() -> dict:
    with urllib.request.urlopen(f"{BASE_URL}/api/telemetry", timeout=2.0) as resp:
        return json.loads(resp.read().decode("utf-8"))


def measure_host_os_jitter_us(iterations: int = 80) -> float:
    """Measure non-RT host OS 1ms sleep wakeup jitter (p99 in microseconds)."""
    target_ns = 1_000_000  # 1.0 ms
    errors_us = []
    for _ in range(iterations):
        t0 = time.perf_counter_ns()
        time.sleep(0.001)
        elapsed_ns = time.perf_counter_ns() - t0
        errors_us.append(abs(elapsed_ns - target_ns) / 1000.0)
    errors_us.sort()
    return errors_us[min(len(errors_us) - 1, int(len(errors_us) * 0.99))]


def run_phase(phase_name: str, setup_cmd: str, csv_rows: list) -> dict:
    post_cmd(setup_cmd)
    time.sleep(0.25)  # Allow state latch transition

    react_samples = []
    p99_samples = []
    ai_lat_samples = []
    radii = []
    p2p_vals = []
    rms_vals = []
    missed_end = 0
    src = "UNKNOWN"

    for i in range(SAMPLES_PER_PHASE):
        d = get_telemetry()
        src = d.get("source", "UNKNOWN")
        react_samples.append(float(d.get("reaction_us", 0.0)))
        p99_samples.append(float(d.get("p99_reaction_us", 0.0)))
        ai_lat_samples.append(float(d.get("ai_latency_us", 0.0)))
        radii.append(float(d.get("radius_mm", 0.0)))
        p2p_vals.append(float(d.get("p2p_jitter_us", 0.0)))
        rms_vals.append(float(d.get("rms_jitter_us", 0.0)))
        missed_end = int(d.get("missed_deadlines", 0))

        csv_rows.append({
            "timestamp": round(time.time(), 4),
            "phase": phase_name,
            "source": src,
            "state": d.get("state", 0),
            "ai_latency_us": d.get("ai_latency_us", 0.0),
            "reaction_us": d.get("reaction_us", 0.0),
            "p99_reaction_us": d.get("p99_reaction_us", 0.0),
            "p2p_jitter_us": d.get("p2p_jitter_us", 0.0),
            "rms_jitter_us": d.get("rms_jitter_us", 0.0),
            "radius_mm": d.get("radius_mm", 0.0),
            "offset_mm": d.get("offset_mm", 0.0),
            "missed_deadlines": missed_end,
        })
        time.sleep(SAMPLE_INTERVAL_S)

    react_samples.sort()
    ai_lat_samples.sort()
    return {
        "phase": phase_name,
        "source": src,
        "react_med": react_samples[len(react_samples) // 2],
        "react_p99": max(p99_samples),
        "ai_p99": ai_lat_samples[int(len(ai_lat_samples) * 0.95)],
        "max_radius": max(radii),
        "p2p_us": sum(p2p_vals) / len(p2p_vals),
        "rms_us": sum(rms_vals) / len(rms_vals),
        "missed": missed_end,
        "host_p99_us": measure_host_os_jitter_us(),
    }


def main():
    print(f"[benchmark] Connecting to Surgical Robotics Cockpit at {BASE_URL} ...")
    csv_rows = []

    p_nom = run_phase("1. NORMAL (1kHz AI)", "RESET", csv_rows)
    p_stv = run_phase("2. FAULT: STARVE AI (Prio 35r)", "STARVE=1", csv_rows)
    post_cmd("RESET")
    time.sleep(0.2)
    p_uns = run_phase("3. FAULT: UNSAFE AI (R=25.4mm)", "UNSAFE=1", csv_rows)
    post_cmd("RESET")

    os.makedirs("logs", exist_ok=True)
    csv_path = os.path.join("logs", "reaction_time_profile.csv")
    with open(csv_path, "w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=list(csv_rows[0].keys()))
        writer.writeheader()
        writer.writerows(csv_rows)

    print(f"\nTelemetry Source: {p_nom['source']}")
    print(f"Saved {len(csv_rows)} raw hardware timing samples -> {csv_path}\n")

    print("| Phase | AI Latency (p95) | QNX 250f Reaction (Latest / p99) | Host OS Jitter (p99) | Max Tool Radius | Safe Envelope (<=18.00mm) |")
    print("| :--- | :--- | :--- | :--- | :--- | :--- |")
    for r in (p_nom, p_stv, p_uns):
        passed = "PASS (Clamped 18.00mm)" if r["max_radius"] <= 18.01 and "FAULT" in r["phase"] else (
                 "PASS (Inside Zone)" if r["max_radius"] <= 18.01 else "BREACH!")
        print(
            f"| {r['phase']} | {r['ai_p99']:.2f} us | "
            f"{r['react_med']:.2f} us / {r['react_p99']:.2f} us | "
            f"{r['host_p99_us']:.1f} us | {r['max_radius']:.2f} mm | {passed} |"
        )
    print(f"\n1kHz Hardware Watchdog Stability: Peak-to-Peak = {p_nom['p2p_us']:.2f} us | RMS Jitter = {p_nom['rms_us']:.2f} us\n")


if __name__ == "__main__":
    main()