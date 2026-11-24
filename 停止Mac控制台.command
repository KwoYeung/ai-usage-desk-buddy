#!/bin/bash
set -e
cd "$(dirname "$0")"
exec .venv-console/bin/python mac_console_control.py stop
