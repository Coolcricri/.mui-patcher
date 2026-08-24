#!/bin/bash
# scans folders for .mui files up to DEPTH subfolders deep, finds matching .exe/.dll in the parent directory, and patches them with Resource Hacker.
# Usage: muipatch.sh [-k] <folder1> [folder2] ...
#   -k  Keep original files: back up each .exe/.dll to *_original before patching.
#       Without it, the .res file and the folder containing the .mui file are deleted after each file is patched.

WINEXE=wine
LOGS_DIR="$(dirname "$0")/logs"
RE_HACKER="$(dirname "$0")/ResourceHacker.exe"
DEPTH=5
KEEP_ORG=0

while getopts ":k" OPT; do
    case "$OPT" in
        k) KEEP_ORG=1 ;;
        \?) echo "Unknown option: -$OPTARG" >&2; exit 1 ;;
    esac
done
shift $((OPTIND - 1))

mkdir -p "$LOGS_DIR"

[[ $# -eq 0 ]] && set -- "$(dirname "$0")"

PATCHED=0
SKIPPED=0

for FOLDER in "$@"; do
    if [[ ! -d "$FOLDER" ]]; then
        echo "WARNING: '$FOLDER' is not a directory, skipping." >&2
        (( SKIPPED++ ))
        continue
    fi
    echo "Scanning: $FOLDER"

    while IFS= read -r -d '' MUI_FILE; do
        PARENT_DIR=$(basename "$(dirname "$MUI_FILE")")
        MUI_BASENAME=$(basename "$MUI_FILE")
        BASE_NAME="${MUI_BASENAME%.mui}"
        EXE_STEM="${BASE_NAME%.*}"

        GAME_DIR="$(dirname "$(dirname "$MUI_FILE")")"
        TARGET=$(find "$GAME_DIR" -maxdepth 1 -type f -iname "$BASE_NAME" | head -n1)

        if [[ ! -f "$TARGET" ]]; then
            echo "  WARNING: No matching $BASE_NAME found in $GAME_DIR, skipping." >&2
            (( SKIPPED++ ))
            continue
        fi

        echo "  [$PARENT_DIR] Patching $BASE_NAME..."

        if [[ $KEEP_ORG -eq 1 ]]; then
            # Backup target before patching, adds _original to the name
            TARGET_EXT=".${TARGET##*.}"
            TARGET_ORIG="${TARGET%.*}_original${TARGET_EXT}"
            cp "$TARGET" "$TARGET_ORIG"
        fi

        echo "  .mui to .res conversion..."
        WINEDEBUG=-all $WINEXE "$RE_HACKER" \
            -open "$MUI_FILE" \
            -save "$TARGET.res" \
            -action extract \
            -mask "*,*" \
            -log "$LOGS_DIR/$EXE_STEM-extract.log"

        echo "  Patching .res into $BASE_NAME..."
        WINEDEBUG=-all $WINEXE "$RE_HACKER" \
            -open "$TARGET" \
            -save "$TARGET" \
            -action addoverwrite \
            -res "$TARGET.res" \
            -mask "*,*" \
            -log "$LOGS_DIR/$EXE_STEM-patch.log"

        # Clean UTF-16 null bytes from Resource Hacker logs
        for _LOG in "$LOGS_DIR/$EXE_STEM-extract.log" "$LOGS_DIR/$EXE_STEM-patch.log"; do
            [[ -f "$_LOG" ]] && tr -d '\000' < "$_LOG" > "$_LOG.tmp" && mv "$_LOG.tmp" "$_LOG"
        done

        if [[ $KEEP_ORG -eq 0 ]]; then
            MUI_DIR="$(dirname "$MUI_FILE")"
            rm -f "$TARGET.res"
            rm -f "$MUI_FILE"
            rmdir "$MUI_DIR" 2>/dev/null
        fi

        echo "  Done: $BASE_NAME"
        (( PATCHED++ ))

    done < <(find "$FOLDER" -maxdepth "$DEPTH" -type f -iname "*.mui" -print0)
done

echo ""
echo "Finished: $PATCHED file(s) patched, $SKIPPED skipped."
