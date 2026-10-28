#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
export IDF_PATH="$ROOT/.tools/esp-idf"
export IDF_TOOLS_PATH="$ROOT/.tools/espressif"
export PATH="$ROOT/.tools/host-tools/bin:$PATH"
if [[ ! -f "$IDF_PATH/export.sh" ]]; then
    echo 'Local ESP-IDF is missing. Run make install first.' >&2
    exit 1
fi
# Espressif export scripts do not promise nounset compatibility.
set +u
source "$IDF_PATH/export.sh" >/dev/null
set -u
cd "$ROOT"
exec python "$IDF_PATH/tools/idf.py" "$@"
