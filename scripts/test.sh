#!/bin/bash

TEST_DIR="../tests"

# Check if directory exists
if [ ! -d "$TEST_DIR" ]; then
    echo "[!] Directory '$TEST_DIR' not found."
    exit 1
fi

# Iterate over all .cpp and .cxx files in the directory
for file in "$TEST_DIR"/*.{cpp,cxx}; do
    # Skip if no files match the glob pattern
    [ -e "$file" ] || continue
    
    # Strip extension to create the output binary name
    exec_name="${file%.*}.out"
    
    echo "[*] Compiling: $file"
    if g++ -o "$exec_name" "$file"; then
        chmod +x "$exec_name"
        echo "[*] Running: $exec_name"
        sudo "$exec_name"
        echo "----------------------------------------"
    else
        echo "[!] Compilation failed for $file"
    fi
done