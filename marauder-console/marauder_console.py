#!/usr/bin/env python3
"""
marauder-console - a laptop-side REPL wrapper around the ESP32 Marauder's
USB-serial CLI. The firmware is untouched; this only drives it.

  * async read thread prints board output ABOVE a fixed prompt line
  * `/` (or /help, ?) shows the command palette, grouped, defensive vs offensive
  * tab-completion on command names + common flags
  * /stop (and Ctrl-C) send `stopscan` without killing the session
  * up-arrow history + timestamped session log

Interactive use needs a real terminal. `--selftest` and `--palette` exercise the
plumbing non-interactively (used to prove it out).
"""
import argparse
import datetime as dt
import os
import sys
import threading
import time

import serial  # pyserial

from commands import GROUPS, DEAD, FLAGS, all_commands
from aggregator import Aggregator, classify

# ---- ANSI colors (only when attached to a tty) ------------------------------
def _supports_color():
    return sys.stdout.isatty()

class C:
    def __init__(self, on):
        self.on = on
    def _w(self, code, s):
        return f"\033[{code}m{s}\033[0m" if self.on else s
    def dim(self, s):   return self._w("2", s)
    def bold(self, s):  return self._w("1", s)
    def cyan(self, s):  return self._w("36", s)
    def green(self, s): return self._w("32", s)
    def red(self, s):   return self._w("31", s)
    def yellow(self, s):return self._w("33", s)

# Role badge used in the palette.
def role_badge(c, role):
    if role == "def":
        return c.green("[DEF]")
    if role == "off":
        return c.red("[OFF]")
    return c.dim("[   ]")


def render_palette(c):
    """Return the grouped command palette as a string. Pure; no serial needed."""
    out = []
    out.append(c.bold("=== Marauder command palette (this headless build) ==="))
    out.append(c.dim("  " + c.green("[DEF]") + " defensive/detection   "
                      + c.red("[OFF]") + " offensive   [   ] neutral"))
    out.append(c.dim("  type a command as-is (e.g. `scanall`), or plain `help` for the board's own list"))
    out.append("")
    for title, entries in GROUPS:
        out.append(c.cyan(title))
        for cmd, role, desc in entries:
            out.append(f"  {role_badge(c, role)} {c.bold(cmd.ljust(14))} {desc}")
        out.append("")
    dead = ", ".join(sorted(DEAD))
    out.append(c.yellow("Not functional in this build (no-ops, excluded): ") + c.dim(dead))
    out.append(c.dim("Wrapper meta: /help  /table  /raw  /sort rssi|last  /stop  /log  /quit   "
                     "| Ctrl-C=stopscan+clear  Ctrl-D=quit"))
    return "\n".join(out)


class Session:
    def __init__(self, port, baud, logdir=None, color=None):
        self.port = port
        self.baud = baud
        self.color = C(_supports_color() if color is None else color)
        self.ser = None
        self.reader = None
        self.running = False
        self.printer = print  # replaced with patch_stdout-safe print in REPL
        self.agg = Aggregator()
        self.table_mode = True   # /table (aggregate) vs /raw (stream); default aggregate
        self.sort = "rssi"
        self.logfile = None
        if logdir is not None:
            os.makedirs(logdir, exist_ok=True)
            stamp = dt.datetime.now().strftime("%Y%m%d-%H%M%S")
            self.logpath = os.path.join(logdir, f"marauder-{stamp}.log")
            self.logfile = open(self.logpath, "a", buffering=1)
        else:
            self.logpath = None

    # -- connection -----------------------------------------------------------
    def open(self):
        # exclusive=True fails fast with a clear error if screen/another app
        # still holds the port, instead of silently fighting over bytes.
        self.ser = serial.Serial(self.port, self.baud, timeout=0.2, exclusive=True)
        self.running = True
        self.reader = threading.Thread(target=self._read_loop, daemon=True)
        self.reader.start()

    def close(self):
        self.running = False
        if self.reader:
            self.reader.join(timeout=1.0)
        if self.ser and self.ser.is_open:
            self.ser.close()
        if self.logfile:
            self.logfile.close()

    def _log(self, direction, text):
        if self.logfile:
            ts = dt.datetime.now().strftime("%H:%M:%S.%f")[:-3]
            self.logfile.write(f"{ts} {direction} {text}\n")

    # -- io -------------------------------------------------------------------
    def _read_loop(self):
        buf = bytearray()
        while self.running:
            try:
                chunk = self.ser.read(256)
            except Exception as e:  # port vanished (unplug), etc.
                self.printer(self.color.red(f"[serial read error: {e}]"))
                self.running = False
                break
            if not chunk:
                continue
            buf.extend(chunk)
            while b"\n" in buf:
                line, _, rest = buf.partition(b"\n")
                buf = bytearray(rest)
                text = line.decode("utf-8", "replace").rstrip("\r")
                self._log("RX", text)          # raw stream always hits the log
                kind, data = classify(text)
                if self.table_mode and kind is not None:
                    # high-rate frame feed -> aggregate in place, don't scroll it
                    self.agg.feed(kind, data)
                else:
                    self.printer(self.color.dim("| ") + text)

    def send(self, cmd):
        if not (self.ser and self.ser.is_open):
            return
        self._log("TX", cmd)
        self.ser.write((cmd + "\r\n").encode("utf-8"))


