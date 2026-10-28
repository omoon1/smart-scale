#!/usr/bin/env bash
set -euo pipefail
# Enumerate without opening ports, resetting boards, or requiring ESP-IDF.
shopt -s nullglob
case "$(uname -s)" in
    Darwin) ports=(/dev/cu.*) ;;
    Linux) ports=(/dev/ttyACM* /dev/ttyUSB*) ;;
    *) echo 'Serial discovery supports Linux and macOS.' >&2; exit 1 ;;
esac
found=0
for port in "${ports[@]}"; do
    [[ -c "$port" ]] || continue
    printf '%s\n' "$port"
    found=1
done
if (( found == 0 )); then
    echo 'No serial ports found. Connect the board with a data-capable USB cable and run make probe again.'
else
    echo 'Select a port above: make flash /dev/<port>'
    echo 'Listed ports are candidates; discovery does not identify or open the board.'
fi
