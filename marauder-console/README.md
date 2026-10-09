# marauder-console

A laptop-side REPL wrapper around the ESP32 Marauder's USB-serial CLI. The
firmware is **untouched** — this only drives it over `/dev/ttyUSB2 @ 115200`.

## Launch

    cd /home/shocker/Marauder/marauder-console && ./marauder

Options: `./marauder -p /dev/ttyUSB0` (other port), `--no-log`, `--no-color`.

> **Free the port first.** Only one program can own the serial port. If a
> `screen`/`picocom`/`minicom` session is connected, close it (screen: `Ctrl-A`
> then `K`). The wrapper opens the port exclusively and will refuse cleanly with
> `Device or resource busy` otherwise — it won't fight for the port.

## In the REPL

- Type any Marauder command directly: `scanall`, `sniffprobe -serial`, `list -a`…
- `/` or `/help` or `?` — grouped command palette (defensive `[DEF]` vs offensive `[OFF]`)
- `/table` (default) — aggregate the high-rate frame feed into a live table (one row
  per BSSID/MAC: RSSI, Ch, MAC, ESSID, seen-count), shown in the bottom toolbar and
  refreshed a few times a second; `/sort rssi|last` changes the ordering
- `/raw` — fall back to the unfiltered line stream (frames scroll). The raw stream is
  written to the session log in **both** modes, so nothing is lost
- `/stop` — send `stopscan` and clear the table (also **Ctrl-C**, which does not quit)
- `plain help` — passes through to the firmware's own raw help list
- `/log` — show the current session log path · `/quit` or Ctrl-D — exit
- Tab-completion on command names and common flags; up-arrow history
- Every session is logged to `logs/marauder-YYYYMMDD-HHMMSS.log` (TX/RX, timestamped)

Non-frame board output (command replies, `list -a/-c`, boot text) prints above a fixed
input line via prompt_toolkit's `patch_stdout`; only the high-rate frame feed is
aggregated into the table, so it never garbles what you're typing.

## Command set

Pulled from this headless build's live `help` and cross-checked against the
`#ifdef` gating in `CommandLine.cpp`. Commands whose dispatch is compiled out in
this build (`gps*`, `led`, `brightness`) are excluded from the palette as no-ops.

## Non-interactive proofs (no terminal needed)

    ./.venv/bin/python marauder_console.py --palette          # print the grouped palette
    ./.venv/bin/python marauder_console.py --selftest -p PORT          # table view: scanall, snapshots, /stop
    ./.venv/bin/python marauder_console.py --selftest --raw -p PORT    # raw stream view

`test/mock_marauder.py` is a PTY that replays real captured `scanall` frames, so
`--selftest` can be demonstrated without the board (used to prove the async
reader + `/stop` without disturbing a live screen session).

## Files

    marauder              launcher (./marauder)
    marauder_console.py   the wrapper (REPL + --selftest + --palette)
    commands.py           grouped command catalog + descriptions + dead list
    test/mock_marauder.py PTY replay of real board output, for offline proofs
    .venv/                pyserial + prompt_toolkit
    logs/                 timestamped session logs
