#!/bin/bash

# Find root directory of the repository
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../../.." && pwd)"

# Ports and binaries
PORT=12345
SERVER_BIN="$ROOT_DIR/build/examples/demo_chat"
CLIENT_BIN="$ROOT_DIR/build/client/ascii_emulator"
SCRIPT_FILE="$SCRIPT_DIR/input_hello.script"

# Check if binaries exist
if [ ! -f "$SERVER_BIN" ] || [ ! -f "$CLIENT_BIN" ]; then
    echo "Error: Binaries not found. Please compile the project first:"
    echo "  cmake -B build -S ."
    echo "  cmake --build build"
    exit 1
fi

echo "Starting chat server in the background on port $PORT..."
# Start server in background
"$SERVER_BIN" > /dev/null 2>&1 &
SERVER_PID=$!

# Ensure server gets killed on script exit
cleanup() {
    echo "Stopping chat server (PID $SERVER_PID)..."
    kill "$SERVER_PID" 2>/dev/null
    wait "$SERVER_PID" 2>/dev/null
}
trap cleanup EXIT

# Wait a brief moment for server socket to bind
sleep 0.5

echo "Running emulator client with script: $(basename "$SCRIPT_FILE")"
echo "------------------------------------------------------------"

# Run client in headless mode, printing the final screen output
"$CLIENT_BIN" -p "$PORT" -f "$SCRIPT_FILE" --headless --print-screen

echo "------------------------------------------------------------"
echo "Test finished."
