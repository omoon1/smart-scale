#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
export PATH="$ROOT/.tools/host-tools/bin:$PATH"
if [[ ! -d .tools/lvgl/.git ]]; then
    mkdir -p .tools
    git clone --depth 1 --branch v8.4.0 https://github.com/lvgl/lvgl.git .tools/lvgl
fi
if [[ "$(git -C .tools/lvgl rev-parse HEAD)" != 4495f428630cc1741bd8bfd977f080e8460e8e8d ]]; then
    echo 'Expected LVGL v8.4.0 checkout. Move .tools/lvgl aside before retrying.' >&2
    exit 1
fi
cmake -S host -B build-host/ui -G Ninja
cmake --build build-host/ui --parallel 4
./build-host/ui/smart_scale_host "${1:---screenshots}"
