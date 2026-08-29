#!/bin/bash

SRC="$(dirname "$0")/ResourceHacker.exe"
OUT="$(dirname "$0")/ResourceHacker.b64.txt"

if [[ ! -f "$SRC" ]]; then
    echo "ERROR: ResourceHacker.exe not found next to $0" >&2
    exit 1
fi

gzip -9 -c "$SRC" | base64 > "$OUT"
echo "Wrote $OUT"
