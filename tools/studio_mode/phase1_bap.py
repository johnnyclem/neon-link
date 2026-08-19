#!/usr/bin/env python3
"""Phase 1 B-AP: Link-only on the module SoftAP, no P5 rig.

Also: 100 pings for the idle-AP RTT spread the Phase 0 3-packet sample
could not dismiss, and a 30 min watch for macOS hopping off a
no-internet AP.
"""

from __future__ import annotations

import json
import os
import statistics
import subprocess
import sys
import time
import urllib.error
import urllib.request
from datetime import datetime, timezone

AP_SSID = os.environ.get("PHASE1_AP_SSID", "NEON-LINK-6BA0")
MODULE_LAN = os.environ.get("PHASE1_LAN", "192.168.50.252")
MODULE_AP = os.environ.get("PHASE1_AP_IP", "192.168.4.1")
WIFI_DEV = os.environ.get("PHASE1_WIFI_DEV", "en0")
RESTORE = [
    s
    for s in os.environ.get("PHASE1_RESTORE_SSID", "clemhaus,clemhaus_IoT").split(",")
    if s
]
MINUTES = float(os.environ.get("PHASE1_MINUTES", "30"))
PING_N = int(os.environ.get("PHASE1_PING_N", "100"))
OUT = os.environ.get(
    "PHASE1_OUT",
    os.path.join(os.path.dirname(__file__), "phase1_bap_results.json"),
)


def sh(args, timeout=40):
    return subprocess.run(args, capture_output=True, text=True, timeout=timeout)


def ifaddr():
    return sh(["ipconfig", "getifaddr", WIFI_DEV]).stdout.strip()


def join(ssid, password=""):
    return sh(
        ["networksetup", "-setairportnetwork", WIFI_DEV, ssid, password],
        timeout=45,
    )


def wait_ip(prefix, seconds):
    deadline = time.time() + seconds
    last = ""
    while time.time() < deadline:
        last = ifaddr()
        if last.startswith(prefix):
            return last
        time.sleep(1)
    return last


def http(host, method, path, body=None, timeout=8):
    data = None if body is None else json.dumps(body).encode()
    req = urllib.request.Request(
        f"http://{host}{path}",
        data=data,
        method=method,
        headers={"Content-Type": "application/json", "Host": host},
    )
    with urllib.request.urlopen(req, timeout=timeout) as r:
        raw = r.read().decode()
        return r.status, json.loads(raw) if raw else {}


def ping_stats(ip, n):
    r = sh(["ping", "-c", str(n), "-W", "1000", ip], timeout=n + 30)
    times = []
    for line in r.stdout.splitlines():
        if "time=" in line:
            try:
                times.append(float(line.split("time=")[1].split(" ")[0]))
            except (IndexError, ValueError):
                pass
    out = {
        "n": n,
        "ok": r.returncode == 0,
        "got": len(times),
        "loss_line": next(
            (ln for ln in r.stdout.splitlines() if "packet loss" in ln), ""
        ),
    }
    if times:
        out.update(
            {
                "min_ms": min(times),
                "avg_ms": statistics.mean(times),
                "max_ms": max(times),
                "stdev_ms": statistics.pstdev(times) if len(times) > 1 else 0.0,
                "spread": max(times) / min(times) if min(times) > 0 else None,
            }
        )
    return out, r.stdout[-1200:]


def restore():
    note = {"attempts": []}
    for ssid in RESTORE:
        msg = join(ssid)
        ip = wait_ip("192.168.50.", 20)
        note["attempts"].append({"ssid": ssid, "msg": (msg.stdout + msg.stderr).strip(), "ip": ip})
        if ip.startswith("192.168.50."):
            note["restored_ssid"] = ssid
            note["restored_ip"] = ip
            return note
    sh(["networksetup", "-setairportpower", WIFI_DEV, "off"])
    time.sleep(2)
    sh(["networksetup", "-setairportpower", WIFI_DEV, "on"])
    ip = wait_ip("192.168.50.", 25)
    note["attempts"].append({"ssid": "(toggle)", "ip": ip})
    note["restored_ip"] = ip
    return note


