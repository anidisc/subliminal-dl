#!/bin/bash
set -e

# Setup
mkdir -p server_root
# Create a dummy large file if not exists
if [ ! -f server_root/source_file.bin ]; then
    dd if=/dev/urandom of=server_root/source_file.bin bs=1M count=1000
fi

echo "Starting Python server in server_root..."
cd server_root
python3 -m http.server 8082 &
SERVER_PID=$!
cd ..
sleep 2 # Wait for server to start

DOWNLOADED_FILE="source_file.bin"
PART_FILE="${DOWNLOADED_FILE}.part"
URL="http://localhost:8082/${DOWNLOADED_FILE}"

# Calculate expected MD5
EXPECTED_MD5=$(md5sum server_root/source_file.bin | awk '{print $1}')
echo "Expected MD5: $EXPECTED_MD5"

# Remove any previous runs in current dir
rm -f "$DOWNLOADED_FILE" "$PART_FILE"

# Start download and interrupt
echo "Starting download (will interrupt)..."
./sdl --url "$URL" &
SDL_PID=$!
sleep 0.5
echo "Interrupting download (PID $SDL_PID)..."
kill -SIGINT $SDL_PID || true
wait $SDL_PID || true

# Check for partial file
if [ -f "$PART_FILE" ]; then
    echo "SUCCESS: Partial file created: $PART_FILE"
    ls -l "$PART_FILE"
    # Verify it has some size
    SIZE=$(stat -c%s "$PART_FILE")
    if [ "$SIZE" -eq "0" ]; then
        echo "FAILURE: Partial file is empty!"
        kill $SERVER_PID
        exit 1
    fi
else
    echo "FAILURE: Partial file NOT found!"
    kill $SERVER_PID
    exit 1
fi

# Resume download
echo "Resuming download..."
./sdl --url "$URL"

# Check for final file
if [ -f "$DOWNLOADED_FILE" ]; then
    echo "File download completed."
else
    echo "FAILURE: Final file NOT found!"
    kill $SERVER_PID
    exit 1
fi

# Check if part file is gone
if [ -f "$PART_FILE" ]; then
    echo "FAILURE: Partial file still exists!"
    kill $SERVER_PID
    exit 1
fi

# Verify MD5
ACTUAL_MD5=$(md5sum $DOWNLOADED_FILE | awk '{print $1}')
echo "Actual MD5: $ACTUAL_MD5"

if [ "$EXPECTED_MD5" == "$ACTUAL_MD5" ]; then
    echo "SUCCESS: MD5 matches!"
else
    echo "FAILURE: MD5 mismatch!"
    kill $SERVER_PID
    exit 1
fi

# Cleanup
kill $SERVER_PID
rm -f "$DOWNLOADED_FILE"
echo "Test Passed."