# ---- selftest: prove the plumbing without an interactive TTY ----------------
def run_selftest(sess, seconds):
    c = sess.color
    print(render_palette(c))
    print()
    print(c.bold(f"[selftest] opening {sess.port} @ {sess.baud} ..."))
    sess.open()
    print(c.green("[selftest] connected; reader thread live. Board output prints below as `| ...`"))
    time.sleep(0.5)
    mode = "TABLE (aggregated)" if sess.table_mode else "RAW (stream)"
    print(c.bold(f"[selftest] -> scanall   [{mode}]"))
    sess.send("scanall")
    # In table mode, frames are aggregated (not scrolled); print periodic
    # snapshots so you can watch repeated APs collapse into stable rows.
    if sess.table_mode:
        steps = max(1, seconds // 3)
        for i in range(steps):
            time.sleep(3.0)
            print(c.cyan(f"\n--- table snapshot {i + 1}/{steps} "
                         f"(t+{(i + 1) * 3}s) ---"))
            print(sess.agg.render(sort=sess.sort))
    else:
        time.sleep(seconds)
    print(c.bold("\n[selftest] -> /stop  (sends stopscan, clears table)"))
    sess.send("stopscan")
    sess.agg.clear()
    time.sleep(1.5)
    print(c.green("[selftest] table after stop: "
                  + ("empty" if not sess.agg.snapshot()[0] else "NON-EMPTY")))
    print(c.bold("[selftest] -> list -a  (non-frame output still prints normally)"))
    sess.send("list -a")
    time.sleep(2.0)
    sess.close()
    print(c.green("[selftest] done."))
    if sess.logpath:
        print(c.dim(f"[selftest] session logged to {sess.logpath}"))


# ---- interactive REPL -------------------------------------------------------
def run_repl(sess):
    from prompt_toolkit import PromptSession, print_formatted_text
    from prompt_toolkit.patch_stdout import patch_stdout
    from prompt_toolkit.history import FileHistory
    from prompt_toolkit.completion import NestedCompleter, WordCompleter
    from prompt_toolkit.key_binding import KeyBindings
    from prompt_toolkit.formatted_text import ANSI

    c = sess.color

    # Route ALL styled output through prompt_toolkit's renderer (same path the
    # table uses). Our C class emits standard SGR codes; ANSI() parses them so
    # patch_stdout renders real colors instead of leaking literal escape bytes
    # ("?[2m"). With color off (non-TTY), the strings carry no codes, so this is
    # clean plain text too.
    def emit(s=""):
        print_formatted_text(ANSI(s))

    cmds = all_commands()

    # command -> flag completer; meta commands too.
    flagset = {f: None for f in FLAGS}
    nested = {cmd: WordCompleter(FLAGS, WORD=True) for cmd in cmds}
    nested.update({"/help": None, "/stop": None, "/log": None, "/quit": None,
                   "/exit": None, "/table": None,
                   "/sort": WordCompleter(["rssi", "last"], WORD=True),
                   "/raw": None, "help": None})
    completer = NestedCompleter.from_nested_dict(nested)

    histpath = os.path.join(os.path.dirname(os.path.abspath(__file__)), ".mc_history")
    kb = KeyBindings()

    @kb.add("c-c")
    def _(event):
        # Ctrl-C halts the board and clears the live table, not the session.
        sess.send("stopscan")
        sess.agg.clear()
        event.app.current_buffer.reset()
        emit(c.yellow("^C -> stopscan (table cleared)"))

    def bottom_toolbar():
        # Live, in-place aggregated table (refreshes via refresh_interval).
        if not sess.table_mode:
            return "[raw stream] /table for aggregated view"
        return sess.agg.render(sort=sess.sort)

    psession = PromptSession(history=FileHistory(histpath),
                             completer=completer, key_bindings=kb,
                             complete_while_typing=True,
                             bottom_toolbar=bottom_toolbar,
                             refresh_interval=0.4)

    def show_help():
        emit(render_palette(c))

    with patch_stdout():
        sess.printer = emit   # all async/board output goes through pt renderer
        sess.open()
        emit(c.green(f"connected to {sess.port} @ {sess.baud}"))
        if sess.logpath:
            emit(c.dim(f"logging to {sess.logpath}"))
        emit(c.dim("type `/` or `/help` for the command palette, /quit to exit"))
        while True:
            try:
                line = psession.prompt("marauder> ")
            except EOFError:        # Ctrl-D
                break
            except KeyboardInterrupt:
                continue            # handled by the c-c binding
            line = line.strip()
            if not line:
                continue
            low = line.lower()
            if low in ("/", "/help", "?", "/palette"):
                show_help(); continue
            if low in ("/quit", "/exit"):
                break
            if low == "/stop":
                sess.send("stopscan"); sess.agg.clear()
                emit(c.yellow("stopscan sent (table cleared)")); continue
            if low == "/log":
                emit(c.dim(sess.logpath or "logging disabled")); continue
            if low == "/raw":
                sess.table_mode = False
                emit(c.dim("raw stream mode - frames scroll above; /table to aggregate")); continue
            if low == "/table":
                sess.table_mode = True
                emit(c.dim("table mode - frames aggregate below; /raw for the stream")); continue
            if low.startswith("/sort"):
                parts = low.split()
                sess.sort = "last" if (len(parts) > 1 and parts[1].startswith("last")) else "rssi"
                emit(c.dim(f"table sort = {sess.sort}")); continue
            if line.startswith("/"):
                emit(c.red(f"unknown meta-command {line!r} (try /help)")); continue
            # warn (don't block) on a command we know is a no-op in this build
            head = line.split()[0]
            if head in DEAD:
                emit(c.yellow(f"note: `{head}` is a no-op in this headless build "
                               f"({DEAD[head]}); sending anyway"))
            sess.send(line)
    sess.close()
    emit("bye")


def main():
    ap = argparse.ArgumentParser(description="Marauder serial console wrapper")
    ap.add_argument("-p", "--port", default="/dev/ttyUSB2")
    ap.add_argument("-b", "--baud", type=int, default=115200)
    ap.add_argument("--no-log", action="store_true", help="don't write a session log")
    ap.add_argument("--selftest", action="store_true",
                    help="non-interactive plumbing test (connect, scanall, stop)")
    ap.add_argument("--palette", action="store_true",
                    help="print the grouped command palette and exit (no serial)")
    ap.add_argument("--seconds", type=int, default=15, help="selftest scan duration")
    ap.add_argument("--raw", action="store_true",
                    help="start in raw stream mode instead of the aggregated table")
    ap.add_argument("--no-color", action="store_true")
    args = ap.parse_args()

    color = False if args.no_color else None
    logdir = None if args.no_log else os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "logs")

    if args.palette:
        print(render_palette(C(_supports_color() if color is None else color)))
        return 0

    sess = Session(args.port, args.baud, logdir=logdir, color=color)
    if args.raw:
        sess.table_mode = False
    try:
        if args.selftest:
            run_selftest(sess, args.seconds)
        else:
            run_repl(sess)
    except serial.SerialException as e:
        c = sess.color
        print(c.red(f"[error] could not open {args.port}: {e}"), file=sys.stderr)
        print(c.yellow("If a `screen`/`picocom` session still holds the port, close it first "
                       "(screen: Ctrl-A K, or `screen -d` from another shell)."), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
