"""
Frame-line aggregation for the console's /table view.

Marauder's scan/sniff feed emits one line per heard frame, e.g.
    -32 Ch: 2 2a:ef:99:b8:32:47 ESSID: Phone_1_1004 21 14
    1: sta: d4:f3:2d:1f:b3:82 -> ap: 2a:ef:99:b8:32:47
The first kind (RSSI/Ch/MAC/ESSID) becomes a table row keyed by MAC; repeats
update the row in place and bump a seen-count. Association lines are recognized
as part of the high-rate feed (so /table can suppress them from the scrollback)
but are not themselves rows. Everything else is "not a frame" and prints normally.
"""
import re
import time
import threading

# leading "> " or "#" from the prompt/echo is tolerated
_FRAME_RE = re.compile(
    r'^[>#\s]*(-?\d+)\s+Ch:\s*(\d+)\s+([0-9a-fA-F:]{17})\s+ESSID:\s*(.*)$')
_ASSOC_RE = re.compile(
    r'^[>#\s]*\d+:\s*(?:sta|ap):\s*[0-9a-fA-F:]{17}\s*->\s*(?:sta|ap):\s*[0-9a-fA-F:]{17}')
# trailing " 21 14" style capability/flags tokens on a frame line
_TAIL_RE = re.compile(r'\s+\d+\s+[0-9A-Fa-f]{1,2}\s*$')


def classify(line):
    """Return ('frame', (rssi,ch,mac,essid)) | ('assoc', None) | (None, None)."""
    m = _FRAME_RE.match(line)
    if m:
        rssi = int(m.group(1))
        ch = int(m.group(2))
        mac = m.group(3).lower()
        essid = _TAIL_RE.sub('', m.group(4)).strip()
        return 'frame', (rssi, ch, mac, essid)
    if _ASSOC_RE.match(line):
        return 'assoc', None
    return None, None


class Aggregator:
    def __init__(self):
        self._lock = threading.Lock()
        self._rows = {}          # mac -> dict(rssi, ch, essid, count, last)
        self.frames = 0          # total frame lines consumed
        self.assoc = 0           # association lines consumed

    def feed(self, kind, data):
        with self._lock:
            if kind == 'assoc':
                self.assoc += 1
                return
            rssi, ch, mac, essid = data
            self.frames += 1
            r = self._rows.get(mac)
            if r is None:
                self._rows[mac] = {'rssi': rssi, 'ch': ch, 'essid': essid,
                                   'count': 1, 'last': time.time()}
            else:
                r['rssi'] = rssi
                r['ch'] = ch
                if essid:
                    r['essid'] = essid
                r['count'] += 1
                r['last'] = time.time()

    def clear(self):
        with self._lock:
            self._rows.clear()
            self.frames = 0
            self.assoc = 0

    def snapshot(self, sort='rssi'):
        with self._lock:
            rows = [dict(mac=m, **v) for m, v in self._rows.items()]
            uniq = len(self._rows)
            frames, assoc = self.frames, self.assoc
        if sort == 'last':
            rows.sort(key=lambda r: r['last'], reverse=True)
        else:  # strongest signal first
            rows.sort(key=lambda r: r['rssi'], reverse=True)
        return rows, uniq, frames, assoc

    def render(self, sort='rssi', limit=12, width=None):
        rows, uniq, frames, assoc = self.snapshot(sort)
        now = time.time()
        head = (f"  {'RSSI':>4}  {'Ch':>2}  {'MAC':<17}  {'ESSID':<20} {'Seen':>5} {'Age':>4}")
        sep = "  " + "-" * (len(head) - 2)
        out = [f"[table] {uniq} unique | {frames} frames | {assoc} assoc "
               f"| sort={sort}  (/raw for stream)",
               head, sep]
        if not rows:
            out.append("  (waiting for frames...)")
        for r in rows[:limit]:
            essid = r['essid'][:20] if r['essid'] else ''
            age = int(now - r['last'])
            out.append(f"  {r['rssi']:>4}  {r['ch']:>2}  {r['mac']:<17}  "
                       f"{essid:<20} {r['count']:>5} {age:>3}s")
        if len(rows) > limit:
            out.append(f"  ... +{len(rows) - limit} more rows")
        return "\n".join(out)
