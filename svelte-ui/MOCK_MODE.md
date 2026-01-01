# Mock Mode Instructions

## How to Enable Mock Mode

To test the UI without connecting to an actual ESP32 device, you can enable mock mode by adding `?mock=true` to the URL.

### Development Server

When running the development server:

```bash
npm run dev
```

Then open your browser and navigate to:

```
http://localhost:5173/?mock=true
```

### Production Build

If testing the built version:

```bash
npm run preview
```

Then navigate to:

```
http://localhost:4173/?mock=true
```

## What Mock Mode Does

When mock mode is enabled:

1. **API Calls are Intercepted**: All fetch requests to `/get_config`, `/set_config`, `/get_system_config`, and `/set_system_config` are intercepted and return mock data instead of hitting the ESP32 endpoints.

2. **Mock Data**: The system loads with realistic test data:
   - **Color Override**: Purple color (RGB: 120, 80, 200) with override enabled
   - **Alarms**: Three sample alarms (Monday 7:30 AM, Tuesday 8:00 AM, Friday 6:45 AM - disabled)
   - **System Settings**: Mock WiFi credentials

3. **Visual Feedback**: When mock mode is active, you'll see `🎭 Mock API call:` messages in the browser console for all API interactions.

4. **Save Operations**: Save operations will appear to succeed and show success messages, but no actual data is sent to any backend.

## Mock Data Structure

The mock data includes:

```javascript
// Configuration
{
  override_color: { r: 120, g: 80, b: 200 },
  colorOverrideActive: true,
  alarms: [
    { day: 1, hour: 7, minute: 30, active: true },   // Monday 7:30 AM
    { day: 2, hour: 8, minute: 0, active: true },    // Tuesday 8:00 AM  
    { day: 5, hour: 6, minute: 45, active: false },  // Friday 6:45 AM (disabled)
    // ... remaining 7 slots empty
  ]
}

// System Settings
{
  internalSSID: 'Lisa_Lamp_Mock',
  internalPW: 'mock_password',
  externalSSID: 'Home_WiFi_Mock',
  externalPW: 'home_password_123'
}
```

## Customizing Mock Data

To modify the mock data, edit `/src/lib/mockData.js`:

1. **Change Default Values**: Modify `mockConfig` and `mockSystemSettings` objects
2. **Add Response Delays**: Adjust the `setTimeout` delay in `mockFetch` (currently 200ms)
3. **Simulate Errors**: Modify `mockFetch` to return error responses for testing error handling

## Use Cases

Mock mode is perfect for:

- **UI Development**: Design and test UI components without ESP32 hardware
- **Demo Purposes**: Show the interface functionality without device setup
- **Testing**: Verify form validation, user interactions, and state management
- **Development**: Work on the frontend when the ESP32 is not available or accessible

## Switching Back to Real Mode

Simply remove `?mock=true` from the URL or navigate to the base URL without query parameters to connect to the actual ESP32 endpoints.