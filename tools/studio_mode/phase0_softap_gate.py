#!/usr/bin/env python3
"""Phase 0 Studio Mode multicast gate.

Joins the module SoftAP, confirms unicast (ping + /api/status), then
polls peer count / tempo for --minutes. Restores the previous Wi-Fi
network at the end so the session can come back.

This is discovery + session hold only. Not audio.
"""

from __future__ import annotations

import json
import os
import subprocess
import sys
import time
import urllib.error
import urllib.request
from datetime import datetime, timezone

AP_SSID = os.environ.get("PHASE0_AP_SSID", "NEON-LINK-6BA0")
AP_PASS = os.environ.get("PHASE0_AP_PASS", "")  # open
RESTORE_SSIDS = [
    s
    for s in os.environ.get("PHASE0_RESTORE_SSID", "clemhaus,clemhaus_IoT").split(",")
    if s
]
MODULE_IP = os.environ.get("PHASE0_MODULE_IP", "192.168.4.1")
WIFI_DEV = os.environ.get("PHASE0_WIFI_DEV", "en0")
MINUTES = float(os.environ.get("PHASE0_MINUTES", "10"))
POLL_S = float(os.environ.get("PHASE0_POLL_S", "10"))
OUT = os.environ.get(
    "PHASE0_OUT",
    os.path.join(os.path.dirname(__file__), "phase0_d1_results.json"),
)


def sh(args: list[str], timeout: float = 30) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        args, capture_output=True, text=True, timeout=timeout, check=False
    )


def wifi_ip() -> str:
    r = sh(["ipconfig", "getifaddr", WIFI_DEV])
    return r.stdout.strip()


def set_wifi(ssid: str, password: str | None = None) -> str:
    # Open networks: pass an empty password. Omitting it made macOS
    # return tmpErr (-528342014) on the first attempt this session.
    cmd = ["networksetup", "-setairportnetwork", WIFI_DEV, ssid]
    cmd.append("" if password is None else password)
    r = sh(cmd, timeout=45)
    return (r.stdout + r.stderr).strip()


def on_ap() -> bool:
    ip = wifi_ip()
    return ip.startswith("192.168.4.")


def toggle_wifi() -> None:
    sh(["networksetup", "-setairportpower", WIFI_DEV, "off"])
    time.sleep(2)
    sh(["networksetup", "-setairportpower", WIFI_DEV, "on"])


def wait_ip(prefix: str | None, seconds: float) -> str:
    deadline = time.time() + seconds
    last = ""
    while time.time() < deadline:
        last = wifi_ip()
        if last and (prefix is None or last.startswith(prefix)):
            return last
        time.sleep(1)
    return last


def ping_ok(ip: str) -> tuple[bool, str]:
    r = sh(["ping", "-c", "3", "-W", "1000", ip], timeout=12)
    out = (r.stdout + r.stderr).strip()
    return r.returncode == 0, out


def http_status(ip: str, timeout: float = 4.0) -> tuple[int, dict | str]:
    url = f"http://{ip}/api/status"
    try:
        with urllib.request.urlopen(url, timeout=timeout) as resp:
            body = resp.read().decode("utf-8", errors="replace")
            return resp.status, json.loads(body)
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode("utf-8", errors="replace")[:400]
    except Exception as e:  # noqa: BLE001 — bench script, record anything
        return 0, str(e)


def restore() -> dict:
    note = {"attempts": []}
    for ssid in RESTORE_SSIDS:
        msg = set_wifi(ssid)
        ip = wait_ip("192.168.50.", 20)
        note["attempts"].append({"ssid": ssid, "msg": msg, "ip": ip})
        if ip.startswith("192.168.50."):
            note["restored_ssid"] = ssid
            note["restored_ip"] = ip
            return note
    toggle_wifi()
    ip = wait_ip("192.168.50.", 25)
    note["attempts"].append({"ssid": "(toggle)", "ip": ip})
    note["restored_ip"] = ip
    return note


