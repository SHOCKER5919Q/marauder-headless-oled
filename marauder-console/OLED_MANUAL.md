# Marauder OLED Menu — Manual

The headless ESP32 Marauder, driven entirely from its **128×64 OLED + 3 buttons**.
Every menu item fires the real firmware command (`cli_obj.runCommand(...)`) — the
OLED is just a front-end over the CLI.

## Buttons
| Button | GPIO | Action |
|---|---|---|
| **UP**     | 25 | move highlight up |
| **DOWN**   | 27 | move highlight down |
| **SELECT** | 26 | **short press** = enter / confirm · **hold ~0.5 s** = Back |

## Screen flow
`Boot splash → HOME dashboard → MENU (10 categories) → feature list → run`.
- **TX / identity items** show a **CONFIRM** screen first (UP/DOWN toggles
  START/CANCEL, SELECT activates).
- **Scans/sniffs/attacks** jump to a **RUNNING** screen (SELECT = stop).
- **Target commands** (deauth, info, clone, karma, foxhunt) open the **AP LIST**
  to pick a target first.
- **set channel** opens a number picker (UP/DN 1–14, SELECT sets).
- Row marker: **⚠** = transmits / changes identity (offensive). Dot = passive.

## HOME dashboard
Live: current **CH**, **RUN/IDLE**, **AP/STA** counts, **Pkts/s**, **BLE** device
count, and an animated packets-per-second **bar-graph**. SELECT opens the menu.
(No clock/battery — this board has no RTC or battery gauge; uptime/graph instead.)

---

## 1. Scan / Discover — *passive, safe*
| Option | Starts | What it does |
|---|---|---|
| scanall      | `scanall`        | Scans APs **and** stations at once; fills the list, graph moves |
| recon wifi   | `recon wifi`     | Automated Wi-Fi recon mission |
| recon ble    | `recon ble`      | Automated BLE recon mission |
| AP list view | *(opens list)*   | Scroll scanned APs with RSSI + signal bars |
| select AP    | `select -a <i>`  | Marks an AP as the target for later attacks |
| info AP      | `info -a <i>`    | Prints that AP's details **to serial** |
| mactrack     | `mactrack`       | Tracks specific MAC addresses |
| packetcount  | `packetcount`    | Live packet-rate counter |
| set channel  | `channel -s <n>` | Lock the radio to a channel (picker) |
| foxhunt AP   | `foxhunt -w <i>` | RSSI direction-finding toward a chosen AP |

## 2. Defensive Detect — *passive detection*
| Option | Starts | Detects / prints |
|---|---|---|
| sniffdeauth    | `sniffdeauth`    | Deauth/disassoc attacks → `-rssi Ch:n src -> dst` |
| sniffpwn       | `sniffpwn`       | Pwnagotchi → `Name: … / Pwnd #: …` |
| sniffpinescan  | `sniffpinescan`  | Wi-Fi Pineapple → `MAC: …` |
| sniffmultissid | `sniffmultissid` | Karma / multi-SSID evil-twin → `MAC: …` |
| sniffskim      | `sniffskim`      | BLE skimmers (HC-03/05/06) → name + RSSI |
| remoteid scan  | `remoteid scan`  | Drone Remote-ID beacons |

## 3. Passive Sniff — *capture, safe*
Each starts the sniffer, OLED shows the mode + Pkts climbing; frames stream over
serial and to SD pcap.
| Option | Starts | Captures |
|---|---|---|
| sniffbeacon | `sniffbeacon` | Beacon frames (AP discovery/fingerprinting) |
| sniffprobe  | `sniffprobe`  | Client probe requests (device fingerprinting) |
| sniffraw    | `sniffraw`    | All 802.11 frames |
| sniffsae    | `sniffsae`    | WPA3 SAE commit frames |
| sniffpmkid  | `sniffpmkid`  | PMKID handshake material |

