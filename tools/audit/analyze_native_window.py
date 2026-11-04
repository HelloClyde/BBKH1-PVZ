"""Recompute counters from a quiet H1 runtime-audit window; no device access."""
from pathlib import Path
import argparse
import hashlib
import json
import struct


def analyze(path):
    record = json.loads(path.read_text(encoding="utf8"))
    assert record["ok"]
    reads = [a for a in record["actions"] if a["action"]["kind"] == "read"]
    frames = [a for a in reads if a["action"]["bytes"] == 128]
    clocks = [a for a in reads if a["action"]["bytes"] == 24]
    assert len(frames) == len(clocks) == 2
    counters = [struct.unpack("<32I", bytes.fromhex(a["result"]["hex"])) for a in frames]
    ticks = [struct.unpack("<6I", bytes.fromhex(a["result"]["hex"])) for a in clocks]
    assert counters[0][0] == counters[1][0] == 0x50565031
    assert counters[0][7] == counters[1][7]
    assert ticks[0][4] == ticks[1][4] == 40
    snapshots = [a["result"] for a in record["actions"] if a["action"]["kind"] == "snapshot"]
    assert len(snapshots) == 2
    for s in snapshots:
        assert s["pvzh1_physical_screen_diagnostic"][:6] == [1, 480, 272, 1060, 600, 0]
        assert s["pvz_runtime_diagnostic"][1] == 2
        assert s["pvz_runtime_diagnostic"][12] == 0
    between = record["actions"][record["actions"].index(frames[0]) + 1:record["actions"].index(frames[1])]
    assert all(a["action"]["kind"] in ("read", "wait") for a in between)
    assert sum(a["action"].get("seconds", 0) for a in between) == 20

    def delta(name, index):
        return (snapshots[1][name][index] - snapshots[0][name][index]) & 0xffffffff

    elapsed = ((frames[1]["started_monotonic"] + frames[1]["finished_monotonic"])
               - (frames[0]["started_monotonic"] + frames[0]["finished_monotonic"])) / 2
    presented = (counters[1][2] - counters[0][2]) & 0xffffffff
    return {
        "scene": counters[0][7],
        "frames": presented,
        "PC_elapsed_seconds": elapsed,
        "FPS": presented / elapsed,
        "native_raw_ticks": (ticks[1][1] - ticks[0][1]) & 0xffffffff,
        "native_tick_hz": 40,
        "successful_file_opens_delta": delta("pvz_runtime_diagnostic", 5),
        "native_open_calls_delta": delta("pvz_runtime_io_diagnostic", 0),
        "native_open_failures_delta": delta("pvz_runtime_io_diagnostic", 2),
        "native_read_bytes_delta": delta("pvz_runtime_io_diagnostic", 5),
        "image_loads_delta": delta("pvzh1_cache_diagnostic", 2),
        "image_evictions_delta": delta("pvzh1_cache_diagnostic", 3),
        "allocation_pressure_callbacks_delta": delta("pvz_runtime_pressure_diagnostic", 0),
        "final_allocation_failures_delta": delta("pvz_runtime_pressure_diagnostic", 5),
        "raw_canvas_fallbacks_delta": delta("pvzh1_physical_screen_diagnostic", 5),
        "scope": "Physical H1; 20-second quiet wait, bounded endpoint reads. PC time is the FPS denominator; native clock stores last-frame timestamps and can lag at low FPS.",
        "result_sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
    }


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    args = parser.parse_args()
    print(json.dumps(analyze(args.input), indent=2))
