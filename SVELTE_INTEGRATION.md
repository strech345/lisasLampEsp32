# Lisa's Lamp - Svelte UI Integration

This document explains the new Svelte-based web UI that has been integrated into Lisa's Lamp ESP32 project.

## Overview

The project now includes a modern Svelte-based web interface that replaces the original HTML/CSS/JS implementation. The Svelte app is compiled to highly optimized, gzipped files that are served by the ESP32.

## Project Structure

```
├── svelte-ui/              # Svelte application source
│   ├── src/
│   │   ├── App.svelte     # Main application component
│   │   ├── main.js        # Application entry point
│   │   ├── components/    # Reusable Svelte components
│   │   │   ├── AlarmTable.svelte
│   │   │   ├── ColorPicker.svelte
│   │   │   ├── MessageAlert.svelte
│   │   │   └── SystemSettings.svelte
│   │   └── stores/        # Svelte stores for state management
│   │       ├── configStore.js
│   │       ├── messageStore.js
│   │       └── systemStore.js
│   ├── package.json       # NPM dependencies
│   ├── vite.config.js     # Vite build configuration
│   └── build-esp32.js     # Custom build script for ESP32
├── data/                  # ESP32 file system (LittleFS)
│   ├── index.html.bak    # Original HTML (backup)
│   ├── index.html.gz     # Compressed Svelte app
│   ├── app.js.gz         # Compressed JavaScript
│   ├── app.css.gz        # Compressed styles
│   └── pico.min.css.gz   # Compressed CSS framework
├── src/
│   └── route_handlers.cpp # Updated to serve gzipped files
└── build.sh              # Convenient build script
```

## Features

### Svelte Components
- **AlarmTable**: Manages the alarm configuration with dynamic add/remove
- **ColorPicker**: RGB color selection with live preview
- **SystemSettings**: Modal for WiFi and system configuration
- **MessageAlert**: Toast notifications for user feedback

### State Management
- **configStore**: Handles lamp configuration (colors, alarms)
- **messageStore**: Manages user notifications
- **systemStore**: Handles system settings (WiFi credentials)

### Build Optimization
- Files are automatically minified and gzipped
- Significant size reduction (85%+ for CSS, 60%+ for JS)
- Optimized for ESP32 memory constraints

## Development Workflow

### Prerequisites
- Node.js and npm
- PlatformIO
- ESP32 development environment

### Building the UI

```bash
# Build only the Svelte UI
./build.sh ui

# Start development server (with ESP32 proxy)
./build.sh dev

# Full build (UI + firmware)
./build.sh all

# Upload to ESP32
./build.sh upload

# Clean all artifacts
./build.sh clean
```

### Development Server

When running `./build.sh dev`, the development server includes proxy configuration to forward API calls to the ESP32 (assumes ESP32 AP at 192.168.4.1).

```javascript
// Proxy configuration in vite.config.js
server: {
  proxy: {
    '/get_config': 'http://192.168.4.1',
    '/set_config': 'http://192.168.4.1',
    '/get_system_config': 'http://192.168.4.1',
    '/set_system_config': 'http://192.168.4.1'
  }
}
```

## API Integration

The Svelte app communicates with the ESP32 via the same REST API as the original HTML implementation:

### Endpoints
- `GET /get_config` - Retrieve current lamp configuration
- `POST /set_config` - Update lamp configuration
- `GET /get_system_config` - Retrieve system settings
- `POST /set_system_config` - Update system settings

### Data Structures

```javascript
// Configuration structure
{
  "override_color": {"r": 255, "g": 255, "b": 255},
  "colorOverrideActive": false,
  "alarms": [
    {"day": 1, "hour": 7, "minute": 0, "active": true},
    // ... up to 10 alarms
  ]
}

// System settings structure
{
  "internalSSID": "Lisa_Lamp_Config",
  "internalPW": "password",
  "externalSSID": "home_wifi",
  "externalPW": "wifi_password"
}
```

## File Serving

The ESP32 now serves compressed files with proper headers:

```cpp
// Example route handler for compressed files
void handleAppJS(AsyncWebServerRequest *request) {
    AsyncWebServerResponse *response = request->beginResponse(
        LittleFS, "/app.js.gz", "application/javascript"
    );
    response->addHeader("Content-Encoding", "gzip");
    response->addHeader("Cache-Control", "public,max-age=31536000");
    request->send(response);
}
```

## Benefits of Svelte Integration

### For Users
- **Faster Loading**: Compressed files load much faster
- **Better UX**: Reactive components provide smoother interactions
- **Mobile Friendly**: Responsive design works well on all devices

### For Developers
- **Component-Based**: Modular, reusable components
- **State Management**: Centralized stores for consistent state
- **Modern Tooling**: Hot reload, TypeScript support, etc.
- **Maintainable**: Clear separation of concerns

### For ESP32
- **Memory Efficient**: Smaller files mean less memory usage
- **Performance**: Gzipped files reduce transfer time
- **Caching**: Proper cache headers improve perceived performance

## Troubleshooting

### Build Issues
```bash
# Clean and rebuild
./build.sh clean
./build.sh all
```

### Development Server Issues
- Ensure ESP32 is accessible at 192.168.4.1
- Check that ESP32 AP is running
- Verify proxy configuration in vite.config.js

### Compression Issues
- Verify files exist in `data/` directory after build
- Check ESP32 LittleFS has enough space
- Ensure proper Content-Encoding headers

## Migration Notes

The original `index.html` has been preserved as `index.html.bak`. The Svelte implementation maintains full API compatibility, so no changes are required to the ESP32 backend code except for the route handlers to serve gzipped files.

## Future Enhancements

- TypeScript integration for better type safety
- Progressive Web App (PWA) features
- Real-time updates via WebSocket
- Advanced alarm scheduling
- Color themes and customization