#!/bin/bash

# Build-Skript für Lisas Lampe (ESP32) mit Svelte-UI: ui | firmware | upload | all | dev | clean

# PlatformIO-Binary: bevorzugt die Standard-Installation, sonst `pio` aus dem PATH
PIO="${HOME}/.platformio/penv/bin/pio"
if [ ! -x "$PIO" ]; then
    PIO="$(command -v pio || true)"
fi

set -e # Exit on error

echo "🔨 Building Lisa's Lamp Project"
echo "================================"

# Check if we're in the project root
if [ ! -f "platformio.ini" ]; then
    echo "❌ Error: This script must be run from the project root directory"
    exit 1
fi

# Function to check if npm is installed
check_npm() {
    if ! command -v npm &> /dev/null; then
        echo "❌ Error: npm is not installed. Please install Node.js and npm first."
        exit 1
    fi
}

# Bricht ab, wenn PlatformIO nicht gefunden wurde
require_pio() {
    if [ -z "$PIO" ] || [ ! -x "$PIO" ]; then
        echo "❌ Error: PlatformIO binary not found. Please install PlatformIO."
        exit 1
    fi
}

# Function to build Svelte UI
build_ui() {
    echo "📱 Building Svelte UI..."
    
    check_npm
    
    cd svelte-ui
    
    # Install dependencies if node_modules doesn't exist
    if [ ! -d "node_modules" ]; then
        echo "📦 Installing npm dependencies..."
        npm install
    fi
    
    # Build and prepare files for ESP32
    echo "🔧 Building and compressing for ESP32..."
    npm run build:esp32
    
    cd ..
    
    echo "✅ Svelte UI build complete!"
}

# Function to build ESP32 firmware
build_firmware() {
    echo "⚡ Building ESP32 firmware..."
    
    require_pio
    
    # Build the firmware
    "$PIO" run -e esp32c3
    
    echo "✅ ESP32 firmware build complete!"
}

# Function to upload firmware
upload_firmware() {
    echo "📤 Uploading firmware to ESP32..."
    require_pio
    
    "$PIO" run -e esp32c3 -t upload
    
    echo "✅ Firmware upload complete!"
}

# Function to show file system contents
show_data_files() {
    echo "📋 Files in data/ directory:"
    if [ -d "data" ]; then
        ls -la data/
        echo ""
        echo "📊 Compressed file sizes:"
        for file in data/*.gzip; do
            if [ -f "$file" ]; then
                size=$(wc -c < "$file")
                echo "   $(basename "$file"): ${size} bytes"
            fi
        done
    else
        echo "   No data directory found"
    fi
}

# Parse command line arguments
case "${1:-all}" in
    "ui")
        build_ui
        show_data_files
        ;;
    "firmware")
        build_firmware
        ;;
    "upload")
        upload_firmware
        ;;
    "all")
        build_ui
        show_data_files
        echo ""
        build_firmware
        ;;
    "dev")
        build_ui
        show_data_files
        echo ""
        echo "🔗 Starting development mode..."
        echo "   - Web UI: http://localhost:5173"
        echo "   - ESP32 proxy endpoints configured"
        echo ""
        cd svelte-ui && npm run dev
        ;;
    "clean")
        echo "🧹 Cleaning build artifacts..."
        rm -rf svelte-ui/dist svelte-ui/node_modules
        # Nur erzeugte Dateien löschen; pico.min.css.gzip und meerkat.avif sind eingecheckt
        rm -f data/app.js.gzip data/index.html.gzip
        require_pio
        "$PIO" run -t clean
        echo "✅ Clean complete!"
        ;;
    *)
        echo "Usage: $0 [ui|firmware|upload|all|dev|clean]"
        echo ""
        echo "Commands:"
        echo "  ui        - Build only the Svelte UI and compress for ESP32"
        echo "  firmware  - Build only the ESP32 firmware"  
        echo "  upload    - Upload firmware to ESP32"
        echo "  all       - Build both UI and firmware (default)"
        echo "  dev       - Start development server for UI"
        echo "  clean     - Clean all build artifacts"
        echo ""
        echo "Examples:"
        echo "  ./build.sh ui      # Just build the web interface"
        echo "  ./build.sh dev     # Start development server"
        echo "  ./build.sh all     # Full build"
        exit 1
        ;;
esac

echo ""
echo "🎉 Build completed successfully!"