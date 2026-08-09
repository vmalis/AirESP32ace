#!/usr/bin/env bash
# AirESP32ace
# Created by VM, August 2026

set -euo pipefail

echo "USB serial ports:"
ls /dev/cu.usbmodem* /dev/cu.usbserial* 2>/dev/null || true

echo
echo "If nothing appears, check that the USB-C cable supports data."
