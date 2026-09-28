#!/usr/bin/env bash
# AirESP32ace
# Created by VM, August 2026

set -euo pipefail

if [[ $# -lt 2 ]]; then
  echo "Usage: $0 <project-dir> <idf.py args...>"
  exit 2
fi

project_dir="$1"
shift

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
idf_path="${IDF_PATH:-${repo_root}/tools/esp-idf}"
idf_tools_path="${IDF_TOOLS_PATH:-${HOME}/.espressif}"
known_python_env="${idf_tools_path}/python_env/idf5.5_py3.9_env"

if [[ ! -f "${idf_path}/export.sh" ]]; then
  echo "Missing ESP-IDF export script: ${idf_path}/export.sh"
  exit 1
fi

if [[ -x "${known_python_env}/bin/python" ]]; then
  export IDF_PYTHON_ENV_PATH="${known_python_env}"
fi

. "${idf_path}/export.sh" >/dev/null
cd "${repo_root}/${project_dir}"
idf.py "$@"
