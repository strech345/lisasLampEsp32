#!/bin/bash
# Full build and upload script for Lisa's Lamp

# PlatformIO-Binary: bevorzugt die Standard-Installation, sonst `pio` aus dem PATH
PIO="${HOME}/.platformio/penv/bin/pio"
if [ ! -x "$PIO" ]; then
    PIO="$(command -v pio || true)"
fi
if [ -z "$PIO" ] || [ ! -x "$PIO" ]; then
    echo "❌ Error: PlatformIO binary not found. Please install PlatformIO."
    exit 1
fi

set -e

echo "🚀 Full build and upload for Lisa's Lamp"

echo "📦 Building UI..."
./build.sh ui

echo "⚡ Building firmware..."
"$PIO" run -e esp32c3

echo "💾 Uploading data partition (LittleFS)..."
"$PIO" run -e esp32c3 -t uploadfs
echo "📤 Uploading firmware..."
"$PIO" run -e esp32c3 -t upload

echo "✅ Success: Firmware and UI deployed!"