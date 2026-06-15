#!/usr/bin/env bash

RELATIVE_FILE="$1"
TARGET_DIR=""

# 1. Try to determine the project directory from the currently open file in Zed
if [ -n "$RELATIVE_FILE" ]; then
    # Extract the first directory component (one level below top level)
    SUBDIR=$(echo "$RELATIVE_FILE" | cut -d'/' -f1)
    if [ -n "$SUBDIR" ] && [ -d "$SUBDIR" ] && [ -f "$SUBDIR/wscript" ]; then
        TARGET_DIR="$SUBDIR"
    fi
fi

# 2. Fallback: Scan for any subdirectory one level below root containing a 'wscript' file
if [ -z "$TARGET_DIR" ]; then
    projects=()
    for d in */; do
        if [ -f "${d}wscript" ]; then
            projects+=("${d%/}")
        fi
    done
    
    if [ ${#projects[@]} -eq 1 ]; then
        TARGET_DIR="${projects[0]}"
    elif [ ${#projects[@]} -gt 1 ]; then
        # Default to the most recently modified project directory based on package.json mtime
        newest_project=""
        newest_time=0
        for p in "${projects[@]}"; do
            mtime=$(stat -c %Y "$p/package.json" 2>/dev/null || stat -c %Y "$p" 2>/dev/null || echo 0)
            if [ "$mtime" -gt "$newest_time" ]; then
                newest_time=$mtime
                newest_project="$p"
            fi
        done
        TARGET_DIR="${newest_project:-${projects[0]}}"
        echo "Multiple projects found. Dynamically selected most recently modified: $TARGET_DIR"
    fi
fi

# If we still couldn't find a target directory, report error and open shell
if [ -z "$TARGET_DIR" ]; then
    echo "Error: Could not dynamically determine a Pebble project subdirectory." >&2
    exec /bin/bash
fi

echo "Selected project directory: $TARGET_DIR"
cd "$TARGET_DIR"

# Build the Pebble watchface
if ! pebble build; then
    echo "Error: Pebble build failed." >&2
    exec /bin/bash
fi

# Install the watchface to the emulator
if ! pebble install --emulator "${PEBBLE_EMULATOR:-emery}"; then
    echo "Error: Pebble install failed." >&2
    exec /bin/bash
fi

echo "Build and Install completed successfully!"

# Keep the terminal open by replacing the script process with an interactive shell
exec /bin/bash
