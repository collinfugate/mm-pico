#!/bin/sh
# Double-click to flash the Pico. Arguments pass straight through to flash.py.
cd "$(dirname "$0")" || exit 1
if ! command -v python3 >/dev/null 2>&1; then
    echo "Python 3 is needed. Install it from https://www.python.org/downloads/"
    exit 1
fi
exec python3 flash.py "$@"
