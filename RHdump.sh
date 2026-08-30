#!/bin/bash

SELF_DIR="$(dirname "$0")"
SRC="$SELF_DIR/ResourceHacker.exe"
OUT="$SELF_DIR/ResourceHacker.b64.txt"
SCRIPTS="muipatch.sh muipatch-win.sh"

if [[ ! -f "$SRC" ]]; then
    echo "ERROR: ResourceHacker.exe not found next to $0" >&2
    exit 1
fi

gzip -9 -c "$SRC" | base64 | sed 's/^/#/' > "$OUT"

EMBEDDED=0
for SCRIPT in $SCRIPTS; do
    SCRIPT_PATH="$SELF_DIR/$SCRIPT"
    if [[ -f "$SCRIPT_PATH" ]]; then
        TMP="$SELF_DIR/.temptrensfer.txt"
        awk -v payload_file="$OUT" '
            BEGIN {
                while ((getline line < payload_file) > 0) payload = payload line "\n"
            }
            /^#__RESOURCEHACKER_B64_START__$/ { print; printf "%s", payload; skip=1; next }
            /^#__RESOURCEHACKER_B64_END__$/ { skip=0 }
            skip { next }
            { print }
        ' "$SCRIPT_PATH" > "$TMP" && mv "$TMP" "$SCRIPT_PATH"
        chmod +x "$SCRIPT_PATH"
        echo "Embedded payload into $SCRIPT_PATH"
        EMBEDDED=1
    fi
done

if [[ $EMBEDDED -eq 1 ]]; then
    rm -f "$OUT"
else
    echo "Wrote $OUT (no patcher scripts found next to this one)"
fi
