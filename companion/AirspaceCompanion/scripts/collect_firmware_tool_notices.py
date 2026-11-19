# AirESP32ace
# Created by VM, August 2026

"""Collect redistribution notices and the separately executed GPL tool source."""
import importlib.metadata
from pathlib import Path
import shutil
import sys
import sysconfig

destination = Path(sys.argv[1])
destination.mkdir(parents=True, exist_ok=True)
for distribution in importlib.metadata.distributions():
    name = distribution.metadata["Name"]
    for entry in distribution.files or []:
        if any(word in entry.name.lower() for word in ("license", "copying", "notice")):
            source = Path(distribution.locate_file(entry))
            if source.is_file() and source.suffix not in (".py", ".pyc"):
                target = destination / name / Path(*entry.parts[-2:])
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(source, target)

python_license = Path(sysconfig.get_path("stdlib")) / "LICENSE.txt"
if python_license.exists():
    shutil.copyfile(python_license, destination / "Python-LICENSE.txt")

scripts = Path(__file__).resolve().parent
source = scripts.parent / ".build/firmware-tool/sources/esptool-4.12.0.tar.gz"
if not source.is_file():
    raise SystemExit("Missing matching esptool source archive; download it before packaging.")
shutil.copyfile(source, destination / source.name)
for name in ("firmware_tool.py", "firmware_tool.spec", "build_firmware_tool.sh", "firmware_tool_requirements.txt"):
    shutil.copyfile(scripts / name, destination / name)
