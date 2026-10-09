# Marauder headless — manual testing guide

Hands-on test plan for the headless ESP32 Marauder (GENERIC_ESP32 build, no TFT)
driven over USB-serial through the `marauder` console wrapper, with an I2C OLED
status readout.

Everything here is from this build's live `help` + what was actually verified on
the board. Commands not confirmed present in this build are **not** listed.

> **Transmitting vs passive — read first.** Scanning and all `sniff*` commands are
> **passive receive only** (safe, legal to run anywhere). A handful of commands
> **transmit** (deauth, beacon/BLE spam, karma, evil portal, MAC spoofing). Only
> ever run the transmitting ones against **your own APs and devices**, ideally in
> RF isolation. They are marked **[TX]** throughout.

---

## 1. Launch, connect, read the OLED

### Launch
From **any** directory:
```bash
marauder
```
This is a global launcher (`~/.local/bin/marauder`) that runs the wrapper out of
its own venv — no `cd`, no activate. Args pass through:
```bash
marauder --palette           # print the grouped command palette and exit
marauder -p /dev/ttyUSB0     # use a different port
marauder --no-log            # don't write a session log
```
Default port is `/dev/ttyUSB2` at 115200. If you don't know the port:
`arduino-cli board list` (ours enumerated as `/dev/ttyUSB2`).

> **One owner only.** The serial port is exclusive. Close any `screen`/`picocom`/
> `minicom` on it first (screen: `Ctrl-A` then `K`). The wrapper refuses cleanly
> with `Device or resource busy` rather than fighting for it.

At the `marauder> ` prompt: type a Marauder command directly, or a `/meta` command
(below). `/` or `/help` shows the palette; plain `help` passes through to the
board's own raw help list.

### Read the OLED (5 lines)
```
ESP32 Marauder |     L1: name + heartbeat (rotates | / - \ ~4x/s = firmware alive)
Beacon Sniff         L2: current mode (from WiFiScan.currentScanMode)
RUNNING              L3: RUNNING while a scan/sniff runs, else STOPPED
Pkts: 284            L4: frames captured THIS scan (resets to 0 at each scan start)
Ch:7  Up:3m12s       L5: current channel + uptime
```
- Boot prints `[OLED] device found at 0x3C (driver: SSD1306 ...)` — confirms the
  panel on the I2C bus (SDA=21, SCL=22).
- L2/L3 update live as you start/stop scans. L4 `Pkts` climbs in every capture
  mode and resets when you start a new scan. The OLED redraws ~3–4x/sec, so `Pkts`
  may lag the live frame count by a few — that's expected for a status display.

---

## 2. Wrapper controls

| Input | Effect |
|---|---|
| `/` or `/help` or `?` | Show the grouped command palette ([DEF]/[OFF] tags) |
| `/table` (default) | Aggregate the high-rate frame feed into a live table (one row per BSSID/MAC: RSSI, Ch, MAC, ESSID, seen-count), bottom of screen, refreshed a few times/sec |
| `/raw` | Fall back to the unfiltered line stream (every frame scrolls) |
| `/sort rssi` / `/sort last` | Table order: strongest RSSI first (default), or most-recently-seen |
| `/stop` | Send `stopscan` and clear the table |
| `Ctrl-C` | Same as `/stop` (halts the board), **does not quit the session** |
| `help` | Board's own raw command list (passthrough) |
| `/log` | Print the current session log path |
| `/quit` or `Ctrl-D` | Exit the wrapper |

- **The raw stream is written to `logs/marauder-<timestamp>.log` in both `/table`
  and `/raw` modes** — nothing is lost.
- **Non-frame output always prints above the prompt** in both modes: command
  replies, `list -a`/`list -c` results, detection lines, boot text. Only the
  repeating `-RSSI Ch:N MAC ESSID` frame feed is aggregated by `/table`.

Quick check: start `scanall`, watch rows accumulate and seen-counts rise in the
table; `/raw` → the same frames scroll; `/table` → back to the table; `/stop` →
table clears, OLED returns to `Idle` / `STOPPED`.

---

