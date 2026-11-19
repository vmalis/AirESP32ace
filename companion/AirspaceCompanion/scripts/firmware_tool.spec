# AirESP32ace
# Created by VM, August 2026
"""Self-contained arm64 USB helper, without build-machine install records."""
from pathlib import Path
from PyInstaller.utils.hooks import collect_all

datas, binaries, imports = [], [], []
for package in ("esptool", "esp_idf_nvs_partition_gen"):
    files, native, modules = collect_all(package)
    datas.extend(files)
    binaries.extend(native)
    imports.extend(modules)

analysis = Analysis(
    [str(Path(SPECPATH) / "firmware_tool.py")],
    binaries=binaries, datas=datas, hiddenimports=imports,
)
# Filter the final list *after* dependency hooks have run. Pip's RECORD
# may contain absolute build paths even when the wheel itself is clean.
# Versions, entry points, package data and licenses remain available.
private_metadata = {"RECORD", "direct_url.json"}
analysis.datas = [
    entry for entry in analysis.datas
    if not (Path(entry[0]).parent.name.endswith(".dist-info")
            and Path(entry[0]).name in private_metadata)
]
exe = EXE(
    PYZ(analysis.pure), analysis.scripts, analysis.binaries, analysis.datas, [],
    name="AirESP32aceFirmwareTool", console=True, target_arch="arm64",
)
