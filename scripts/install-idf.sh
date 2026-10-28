#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
IDF_VERSION=v5.5.2
IDF_COMMIT=30aaf64524299d3bde422ca9a2848090d1bc5d0f
export IDF_PATH="$ROOT/.tools/esp-idf"
export IDF_TOOLS_PATH="$ROOT/.tools/espressif"
for tool in git python3; do
    command -v "$tool" >/dev/null || { echo "Missing prerequisite: $tool" >&2; exit 1; }
done
mkdir -p "$ROOT/.tools"
if [[ ! -x "$ROOT/.tools/host-tools/bin/python" ]]; then
    python3 -m venv "$ROOT/.tools/host-tools"
fi
"$ROOT/.tools/host-tools/bin/python" -m pip install 'cmake==3.30.5' 'ninja==1.11.1.3'
export PATH="$ROOT/.tools/host-tools/bin:$PATH"
if [[ ! -d "$IDF_PATH/.git" ]]; then
    git clone --branch "$IDF_VERSION" --depth 1 --recursive --shallow-submodules \
        https://github.com/espressif/esp-idf.git "$IDF_PATH"
fi
if [[ "$(git -C "$IDF_PATH" rev-parse HEAD)" != "$IDF_COMMIT" ]]; then
    echo "Existing .tools/esp-idf is not $IDF_VERSION ($IDF_COMMIT). Move it aside to install; it will not be overwritten." >&2
    exit 1
fi
if [[ -n "$(git -C "$IDF_PATH" status --porcelain --untracked-files=no)" ]]; then
    echo 'ESP-IDF checkout has modifications. Resolve them before installing.' >&2
    exit 1
fi
git -C "$IDF_PATH" submodule update --init --recursive --depth 1
"$IDF_PATH/install.sh" esp32s3
echo 'Local ESP-IDF is ready. Run make build.'
