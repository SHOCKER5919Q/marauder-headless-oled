"""
Marauder headless CLI catalog.

Source of truth: the `help` output pulled live from THIS headless build
(GENERIC_ESP32, HAS_SCREEN off, HAS_GPS off) during the build session, then
cross-checked against the #ifdef-gated dispatch in CommandLine.cpp.

Descriptions are factual to Marauder's own behavior. Commands whose dispatch
body is compiled out in this build are listed in DEAD (excluded from the
palette). Anything genuinely uncertain is tagged "(unverified)" in its text
rather than guessed at.

Nothing here is invented: type plain `help` at the prompt to get the firmware's
own raw list and compare.
"""

# role: "def" = defensive / detection, "off" = offensive, "neutral" = neither.
# Each entry: (command, role, one-line description)

GROUPS = [
    ("Scan / discover", [
        ("scanall",     "neutral", "Scan APs + stations at once; fills the list (list -a / list -c)"),
        ("recon",       "neutral", "recon wifi|ble|status|stop - automated recon mission"),
        ("list",        "neutral", "list -a|-c|-s|-t|-b|-p|-i|-x|-m - show scanned APs/stations/etc"),
        ("select",      "neutral", "select -a|-s|-c <idx>|-f \"...\" - mark targets for filtered ops"),
        ("info",        "neutral", "info [-a <idx>] - detailed info for a scanned AP"),
        ("mactrack",    "neutral", "Track specific MAC addresses across the capture"),
        ("packetcount", "neutral", "Live packet-rate counter (headless stand-in for the RSSI graph)"),
        ("channel",     "neutral", "channel [-s <n>] - get or set the Wi-Fi channel"),
        ("foxhunt",     "neutral", "foxhunt -w/-s/-b/-t/-f/-p/-m <idx> - RSSI direction-finding to a target"),
    ]),
    ("Sniff - defensive / detection", [
        ("sniffdeauth",    "def", "Detect deauth / disassoc attacks happening around you"),
        ("sniffpwn",       "def", "Detect Pwnagotchi devices"),
        ("sniffpinescan",  "def", "Detect Wi-Fi Pineapple devices"),
        ("sniffmultissid", "def", "Detect karma / multi-SSID (evil-twin) access points"),
        ("sniffskim",      "def", "Detect BLE credit-card skimmers"),
    ]),
    ("Sniff - passive capture  (add -serial to stream raw pcap over USB)", [
        ("sniffbeacon", "neutral", "Capture beacon frames - AP discovery / fingerprinting"),
        ("sniffprobe",  "neutral", "Capture client probe requests - device / MAC-randomization fingerprinting"),
        ("sniffraw",    "neutral", "Capture all raw 802.11 frames"),
        ("sniffsae",    "neutral", "Capture WPA3 SAE commit frames"),
        ("sniffpmkid",  "neutral", "sniffpmkid [-c <ch>][-d][-l] - capture PMKID handshake material"),
    ]),
    ("Attack - OFFENSIVE", [
        ("attack",     "off", "attack -t <deauth|beacon|probe|rickroll|csa|sae|badmsg|quiet|sleep ...>"),
        ("karma",      "off", "karma -p <idx> - answer probe requests as bait (evil-twin)"),
        ("evilportal", "off", "evilportal [-c start [-w html.html]] - captive-portal credential capture"),
        ("spoofat",    "off", "spoofat -t <idx> - spoof a scanned AP"),
    ]),
    ("BLE", [
        ("sniffbt",  "neutral", "sniffbt [-t] <airtag|ibeacon|flipper|flock|meta> - BLE device sniff"),
        ("findmy",   "neutral", "findmy -t <idx> - track Apple Find My devices"),
        ("remoteid", "def",     "remoteid <scan|list|track|clear> [idx] - drone Remote ID (detection)"),
        ("blespam",  "off",     "blespam -t <sourapple|applejuice|google|samsung|windows|flipper|all> - BLE ad spam"),
    ]),
    ("MAC-spoof / identity - OFFENSIVE (useful as test traffic for a detector)", [
        ("randapmac",   "off", "Randomize the SoftAP (own AP) MAC"),
        ("randstamac",  "off", "Randomize the station (own client) MAC"),
        ("cloneapmac",  "off", "cloneapmac [-a <idx>] - clone a scanned AP's MAC"),
        ("clonestamac", "off", "clonestamac [-s <idx>] - clone a scanned station's MAC"),
    ]),
    ("Target lists / session", [
        ("ssid",      "neutral", "ssid -a [-g <n>/-n <name>] / -r <idx> - manage the SSID list"),
        ("add",       "neutral", "add -a|-c -b <mac> ... - manually add an AP/station to the list"),
        ("save",      "neutral", "save -a|-s - write target lists to SD"),
        ("load",      "neutral", "load -a|-s - read target lists from SD"),
        ("clearlist", "neutral", "clearlist -a|-c|-s - clear scanned AP/station/SSID lists"),
        ("join",      "neutral", "join -a <idx> -p <pw> - join a network (enables ping/arp/portscan)"),
    ]),
    ("Network recon  (only after `join`)", [
        ("pingscan", "neutral", "Ping-sweep the joined network"),
        ("arpscan",  "neutral", "arpscan [-f] - ARP-scan the joined network"),
        ("portscan", "neutral", "portscan [-a -t <ip>]/[-s <ssh|telnet|dns|http|smtp|https|rdp>]"),
    ]),
    ("SD / files", [
        ("ls",            "neutral", "ls <dir> - list files on the SD card"),
        ("backupspiffs",  "neutral", "Copy SPIFFS -> /spiffs on SD"),
        ("backupstatus",  "neutral", "Inspect /spiffs backup on SD"),
        ("restorespiffs", "neutral", "Restore SPIFFS from /spiffs on SD"),
        ("update",        "neutral", "update -s|-w - firmware update from SD or web"),
        ("upload",        "neutral", "upload -d <wdg|wigle|both> - upload wardrive logs (unverified: needs GPS data)"),
    ]),
    ("Settings / control", [
        ("settings",     "neutral", "settings [-s <key> enable/disable]|[-r] - e.g. toggle SavePCAP"),
        ("stopscan",     "neutral", "Stop any running scan/sniff/attack  (or just use /stop)"),
        ("reboot",       "neutral", "Reboot the board"),
        ("protocolinfo", "neutral", "Dump protocol / build info"),
        ("geofence",     "neutral", "geofence list/set <1-5> <lat> <lon> <mi> \"name\" / clear <1-5>"),
    ]),
]

# Compiled OUT of this headless build - names appear in `help` but their
# dispatch bodies are #ifdef'd away, so they are no-ops here. Excluded from the
# palette and completion on purpose.
DEAD = {
    "gps": "HAS_GPS off (no GPS module)",
    "gpsdata": "HAS_GPS off",
    "nmea": "HAS_GPS off",
    "gpspoi": "HAS_GPS off",
    "gpstracker": "HAS_GPS off",
    "led": "HAS_NEOPIXEL_LED off (no NeoPixel)",
    "brightness": "drives a TFT backlight that isn't present (harmless no-op)",
}

# Common flags, surfaced in tab-completion.
FLAGS = ["-serial", "-t", "-c", "-a", "-s", "-b", "-p", "-d", "-g", "-n",
         "-w", "-r", "-f", "-l", "-ch", "-i", "-x", "-m",
         "start", "stop", "status", "wifi", "ble", "enable", "disable"]


def all_commands():
    cmds = []
    for _title, entries in GROUPS:
        cmds.extend(c for c, _r, _d in entries)
    return cmds