## 3. Feature groups — command, CLI, OLED, success

For each: type the command at `marauder>`. Unless noted, `/stop` (or Ctrl-C) ends
it. All of Scan + Sniff are **passive RX (safe)**.

### Scan / discover  (passive)
| Command | CLI shows | OLED | Success |
|---|---|---|---|
| `scanall` | `/table`: rows per AP+station, counts rising | `Scan All` / `RUNNING` / Pkts↑ | Real ESSIDs/BSSIDs appear within seconds |
| `list -a` | `[i][CH:n] ESSID RSSI` per AP | (unchanged) | Lists the APs scanall found |
| `list -c` | stations (client MACs) grouped under each AP | — | Client MACs listed |
| `recon wifi` | automated recon status lines | `Active`/`RUNNING` | Runs without error |
| `channel -s 6` | `Set channel: 6` | L5 `Ch:6` | Channel changes |
| `info -a 0` | detailed AP info for index 0 | — | Prints details (after a scan) |
| `packetcount` | packet-rate output | `Active`/`RUNNING` | Counts packets |
| `select -a 0` then `list -a` | AP 0 marked `(selected)` | — | Target selection works |

### Sniff — passive capture  (safe)
| Command | CLI (`/table`) | OLED L2 | Success |
|---|---|---|---|
| `sniffbeacon` | APs aggregate into rows | `Beacon Sniff` | Rows fill, Pkts climbs |
| `sniffprobe` | client probe requests / directed SSIDs | `Probe Sniff` | Probe entries appear |
| `sniffraw` | all frames | `Raw Sniff` | Heavy frame flow, Pkts climbs fast |
| `sniffpmkid -c 6` | PMKID capture status | `PMKID/EAPOL` | Runs on channel 6 |
| `sniffsae` | WPA3 SAE commit frames | `Active` | Runs |

Add `-serial` to any sniff (e.g. `sniffraw -serial`) to also stream raw pcap bytes
over USB — those are written to the log, not the table.

### Sniff — defensive / detection  → **see section 4** (two-device loops)
`sniffdeauth`, `sniffpwn`, `sniffpinescan`, `sniffmultissid`, `sniffskim`.

### BLE
| Command | Notes | OLED | TX? |
|---|---|---|---|
| `sniffbt -t airtag` | BLE device sniff (airtag/ibeacon/flipper/flock/meta) | `BLE Sniff`/`Active` | passive |
| `findmy -t 0` | track an Apple Find My device | `Active` | passive |
| `remoteid scan` | drone Remote ID scan/list/track/clear | `Active` | passive |
| `blespam -t google` | **[TX]** BLE advertisement spam | `BLE Spam` | **transmits** |

### Attack — **OFFENSIVE [TX]** (own gear only)
| Command | Notes |
|---|---|
| `attack -t deauth` | **[TX]** deauth selected AP(s) — `select -a <i>` first |
| `attack -t beacon` | **[TX]** beacon-spam SSIDs from the SSID list |
| `attack -t probe` / `rickroll` / `csa` / `sae` / `badmsg` | **[TX]** various |
| `karma -p 0` | **[TX]** answer probe requests as bait |
| `evilportal -c start -w portal.html` | **[TX]** captive portal (needs HTML on SD) |
| `spoofat -t 0` | **[TX]** spoof a scanned AP |

### MAC-spoof / identity — **[TX]/own-radio** (useful as detector test traffic)
`randapmac`, `randstamac`, `cloneapmac -a 0`, `clonestamac -s 0`. These change your
own radio's MAC; harmless to your gear, but they alter what you emit.

### Target lists / session
`ssid -a -g 20` (generate 20 random SSIDs), `ssid -n Name`, `add -a -b <mac>`,
`save -a` / `load -a`, `clearlist -a`, `join -a <i> -p <pw>`.

### Network recon  (only after `join`)
`pingscan`, `arpscan -f`, `portscan -s http` — require you to have joined an AP with
`join` first.

### SD / files  (SD on CS=5/SCK=18/MISO=19/MOSI=23)
`ls /` (should list `SCRIPTS`), `backupspiffs`, `backupstatus`, `restorespiffs`,
`update -s` / `update -w`, `upload -d wigle`.

