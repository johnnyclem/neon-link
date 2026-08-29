# Lessons: debugging gotchas & dead-ends

Hard-won during the 2026-08-27 MaTouch session
([session](../sessions/2026-08-27-matouch-settings-midi-wifi.md)). The point
of this page is to not repeat the wrong theories.

## Meta-lesson: get the backtrace before theorizing
One crash ("reboots") was chased through **five wrong hypotheses** over many
reflashes before a single `idf.py monitor` backtrace named it in seconds. On
an ESP32, a reboot is a panic or watchdog — the decoded backtrace (with the
ELF present, `idf.py monitor` translates it to `file:line`) is the answer.
Ask for it *first*. Distinguish `Guru Meditation` (code) from
`Brownout detector was triggered` (power) from `assert failed:` immediately.

## The actual bug vs. the wrong theories
Symptoms reported over time: "MIDI out reboots", "test note reboots",
"encoder click crashes regardless of the M5 unit", "boots then freezes then
restarts". They were **all the same Wi-Fi bug**: the STA-fail → setup-AP
fallback double-added the AP netif and asserted ~16 s after boot, coincident
with whatever the user happened to touch. See
[wifi-provisioning](../concepts/wifi-provisioning.md) bug 2.

Wrong theories that wasted time (record so we recognize them faster):
1. **Brownout** from the M5 MIDI unit's current loop on the 3.3 V rail.
   Plausible (works on P4, crashes on MaTouch; IN works, OUT crashes) — but
   it crashed with *nothing connected*, killing it.
2. **`UART_FIFO_AHB_REG` wrong on S3.** The macro *does* differ from P4
   (`REG_UART_AHB_BASE` vs `REG_UART_BASE`), but on S3 both resolve to the
   same address (`0x60000000 + i*0x10000`). Verified by reading `reg_base.h`
   / `soc.h` — dead end. **Check the arithmetic before trusting a diff.**
3. **Driver-TX vs ISR-FIFO mixing** (test note used `uart_write_bytes`, clock
   used direct-FIFO). A real latent hazard, but not this crash.
4. **RX polling racing the TX ISR** (the MIDI-in `uart_read_bytes` per frame).
   Led to reverting good MIDI work. Not the crash — it was WiFi.
5. **A regression in my own recent commits.** It was pre-existing, exposed
   only once the password got poisoned.

The "works on P4 / crashes on MaTouch, same adapter, same 3.3 V" data point
was the key that (eventually) pointed at *native-vs-Hosted firmware*, not
hardware.

## Self-inflicted: don't log from a real-time task
Added a 3 s MIDI heartbeat `ESP_LOGI` inside `pulse_task`
(`configMAX_PRIORITIES - 2`, core 1). Fine with a monitor attached (USB
drained); with **USB power but no reader**, the console back-pressured, the
RT task stalled, and the watchdog rebooted — a boot loop that only happens
headless. **Never `ESP_LOGI` on a repeating path in a high-priority task**;
if you must instrument, expose a counter and read it from a low-priority
path.

## Config persistence bites
`neon_config_apply()` persists **immediately** on a network-identity change.
Applying a *candidate* value (a wifi password being tried) therefore writes
it to flash before it's known good. Use `neon_config_apply_ram()` for trials;
persist only on success. See [wifi-provisioning](../concepts/wifi-provisioning.md).

## NVS survives a normal reflash
`erase-flash` is required to clear a poisoned stored value (bad wifi cred).
The flash script does not erase `nvs` (0x9000).

## Merges can be clean yet semantically wrong
Squash-merging onto a diverged `main` (parallel MIDI-PLL work) reported "no
conflicts", but shared files (`Config` struct, `menu_model.cpp`) were the
risk. Verified: config version reconciled (v8→v9), `kMidiMaster` preserved,
`ClockSource` menu wrap unchanged (both sides had `wrap_int(s,3)` — main
doesn't menu-cycle `kMidiMaster` by design), **host tests pass + firmware
builds**. Always build+test a "clean" auto-merge of shared subsystems.

## Hardware truths from this session
- MaTouch console is native USB Serial/JTAG ⇒ UART0 pins 43/44 free for MIDI.
- MIDI DIN OUT is a ~5 V current loop; 3.3 V can be too weak for the receiver
  opto even if the sender's LED lights.
- M5 Unit-MIDI OUTPUT switch: **SEPARATE** = board clock → DIN OUT,
  **BYPASS** = external DIN IN → board RX. (See
  [midi-clock-path](../concepts/midi-clock-path.md).)