def main() -> int:
    started = datetime.now(timezone.utc).isoformat()
    result: dict = {
        "started_utc": started,
        "ap_ssid": AP_SSID,
        "ap_pass": "(none / open)" if not AP_PASS else "(set)",
        "module_ip": MODULE_IP,
        "minutes": MINUTES,
        "pre_ip": wifi_ip(),
        "samples": [],
        "macos_dropped": False,
        "association_ok": False,
        "unicast_ok": False,
    }

    print(f"joining {AP_SSID!r} (open={not AP_PASS})", flush=True)
    join_msgs = []
    ip = ""
    for attempt in range(4):
        join_msgs.append(set_wifi(AP_SSID, AP_PASS or None))
        ip = wait_ip("192.168.4.", 15)
        print(f"join attempt {attempt+1} ip={ip!r} msg={join_msgs[-1]!r}", flush=True)
        if ip.startswith("192.168.4."):
            break
        time.sleep(1)
    result["join_msg"] = join_msgs
    result["sta_ip"] = ip
    print(f"sta ip={ip!r}", flush=True)
    if not ip.startswith("192.168.4."):
        result["fail"] = "association/dhcp — never got 192.168.4.x"
        result["restore"] = restore()
        write(result)
        print(result["fail"], file=sys.stderr)
        return 2

    result["association_ok"] = True
    ok, ping_out = ping_ok(MODULE_IP)
    result["ping_ok"] = ok
    result["ping"] = ping_out[-800:]
    code, status = http_status(MODULE_IP)
    result["status_http"] = code
    result["status0"] = status
    print(f"ping_ok={ok} status_http={code}", flush=True)
    if not ok or code != 200:
        result["fail"] = "unicast failed — stop, not a multicast result"
        result["restore"] = restore()
        write(result)
        print(result["fail"], file=sys.stderr)
        return 3

    result["unicast_ok"] = True
    deadline = time.time() + MINUTES * 60
    last_peers = None
    tempos: list[float] = []
    peer_series: list[int] = []
    drops = 0
    while time.time() < deadline:
        t = time.time()
        code, status = http_status(MODULE_IP)
        sample = {"t": datetime.now(timezone.utc).isoformat(), "http": code}
        if code == 200 and isinstance(status, dict):
            peers = int(status.get("peers") or 0)
            bpm = float(status.get("bpm") or 0)
            playing = bool(status.get("playing"))
            sample.update(
                {
                    "peers": peers,
                    "bpm": bpm,
                    "playing": playing,
                    "ip": status.get("ip"),
                    "setup_ap": status.get("setup_ap"),
                    "ap_ssid": status.get("ap_ssid"),
                }
            )
            peer_series.append(peers)
            tempos.append(bpm)
            last_peers = peers
            print(
                f"t+{MINUTES*60 - (deadline - t):6.0f}s peers={peers} "
                f"bpm={bpm} playing={playing}",
                flush=True,
            )
        else:
            drops += 1
            sample["err"] = status
            sample["ifaddr"] = wifi_ip()
            result["macos_dropped"] = True
            print(f"status lost http={code} ip={sample['ifaddr']!r} err={status!r}", flush=True)
            # macOS likes to hop back to a known internet SSID. Rejoin so
            # the rest of the 10 minutes is still a multicast sample, and
            # count the hop as the captive-portal finding — not a Link fail.
            if not on_ap():
                set_wifi(AP_SSID, AP_PASS or None)
                wait_ip("192.168.4.", 12)
                sample["rejoined_ip"] = wifi_ip()
        result["samples"].append(sample)
        time.sleep(POLL_S)

    result["peer_min"] = min(peer_series) if peer_series else None
    result["peer_max"] = max(peer_series) if peer_series else None
    result["peer_last"] = last_peers
    result["peer_unique"] = sorted(set(peer_series))
    result["tempo_min"] = min(tempos) if tempos else None
    result["tempo_max"] = max(tempos) if tempos else None
    result["tempo_changed"] = (
        bool(tempos) and max(tempos) - min(tempos) >= 0.05
    )
    result["status_drops"] = drops
    result["ended_utc"] = datetime.now(timezone.utc).isoformat()
    result["restore"] = restore()
    write(result)
    print(json.dumps({k: result[k] for k in result if k != "samples"}, indent=2))
    print(f"wrote {OUT}", flush=True)
    return 0


def write(result: dict) -> None:
    os.makedirs(os.path.dirname(OUT) or ".", exist_ok=True)
    with open(OUT, "w", encoding="utf-8") as f:
        json.dump(result, f, indent=2)
        f.write("\n")


if __name__ == "__main__":
    sys.exit(main())