def main() -> int:
    result = {
        "started_utc": datetime.now(timezone.utc).isoformat(),
        "minutes": MINUTES,
        "samples": [],
        "macos_dropped": False,
        "status_drops": 0,
    }
    print("PUT ap.policy=always on LAN", flush=True)
    try:
        code, cfg = http(MODULE_LAN, "GET", "/api/config")
        retries = int((cfg.get("wifi") or {}).get("retries") or 3)
        body = {
            "ap": {"policy": "always"},
            "wifi": {"retries": retries + 1 if retries < 8 else retries - 1},
        }
        http(MODULE_LAN, "PUT", "/api/config", body)
        print("reboot", flush=True)
        try:
            http(MODULE_LAN, "POST", "/api/reboot", timeout=3)
        except Exception as e:
            print("reboot socket:", e, flush=True)
    except Exception as e:
        print("LAN put failed:", e, flush=True)
        result["fail"] = f"lan put: {e}"
        write(result)
        return 2

    time.sleep(8)
    print(f"joining {AP_SSID}", flush=True)
    ip = ""
    for attempt in range(5):
        join(AP_SSID, "")
        ip = wait_ip("192.168.4.", 15)
        print(f"  try {attempt+1} ip={ip}", flush=True)
        if ip.startswith("192.168.4."):
            break
    result["sta_ip"] = ip
    if not ip.startswith("192.168.4."):
        result["fail"] = "association/dhcp"
        result["restore"] = restore()
        write(result)
        return 3

    print(f"{PING_N} pings", flush=True)
    stats, raw = ping_stats(MODULE_AP, PING_N)
    result["ping"] = stats
    result["ping_tail"] = raw[-400:]
    print("ping", stats, flush=True)

    code, st = http(MODULE_AP, "GET", "/api/status")
    result["status0"] = {
        k: st.get(k) for k in ("peers", "bpm", "playing", "setup_ap", "ap_ssid", "ip")
    }
    print("status0", result["status0"], flush=True)

    deadline = time.time() + MINUTES * 60
    peers = []
    tempos = []
    while time.time() < deadline:
        try:
            code, st = http(MODULE_AP, "GET", "/api/status", timeout=5)
            sample = {
                "t": datetime.now(timezone.utc).isoformat(),
                "peers": st.get("peers"),
                "bpm": st.get("bpm"),
                "playing": st.get("playing"),
                "ifaddr": ifaddr(),
            }
            if isinstance(st.get("peers"), int):
                peers.append(st["peers"])
            if isinstance(st.get("bpm"), (int, float)):
                tempos.append(float(st["bpm"]))
            print(
                f"peers={st.get('peers')} bpm={st.get('bpm')} ip={sample['ifaddr']}",
                flush=True,
            )
            if not sample["ifaddr"].startswith("192.168.4."):
                result["macos_dropped"] = True
                join(AP_SSID, "")
                wait_ip("192.168.4.", 12)
                sample["rejoined"] = ifaddr()
        except Exception as e:
            result["status_drops"] += 1
            result["macos_dropped"] = True
            sample = {
                "t": datetime.now(timezone.utc).isoformat(),
                "err": str(e),
                "ifaddr": ifaddr(),
            }
            print("status lost", e, sample["ifaddr"], flush=True)
            if not sample["ifaddr"].startswith("192.168.4."):
                join(AP_SSID, "")
                wait_ip("192.168.4.", 12)
        result["samples"].append(sample)
        time.sleep(10)

    result["peer_min"] = min(peers) if peers else None
    result["peer_max"] = max(peers) if peers else None
    result["tempo_min"] = min(tempos) if tempos else None
    result["tempo_max"] = max(tempos) if tempos else None
    result["ended_utc"] = datetime.now(timezone.utc).isoformat()

    # Put the module back on fallback before we leave the AP.
    try:
        http(MODULE_AP, "PUT", "/api/config", {"ap": {"policy": "fallback"}})
        try:
            http(MODULE_AP, "POST", "/api/reboot", timeout=3)
        except Exception:
            pass
    except Exception as e:
        result["fallback_put"] = str(e)

    time.sleep(2)
    result["restore"] = restore()
    write(result)
    print(json.dumps({k: result[k] for k in result if k != "samples"}, indent=2))
    print("wrote", OUT, flush=True)
    return 0


def write(result):
    os.makedirs(os.path.dirname(OUT) or ".", exist_ok=True)
    with open(OUT, "w", encoding="utf-8") as f:
        json.dump(result, f, indent=2)
        f.write("\n")


if __name__ == "__main__":
    sys.exit(main())
