# AirESP32ace
# Created by VM, August 2026
# SPDX-License-Identifier: GPL-2.0-or-later

"""Private, frozen installer helper; never depends on the recipient's Python."""

import sys


def main():
    if len(sys.argv) < 2:
        raise SystemExit("Usage: AirESP32aceFirmwareTool <flash|nvs> [arguments]")
    operation = sys.argv.pop(1)
    if operation == "flash":
        import esptool

        esptool._main()
    elif operation == "nvs":
        from esp_idf_nvs_partition_gen.nvs_partition_gen import main as nvs_main

        nvs_main()
    else:
        raise SystemExit("Unknown installer operation")


if __name__ == "__main__":
    main()