### Settings / control
`settings -s SavePCAP disable` (toggle pcap capture), `settings -r` (reset),
`reboot`, `protocolinfo`, `geofence list`, `stopscan`.

---

## 4. Defensive / detection — full two-device test loops

Detection needs something to detect. For each, the **listen** side is this Marauder
(passive, safe); the **trigger** side must be **your own gear**. Where you can't
realistically generate the trigger at home, that's stated plainly with the expected
output instead of a fake positive.

A detection line prints **above the prompt** (it isn't aggregated into the table),
so watch the scrollback.

### 4.1 `sniffdeauth` — detect deauth/disassoc attacks  ✅ self-testable
- **Listen (safe):** `sniffdeauth` → OLED `Deauth Detect` / `RUNNING`.
- **Trigger [TX, own AP only]:** you need a *second* radio — one board can't attack
  and sniff at once. On a **second ESP32/Marauder**, select your own test AP and run
  `attack -t deauth`. (Any deauth tool you own, aimed at your own AP, works too.)
- **Can't do it with one board** — note this honestly; if you only have this one
  board, you can confirm the listener runs but not produce a live positive.
- **Positive detection prints:**
  ```
  -45 Ch: 6 aa:bb:cc:dd:ee:ff -> 11:22:33:44:55:66
  ```
  (rssi, channel, source MAC → destination MAC of each deauth/disassoc frame).
- **Safe?** Listening is passive. The trigger **transmits** — your own AP only.

### 4.2 `sniffmultissid` — detect karma / multi-SSID (evil-twin) APs  ✅ self-testable
- **Listen (safe):** `sniffmultissid` → OLED `MultiSSID Det` / `RUNNING`, Pkts climbs.
- **Trigger [TX, own gear]:** on a **second Marauder**, generate many SSIDs from one
  radio: `ssid -a -g 30` then `attack -t beacon`. That's exactly the multi-SSID/
  karma pattern this detects.
- **Positive detection prints:**
  ```
  MAC: aa:bb:cc:dd:ee:ff ...
  ```
  (a confirmed multi-SSID source MAC, one line per confirmed AP).
- **Safe?** Listening passive. The beacon-spam trigger **transmits** — own gear only.

### 4.3 `sniffpwn` — detect Pwnagotchi devices  ⚠️ needs a Pwnagotchi
- **Listen (safe):** `sniffpwn` → OLED `Pwn Detect` / `RUNNING`.
- **Trigger:** a real **Pwnagotchi** (or a second ESP32 crafting a Pwnagotchi
  beacon with the `{"name":...,"pwnd_tot":...}` payload). **Most people don't have
  one at home** — if you don't, you can't produce a real positive; confirm the
  listener runs and move on.
- **Positive detection WOULD print:**
  ```
  Name: <pwnagotchi-name>
  Pwnd #: <count>
  ```
- **Safe?** Passive listen. (A crafted-beacon trigger would transmit — own gear.)

### 4.4 `sniffpinescan` — detect WiFi Pineapple  ⚠️ needs a Pineapple
- **Listen (safe):** `sniffpinescan` → OLED `Pineapple Det` / `RUNNING`.
- **Trigger:** a **WiFi Pineapple**. **Not realistic to fabricate at home** — don't
  expect a positive without one; confirm the listener runs.
- **Positive detection WOULD print:**
  ```
  MAC: aa:bb:cc:dd:ee:ff ...
  ```
  (a confirmed Pineapple source MAC).
- **Safe?** Passive listen.

### 4.5 `sniffskim` — detect BLE credit-card skimmers  ◑ partially self-testable
- **Listen (safe):** `sniffskim` → OLED `Skimmer Det` / `RUNNING`.
- **Trigger [own gear]:** skimmers advertise as BLE modules named **HC-03 / HC-05 /
  HC-06**. If you own a common **HC-05 or HC-06** Bluetooth module (frequent Arduino
  part), power it on nearby and it will appear. A real card skimmer is **not**
  something to source — don't try.
- **What prints (headless):** one line per BLE device seen — `<name><rssi>` or
  `<address><rssi>`, e.g. `HC-05-42`. **Honest caveat:** the explicit "SKIMMER"
  flag label is screen-only (HAS_SCREEN); headless, you see the device **name** and
  match `HC-03/05/06` yourself. If you power an HC-05, seeing `HC-05` in the list is
  your positive.
- **Safe?** Passive listen (BLE scan).

**Summary — safe to self-test vs transmits:**
- Pure passive, run anywhere: all five **listen** commands, plus all scan/sniff.
- **Transmitting triggers (own AP/devices only):** `attack -t deauth` (4.1),
  `attack -t beacon` (4.2), a crafted Pwnagotchi beacon (4.3). Powering your own
  HC-05 (4.5) is just a BLE device advertising — fine.

---

## 5. Known-inert — don't chase these

Verified dead/non-functional in this headless build (so you don't waste time):
- **Buttons (GPIO 25/26/27):** wired and configured, but **nothing reads them** —
  every consumer lives in the `HAS_SCREEN` menu code, which is compiled out. They do
  nothing at runtime; drive everything from the serial CLI.
- **`gps`, `gpsdata`, `nmea`, `gpspoi`, `gpstracker`:** names appear in `help` but
  the bodies are `#ifdef HAS_GPS` (off) → no-ops. (No GPS module anyway.)
- **`led`:** `#ifdef HAS_NEOPIXEL_LED` (off) → no-op.
- **`brightness`:** drives a TFT backlight that doesn't exist → harmless no-op.

The wrapper's palette already excludes these; if you type one anyway it warns and
sends it, and the board just no-ops.

---

## 6. Troubleshooting (things we actually hit)

| Symptom | Cause / fix |
|---|---|
| `could not open /dev/ttyUSB2: Device or resource busy` | A `screen`/`picocom`/`minicom` still owns the port. Close it (screen: `Ctrl-A` then `K`), then relaunch `marauder`. |
| `Permission denied` on the port | Your user must be in the `uucp` group (you are). Check `ls -l /dev/ttyUSB2` → `root uucp`; if you were removed, `sudo usermod -aG uucp $USER` then re-login. |
| Wrong port / not `ttyUSB2` | `arduino-cli board list` to find it, then `marauder -p /dev/ttyUSBx`. |
| OLED blank | Check boot prints `[OLED] device found at 0x3C`. If "no I2C device", it's wiring/power (SDA=21, SCL=22, 3V3, GND), not code. |
| OLED image shifted ~2px / wrapped | Panel is an **SH1106**, not SSD1306 — set `#define OLED_SH1106` in configs.h (GENERIC_ESP32 block) and reflash. (Ours is SSD1306; no flip needed.) |
| OLED froze on a scan (old symptom) | Was 400 kHz I2C going marginal under WiFi load; already fixed by running the bus at 100 kHz. If it recurs, suspect OLED power/wiring under radio load. |
| `Pkts` looks a few behind the live count | Expected — the OLED redraws ~3–4x/sec, a few frames behind. Not a bug. |
| `marauder` not found | `~/.local/bin` must be on PATH (it is). If a shell can't find it: `export PATH="$HOME/.local/bin:$PATH"` (add to `~/.bashrc`). |
| Table shows nothing during a sniff | You're in `/raw` mode (frames scroll) — type `/table`. Or no frames are being heard yet. |

---

## 7. Quick smoke test (2 minutes)

```
marauder                 # from anywhere; OLED shows Idle / STOPPED
scanall                  # table fills with real APs; OLED Scan All / RUNNING / Pkts↑
/sort last               # table reorders by most-recently-seen
/raw                     # frames scroll raw
/table                   # back to aggregated table
/stop                    # table clears; OLED Idle / STOPPED
sniffbeacon              # OLED Beacon Sniff / RUNNING; Pkts climbs from 0
Ctrl-C                   # stopscan + clear (session stays open)
ls /                     # SD lists SCRIPTS (confirms card mounted)
/quit                    # exit
```
If all of that behaves as described, the project is working end to end.
