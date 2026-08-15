# muipatch.ps1 — patches .exe/.dll files using .mui resource files via Resource Hacker.
# Usage: muipatch.ps1 [folder1] [folder2] ...
# If no folders given, defaults to the script's own directory.

$LOGS_DIR = Join-Path $PSScriptRoot "logs"
$RE_HACKER = Join-Path $PSScriptRoot "ResourceHacker.exe"
$DEPTH = 5

New-Item -ItemType Directory -Force -Path $LOGS_DIR | Out-Null

if ($args.Count -eq 0) { $folders = @($PSScriptRoot) }
else                    { $folders = $args }

$PATCHED = 0
$SKIPPED = 0

foreach ($FOLDER in $folders) {
    if (-not (Test-Path $FOLDER -PathType Container)) {
        Write-Error "WARNING: '$FOLDER' is not a directory, skipping."
        $SKIPPED++
        continue
    }
    Write-Host "Scanning: $FOLDER"

    Get-ChildItem -Path $FOLDER -Recurse -Depth $DEPTH -Filter "*.mui" -File | ForEach-Object {
        $MUI_FILE   = $_.FullName
        $LOCALE_DIR = Split-Path $MUI_FILE -Parent
        $PARENT_DIR = Split-Path $LOCALE_DIR -Leaf
        $GAME_DIR   = Split-Path $LOCALE_DIR -Parent
        $BASE_NAME  = $_.Name -replace '\.mui$', ''          # e.g. game.exe
        $EXE_STEM   = [System.IO.Path]::GetFileNameWithoutExtension($BASE_NAME) # e.g. game

        # Find matching target in game dir, case-insensitive
        $TARGET = Get-ChildItem -Path $GAME_DIR -File |
                  Where-Object { $_.Name -ieq $BASE_NAME } |
                  Select-Object -First 1 -ExpandProperty FullName

        if (-not $TARGET) {
            Write-Warning "  No matching $BASE_NAME found in $GAME_DIR, skipping."
            $SKIPPED++
            return
        }

        Write-Host "  [$PARENT_DIR] Patching $BASE_NAME..."

        $LOG_EXTRACT = Join-Path $LOGS_DIR "$EXE_STEM-extract.log"
        $LOG_PATCH   = Join-Path $LOGS_DIR "$EXE_STEM-patch.log"

        # Step 1: extract resources from .mui into .res
        Write-Host "  .mui to .res conversion..."
        & $RE_HACKER -open $MUI_FILE -save "$TARGET.res" -action extract -mask "*,*" -log $LOG_EXTRACT

        # Step 2: patch .res into target binary
        Write-Host "  Patching .res into $BASE_NAME..."
        & $RE_HACKER -open $TARGET -save $TARGET -action addoverwrite -res "$TARGET.res" -mask "*,*" -log $LOG_PATCH

        # Clean UTF-16 null bytes from Resource Hacker logs
        foreach ($LOG in @($LOG_EXTRACT, $LOG_PATCH)) {
            if (Test-Path $LOG) {
                $bytes = [System.IO.File]::ReadAllBytes($LOG)
                $clean = for ($i = 0; $i -lt $bytes.Length; $i += 2) { $bytes[$i] }
                [System.IO.File]::WriteAllBytes($LOG, [byte[]]$clean)
            }
        }

        Write-Host "  Done: $BASE_NAME"
        $PATCHED++
    }
}

Write-Host ""
Write-Host "Finished: $PATCHED file(s) patched, $SKIPPED skipped."
