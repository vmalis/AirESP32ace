#!/bin/sh
# AirESP32ace
# Created by VM, August 2026
set -eu

if [ "$#" -ne 2 ]; then
    echo "Usage: $0 <firmware.elf> <xtensa-esp32s3-elf-objdump>" >&2
    exit 2
fi

# Inspect the actual optimized ESP32 build, not the host ABI. In particular,
# an allocation-error branch used to reserve a 6 KiB status snapshot even
# during successful map loads, exhausting the nested 16 KiB LVGL stack.
for tracker_symbol in ensure_map_buffers app_remote_merge_status render_tracked_flight_snapshot; do
    tracker_frame=$("$2" -d --disassemble="$tracker_symbol" "$1" | awk '
        /entry[[:space:]]+a1,/ { print $NF; exit }
    ')
    # Objdump prints larger frames in hexadecimal.
    if [ -z "$tracker_frame" ] || ! tracker_frame=$(printf '%d' "$tracker_frame" 2>/dev/null); then
        echo "FAIL: no Xtensa stack frame found for $tracker_symbol" >&2
        exit 1
    fi
    tracker_limit=256
    if [ "$tracker_symbol" = render_tracked_flight_snapshot ]; then
        # Detail's countdown fallback must reuse its existing status snapshot,
        # never allocate another full app_remote_status_t in the nested call.
        tracker_limit=4096
    fi
    if [ "$tracker_frame" -gt "$tracker_limit" ]; then
        echo "FAIL: $tracker_symbol reserves $tracker_frame bytes (limit $tracker_limit)" >&2
        exit 1
    fi
    echo "PASS: $tracker_symbol reserves $tracker_frame bytes (limit $tracker_limit)"
done
