#!/usr/bin/env bash
# Замер качества справки: см. scripts/quality.py (токены файлами, DOCS_URL).
exec python3 -I "$(dirname "$0")/quality.py" "$@"
