#!/usr/bin/env bash
# Copy the Marauder SD payload (manuals, log/config dirs) to the microSD card.
# The TFT UI loads manuals from /marauder/manual/<section>.md when a card is
# present, and falls back to its built-in copy when it is not. Run AFTER you have
# wired the SD slot (SD_CS=5, shared SPI) and inserted the card into this laptop.
#   ./install-to-sd.sh                      # auto-detect a single FAT/exFAT card
#   ./install-to-sd.sh /run/media/you/CARD  # or pass the mountpoint explicitly
set -euo pipefail
SRC="$(cd "$(dirname "$0")" && pwd)/marauder"
[ -d "$SRC" ] || { echo "missing $SRC"; exit 1; }

DEST="${1:-}"
if [ -z "$DEST" ]; then
  mapfile -t CANDS < <(lsblk -rno MOUNTPOINT,FSTYPE | awk '$2 ~ /vfat|exfat|fat/ && $1 ~ /\/(media|run\/media|mnt)\// {print $1}')
  [ "${#CANDS[@]}" -eq 1 ] || { echo "Insert the SD and pass its mountpoint: ./install-to-sd.sh /run/media/$USER/LABEL"; exit 1; }
  DEST="${CANDS[0]}"
fi
[ -d "$DEST" ] || { echo "Not a directory: $DEST"; exit 1; }

mkdir -p "$DEST/marauder"
cp -r "$SRC/." "$DEST/marauder/"
sync
echo "Installed manual + dirs -> $DEST/marauder/"
echo "Sections: $(ls "$DEST/marauder/manual"/*.md 2>/dev/null | wc -l) .md files"
echo "Reinsert the card into the device; Manual screen will show '(SD)'."
