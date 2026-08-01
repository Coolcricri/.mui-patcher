#!/bin/bash
# scans folders for .mui files up to 4 subfolders deep, finds matching .exe/.dll in the parent directory, and patches them with Resource Hacker.
# Usage: patch_mui.sh <folder1> [folder2] ...

#use env WINEPREFIX= to change the variable, like with normal wine, to reach resource hacker
WINEPREFIX="${WINEPREFIX:-$HOME/.wine}"
LOGS_DIR="$(dirname "$0")/logs"
DEPTH=5
RE_HACKER="$(dirname "$0")/ResourceHacker.exe"

mkdir -p "$LOGS_DIR"

PATCHED=0
SKIPPED=0
# main loop
for FOLDER in "$@"; do
    if [[ ! -d "$FOLDER" ]]; then
        echo "WARNING: '$FOLDER' is not a directory, skipping." >&2
        (( SKIPPED++ ))
        continue
    fi
    echo "Scanning: $FOLDER"

    while IFS= read -r -d '' MUI_FILE; do
        PARENT_DIR=$(basename "$(dirname "$MUI_FILE")")

        # Strip .mui to get base filename
        MUI_BASENAME=$(basename "$MUI_FILE")
        BASE_NAME="${MUI_BASENAME%.mui}"
        EXE_STEM="${BASE_NAME%.*}"

        # Target exe/dll that sits one level above the .mui file
        GAME_DIR="$(dirname "$(dirname "$MUI_FILE")")"
        TARGET=$(find "$GAME_DIR" -maxdepth 1 -type f -iname "$BASE_NAME" | head -n1)

        if [[ ! -f "$TARGET" ]]; then
            echo "  WARNING: No matching $BASE_NAME found in $GAME_DIR, skipping." >&2
            (( SKIPPED++ ))
            continue
        fi

        echo "  [$PARENT_DIR] Patching $BASE_NAME..."

        echo "  .mui to .res conversion..."
        WINEDEBUG=-all wine "$RE_HACKER" \
            -open "$MUI_FILE" \
            -save "$TARGET.res" \
            -action extract \
            -mask "*,*" \
            -log "$LOGS_DIR/$EXE_STEM-extract.log"

        # Backup target before patching, adds _original to the name
        TARGET_EXT=".${TARGET##*.}"
        TARGET_ORIG="${TARGET%.*}_original${TARGET_EXT}"
        cp "$TARGET" "$TARGET_ORIG"


        echo "  Patching .res into $BASE_NAME..."
        WINEDEBUG=-all wine "$RE_HACKER" \
            -open "$TARGET" \
            -save "$TARGET" \
            -action addoverwrite \
            -res "$TARGET.res" \
            -mask "*,*" \
            -log "$LOGS_DIR/$EXE_STEM-patch.log"

        # Clean UTF-16 encoding from reshacker logs, does nothing if python not installed
        for _LOG in "$LOGS_DIR/$EXE_STEM-extract.log" "$LOGS_DIR/$EXE_STEM-patch.log"; do
            if command -v python3 &>/dev/null && [[ -f "$_LOG" ]]; then
                python3 -c "
import sys
path = sys.argv[1]
data = open(path, 'rb').read()
open(path, 'wb').write(data[::2])
" "$_LOG"
            fi
        done

        echo "  Done: $BASE_NAME"
        (( PATCHED++ ))

    done < <(find "$FOLDER" -maxdepth $DEPTH -type f -iname "*.mui" -print0)
done

echo ""
echo "Finished: $PATCHED file(s) patched, $SKIPPED skipped."