## 4. Attack / Offensive — *TX, own gear only (each confirms)*
| Option | Starts | What it does |
|---|---|---|
| deauth (target)  | `select -a <i>` → `attack -t deauth` | Deauthenticates clients of the chosen AP |
| beacon spam list | `attack -t beacon -l` | Broadcasts beacons for your SSID list |
| beacon spam rand | `attack -t beacon -r` | Broadcasts random fake SSIDs |
| probe flood      | `attack -t probe`     | Floods probe-request frames |
| rickroll         | `attack -t rickroll`  | Spams rickroll SSIDs |
| evil portal      | `evilportal -c start` | Starts a captive portal (needs HTML on SD) |
| karma (target)   | `karma -p <i>`        | Answers probe requests as bait for a target |

## 5. BLE Tools
| Option | Starts | What it does |
|---|---|---|
| sniffbt airtag  | `sniffbt -t airtag`  | Sniff BLE AirTags |
| sniffbt flipper | `sniffbt -t flipper` | Sniff Flipper Zero devices |
| blespam all     | `blespam -t all`     | **TX** BLE advertisement spam |
| findmy (tag)    | `findmy -t <i>`      | Track an Apple Find My device (needs AirTag scan) |
| spoofat (tag)   | `spoofat -t <i>`     | **TX** Spoof a scanned **AirTag** (needs AirTag scan) |

## 6. MAC-Spoof / Identity — *changes your identity (confirms)*
| Option | Starts | What it does |
|---|---|---|
| randapmac   | `randapmac`       | Randomize your SoftAP MAC → `Setting AP MAC: …` |
| randstamac  | `randstamac`      | Randomize your station MAC → `Setting STA MAC: …` |
| cloneapmac  | `cloneapmac -a <i>` | Clone a scanned AP's MAC as your AP MAC |
| clonestamac | *serial only*     | Needs a station index (`list -c`) — use the serial CLI |

## 7. Target / Session
| Option | Starts | What it does |
|---|---|---|
| save APs   | `save -a`      | Write the AP list to SD |
| load APs   | `load -a`      | Read the AP list from SD |
| clear APs  | `clearlist -a` | Empty the scanned AP list |
| clear STAs | `clearlist -c` | Empty the scanned station list |
| join saved | `join -s`      | Join a previously saved Wi-Fi network |
| join (pw)  | *serial only*  | Needs a typed password → serial CLI |
| ssid x5    | `ssid -a -g 5` | Generate 5 random SSIDs into the list |
| ssid name  | *serial only*  | Needs typed text → serial CLI |

## 8. Network Recon — *after you've joined a network*
| Option | Starts | What it does |
|---|---|---|
| pingscan      | `pingscan`         | Ping-sweep the joined network |
| arpscan       | `arpscan -f`       | ARP-scan the joined network |
| portscan http | `portscan -s http` | Port-scan for HTTP |

## 9. SD / Files
| Option | Starts | What it does |
|---|---|---|
| ls /         | `ls /`           | List files on the SD card |
| backupspiffs | `backupspiffs`   | Copy SPIFFS → /spiffs on SD |
| backupstatus | `backupstatus`   | Inspect the /spiffs backup |
| restorespiffs| `restorespiffs`  | Restore SPIFFS from SD |
| update SD    | `update -s`      | **Confirm** — firmware update from SD |
| upload wigle | `upload -d wigle`| Upload wardrive logs (Wigle) |

## 10. Settings / System
| Option | Starts | What it does |
|---|---|---|
| stopscan       | `stopscan`                      | Stop any running scan/sniff/attack |
| reboot         | `reboot`                        | **Confirm** — reboot the board |
| protocolinfo   | `protocolinfo`                  | Dump protocol / build info |
| SavePCAP on    | `settings -s SavePCAP enable`   | Enable pcap capture |
| SavePCAP off   | `settings -s SavePCAP disable`  | Disable pcap capture |
| settings reset | `settings -r`                   | **Confirm** — reset settings |

---

## Limits (by design, 3 buttons)
- **Typed text isn't possible on 3 buttons** → `join (pw)`, `ssid name`,
  `clonestamac` show "use serial" and stay serial-driven.
- **`info`** prints to serial (no room on the OLED for full AP detail).
- TX items affect **your own** gear only; the CONFIRM screen reminds you.
