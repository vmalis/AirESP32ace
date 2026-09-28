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
for tracker_symbol in ensure_map_buffers app_remote_merge_status; do
    tracker_frame=$("$2" -d --disassemble="$tracker_symbol" "$1" | awk '
        /entry[[:space:]]+a1,/ { print $NF; exit }
    ')
    case "$tracker_frame" in
        ''|*[!0-9]*)
            echo "FAIL: no Xtensa stack frame found for $tracker_symbol" >&2
            exit 1
            ;;
    esac
    if [ "$tracker_frame" -gt 256 ]; then
        echo "FAIL: $tracker_symbol reserves $tracker_frame bytes (limit 256)" >&2
        exit 1
    fi
    echo "PASS: $tracker_symbol reserves $tracker_frame bytes (limit 256)"
done
