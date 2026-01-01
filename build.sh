#!/bin/bash

# Build script for Lisa's Lamp ESP32 project with Svelte UI

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
    
    # Check if PlatformIO is installed
    if ! command -v pio &> /dev/null; then
        echo "❌ Error: PlatformIO is not installed. Please install it first."
        exit 1
    fi
    
    # Build the firmware
    pio run -e esp32
    
    echo "✅ ESP32 firmware build complete!"
}

# Function to upload firmware
upload_firmware() {
    echo "📤 Uploading firmware to ESP32..."
    
    pio run -e esp32 -t upload
    
    echo "✅ Firmware upload complete!"
}

# Function to show file system contents
show_data_files() {
    echo "📋 Files in data/ directory:"
    if [ -d "data" ]; then
        ls -la data/
        echo ""
        echo "📊 Compressed file sizes:"
        for file in data/*.gz; do
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
        rm -f data/*.gz
        pio run -t clean
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