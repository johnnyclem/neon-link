# XIAO ESP32-C5 ESP-Hosted UART slave

**Abandoned.** The C5 Hosted UART path never synced. CrowPanel Advance
5.0" (`linksync-p4lcd`) uses the onboard C6 over SDIO for Wi-Fi and
Crowtail UART1 GPIO 47/48 for MIDI (DIP = UART). Do not flash this
image onto a C5 in that header expecting the P4 firmware to talk to it.

Coprocessor image for a **Seeed XIAO ESP32-C5** seated on the CrowPanel
Advance 5.0" XIAO header with the back DIP on **WM**. Historical: the P4
was meant to talk to this chip over UART1 (GPIO 47/48) instead of the
onboard C6 (2.4 GHz only).

Upstream example: `idf.py create-project-from-example "espressif/esp_hosted=2.12.9:slave"`
plus the XIAO overlay at the bottom of `sdkconfig.defaults.esp32c5`.

| C5 pad | GPIO | Hosted UART |
|--------|------|-------------|
| D6 TX  | 11   | slave TX → P4 RX (GPIO 48) |
| D7 RX  | 12   | slave RX ← P4 TX (GPIO 47) |
| USB-C  | —    | USB-Serial/JTAG console / flash |

Baud 921600, checksum on, no reset GPIO (XIAO `EN` is not on the 14-pin
header). After the P4 Hosted host comes up, tap **RST** on the XIAO if
the transport does not sync.

```bash
. ~/esp/esp-idf-v5.5.5/export.sh
cd tools/xiao-c5-hosted-uart
idf.py set-target esp32c5
idf.py build
idf.py -p /dev/cu.usbmodem2101 flash monitor
```

Flash **only** the C5 USB (`cu.usbmodem*`), never the CrowPanel CH343
(`cu.wchusbserial*`) and never the Waveshare RLCD.
