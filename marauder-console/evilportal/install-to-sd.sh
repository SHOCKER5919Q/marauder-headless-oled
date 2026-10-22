#!/usr/bin/env bash
# Copy the Evil Portal captive page to the Marauder SD card as /index.html
# (the default target_html_name the firmware loads). Run AFTER inserting the
# card into this laptop. Usage:
#   ./install-to-sd.sh               # auto-detect a single mounted removable card
#   ./install-to-sd.sh /run/media/you/CARD   # or pass the mountpoint explicitly
set -euo pipefail
SRC="$(cd "$(dirname "$0")" && pwd)/index.html"
[ -f "$SRC" ] || { echo "missing $SRC"; exit 1; }

DEST="${1:-}"
if [ -z "$DEST" ]; then
  # auto-detect FAT/exFAT removable mountpoints under the usual media dirs
  mapfile -t CANDS < <(lsblk -rno MOUNTPOINT,FSTYPE | awk '$2 ~ /vfat|exfat|fat/ && $1 ~ /\/(media|run\/media|mnt)\// {print $1}')
  if [ "${#CANDS[@]}" -eq 0 ]; then
    echo "No mounted FAT/exFAT card found. Insert the SD, then re-run,"
    echo "or pass the mountpoint: ./install-to-sd.sh /run/media/$USER/LABEL"
    exit 1
  elif [ "${#CANDS[@]}" -gt 1 ]; then
    echo "Multiple cards found; pass one explicitly:"; printf '  %s\n' "${CANDS[@]}"; exit 1
  fi
  DEST="${CANDS[0]}"
fi

[ -d "$DEST" ] || { echo "Not a directory: $DEST"; exit 1; }
cp "$SRC" "$DEST/index.html"
sync
echo "Copied -> $DEST/index.html ($(wc -c < "$SRC") bytes)"
echo "Reinsert the card into the ESP32. Portal twin will serve it as /index.html."
