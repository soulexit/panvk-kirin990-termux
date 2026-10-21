#!/usr/bin/env bash
# Copy the app's PanVK/ARMSX2 logs into a timestamped directory.
set -euo pipefail

SRC="${SRC:-/sdcard/Android/data/com.armsx2.nightly/files/logs}"
OUT="${1:-$PWD/logs-$(date +%Y%m%d-%H%M%S)}"
mkdir -p "$OUT"

for f in panvk_jm.log emulog.txt session.log; do
    if [ -f "$SRC/$f" ]; then
        cp -f "$SRC/$f" "$OUT/$f"
        echo "copied $SRC/$f"
    else
        echo "missing $SRC/$f" >&2
    fi
done

if [ -f "$SRC/panvk_nowait" ]; then
    echo "warning: panvk_nowait is present; present is in diagnostic mode" >&2
fi
if [ -f "$SRC/panvk_no_roaa" ]; then
    echo "warning: panvk_no_roaa is present; ROAA is disabled" >&2
fi

echo "output: $OUT"
