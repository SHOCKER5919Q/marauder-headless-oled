#!/usr/bin/env python3
"""
Mock Marauder over a PTY, for proving the console wrapper without the real board
(e.g. when the USB port is busy). It replays REAL scanall frames captured from
the board this session (test/real_scanall_frames.txt).

It behaves like the firmware's serial side:
  * echoes the command with a leading '#'
  * on `scanall`  -> streams real frame lines until stopped
  * on `stopscan` -> prints "Stopping WiFi tran/recv" and halts the stream
  * on `list -a`  -> prints a short AP list
  * on `help`     -> prints a marker line
Prints the slave PTY path on stdout so the caller can point the wrapper at it.
"""
import os, pty, sys, threading, time

HERE = os.path.dirname(os.path.abspath(__file__))
FRAMES = open(os.path.join(HERE, "real_scanall_frames.txt")).read().splitlines()

master, slave = pty.openpty()
print(os.ttyname(slave), flush=True)

streaming = threading.Event()

def writer():
    while True:
        if streaming.is_set():
            for ln in FRAMES:
                if not streaming.is_set():
                    break
                os.write(master, (ln + "\r\n").encode())
                time.sleep(0.25)          # ~4 frames/sec, like live hopping
        else:
            time.sleep(0.05)

threading.Thread(target=writer, daemon=True).start()

buf = bytearray()
while True:
    try:
        data = os.read(master, 256)
    except OSError:
        break
    if not data:
        break
    buf.extend(data)
    while b"\n" in buf:
        line, _, rest = buf.partition(b"\n")
        buf = bytearray(rest)
        cmd = line.decode("utf-8", "replace").strip()
        if not cmd:
            continue
        os.write(master, ("#" + cmd + "\r\n").encode())
        if cmd == "scanall":
            os.write(master, b"Scanning for APs and Stations. Stop with stopscan\r\n")
            os.write(master, b"/ap_sta_0.pcap\r\n")
            streaming.set()
        elif cmd == "stopscan":
            streaming.clear()
            os.write(master, b"Stopping WiFi tran/recv\r\n> \r\n")
        elif cmd == "list -a":
            os.write(master, b"[0][CH:2] Phone_1_1004 -32\r\n[1][CH:2] ceo -88\r\n"
                             b"[2][CH:2] SECRETARY -87\r\n0 selected\r\n> \r\n")
        elif cmd == "help":
            os.write(master, b"============ Commands ============\r\n(mock)\r\n")
