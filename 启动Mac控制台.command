#!/bin/bash
set -e
cd "$(dirname "$0")"
exec .venv-console/bin/python tools/passport_console.py --port 18766 --config-dir "$PWD/.local-console"
