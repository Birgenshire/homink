# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

> **IMPORTANT:** This project is actively evolving. Keep this file updated when making architectural changes, adding sensors, or modifying the display layout. The accuracy of this documentation directly impacts the quality of AI assistance.

## Project Overview

Homink is an ESPHome-based e-ink dashboard for Home Assistant, displaying solar energy, weather, gate status, and EV charging information on Waveshare 7.5" e-Paper displays (800x480 B/W).

**Two devices deployed:**
- **homink-entrance** - Main entrance display
- **homink-slider** - Secondary display (currently showing same data)

Based on [esphome-weatherman-dashboard](https://github.com/Madelena/esphome-weatherman-dashboard).

## Build & Deploy

This is an ESPHome project. Use standard ESPHome commands:

```bash
# Compile and upload to specific device
esphome run homink-entrance.yaml
esphome run homink-slider.yaml

# Compile only (no upload)
esphome compile homink-entrance.yaml
esphome compile homink-slider.yaml

# View logs
esphome logs homink-entrance.yaml
esphome logs homink-slider.yaml
```

Requires a `secrets.yaml` file with `wifi_ssid` and `wifi_password`.

## Architecture

### File Structure

```
homink/
├── homink-common.inc        # Shared ESPHome config (~95% of code) - uses .inc to hide from ESPHome UI
├── homink_sensor.h          # Generic C++ sensor infrastructure (templates, base classes, macros)
├── homink-entrance.yaml     # Entrance device: substitutions + package include (~68 lines)
├── homink-entrance.h        # Entrance device: C++ sensor definitions (~63 lines)
├── homink-slider.yaml       # Slider device: substitutions + package include (~68 lines)
├── homink-slider.h          # Slider device: C++ sensor definitions (~63 lines)
├── fonts/                   # GothamRnd-Bold.ttf, GothamRnd-Book.ttf, materialdesignicons-webfont.ttf
├── secrets.yaml             # WiFi credentials (not in git)
├── README.md                # User-facing documentation
└── CLAUDE.md                # This file - AI assistant guidance
```

**Why `.inc` extension?** The shared config uses `.inc` instead of `.yaml` so ESPHome's UI in Home Assistant doesn't show it as a standalone compilable project. The `!include` directive works with any file extension.

### Packages + Substitutions Pattern

ESPHome's `packages:` feature enables sharing configuration across devices. The pattern used here:

1. **Device YAML files** define substitutions for all configurable values:
   ```yaml
   substitutions:
     device_name: "Homink Entrance"
     device_ip: "192.168.x.x"
     gate1_var: "gate1"
     gate1_entity: "binary_sensor.your_gate_sensor"
     # ... more substitutions

   packages:
     common: !include homink-common.inc
   ```

2. **homink-common.inc** references these via `${variable_name}`:
   ```yaml
   binary_sensor:
     - platform: homeassistant
       entity_id: ${gate1_entity}
       id: _${gate1_var}
       on_state:
         then:
           - lambda: 'SENSOR_UPDATE_CALLBACK(${gate1_var})'
   ```

3. **Device .h files** define the actual C++ sensor objects with matching variable names:
   ```cpp
   SENSOR_BINARY(S_GATE1, "Sidewalk", "binary_sensor.your_gate_sensor")
   ```

**Key insight:** The `_var` substitutions (e.g., `gate1_var: "gate1"`) must match the C++ variable names exactly. ESPHome auto-prefixes sensor IDs with `_`, so `id: _${gate1_var}` becomes `id: _gate1`, which matches the C++ `gate1` sensor object.

### Sensor State System

The codebase uses a custom C++ template-based sensor tracking system defined in `homink_sensor.h`:

- **ISensor** - Base interface with unified linked list for all sensors
- **BaseSensor<ValueType, SensorType>** - Templated base with common logic
- **StateSensor** - For sensors where ANY change triggers update (gates, lock, weather)
- **ThresholdSensor** - Only triggers when change exceeds threshold (temperature: 1°F, solar: 0.5kW, charging: 100W)
- **PassiveSensor** - Tracks HA connection but never triggers display updates (sun elevation, energy totals)
- **FilteredTextStateSensor** - Text sensor that ignores specific state values (e.g., charger ignores "unavailable")

Type aliases: `BinaryStateSensor`, `TextStateSensor`, `FloatThresholdSensor`, `FloatPassiveSensor`, `WiFiPassiveSensor`

**X-Macro Pattern:** Sensors use X-macros defined in device-specific `.h` files for automatic registration and initialization.

### Update Mechanism

Two-layer change detection:
1. **Push** - Sensor callbacks (`SENSOR_UPDATE_CALLBACK` macro) set `data_updated=true` immediately on significant changes
2. **Poll** - 15-second polling loop catches missed updates, compares current vs cached values
3. **Forced refresh** - Every 30 minutes regardless of sensor changes

The `update_screen` script:
1. Sets `data_updated=false` before polling
2. Calls `homeassistant.update_entity` with entity list from `ISensor::get_ha_entity_list()`
3. Waits 2s for HA response
4. Caches all sensor values via `ISensor::update_all()`
5. Triggers display refresh (~4s blocking operation)

## Display Layout & Constraints

### Physical Display Specifications

| Property | Value |
|----------|-------|
| Model | Waveshare 7.5" e-Paper V2 (7.50inv2) |
| Resolution | 800 x 480 pixels |
| Orientation | Portrait (rotated 90°) - effective 480 x 800 |
| Dot pitch | 0.205mm (123.9 DPI) |
| Colors | Black and White only (1-bit) |
| Refresh time | ~4 seconds (full refresh, blocking) |
| Partial refresh | Not supported on this model |

### CRITICAL: Visible Area Constraints (Framed Display)

**The display is mounted behind a mat/frame that obscures the edges. Content rendered outside the visible area will be hidden!**

| Boundary | Pixel Value | Notes |
|----------|-------------|-------|
| **Top (Y min)** | 60 | Content above Y=60 is hidden by mat |
| **Bottom (Y max)** | 741 | Content below Y=741 is hidden by mat |
| **Visible height** | 681 px | (741 - 60 = 681 pixels) |
| **Left (X min)** | 0 | Full width visible |
| **Right (X max)** | 480 | Full width visible |
| **Visible width** | 480 px | No horizontal cropping |

```
Physical display (480 x 800 after rotation):

     Y=0   ┌─────────────────────────────────────────┐
           │▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓│ ← HIDDEN BY MAT
    Y=60   ├─────────────────────────────────────────┤ ← TOP OF VISIBLE AREA
           │                                         │
           │            VISIBLE AREA                 │
           │         (681 x 480 pixels)              │
           │                                         │
           │    All UI content must stay within      │
           │         Y=60 to Y=741                   │
           │                                         │
   Y=741   ├─────────────────────────────────────────┤ ← BOTTOM OF VISIBLE AREA
           │▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓│ ← HIDDEN BY MAT
   Y=800   └─────────────────────────────────────────┘
          X=0                                      X=480
```

**Development tip:** Uncomment the boundary lines in `homink-common.inc` display lambda to visualize the mat edges:
```cpp
// it.line(0, 60, 480, 60, color_text);    // Top boundary
// it.line(0, 741, 480, 741, color_text);  // Bottom boundary
```

### Current Layout (Y coordinates, portrait orientation)

```
┌─────────────────────────────────────────┐ Y=60 (visible top)
│            WEATHER (title @ 85)         │
│  [Weather Icon]      [Temperature]      │ Y=140
│  (x=100)             (x=300)            │
│         WiFi indicator (448, 70)        │
├─────────────────────────────────────────┤ Y=250 (divider line)
│            ENERGY (title @ 268)         │
│  Solar Output ........................  │ Y=339
│  Solar 24hr .........................   │ Y=382
│  Home 24hr ..........................   │ Y=425
│  Charging ...........................   │ Y=468
├─────────────────────────────────────────┤ Y=503 (divider line)
│            GATES (title @ 521)          │
│  Gate 1 (Sidewalk) ..................   │ Y=592
│  Gate 2 (Driveway) ..................   │ Y=640
│  Gate 3 (Side) ......................   │ Y=688
├─────────────────────────────────────────┤
│  Footer: "Refreshed [timestamp]"        │ Y=721
└─────────────────────────────────────────┘ Y=741 (visible bottom)
```

### Layout Constants (from display lambda)

| Element | X Position | Y Position | Alignment | Font |
|---------|------------|------------|-----------|------|
| Section titles | 240 | varies | TOP_CENTER | font_title (44px) |
| Weather icon | 100 | 140 | TOP_CENTER | font_mdi_large (84px) |
| Temperature | 300 | 140 | TOP_CENTER | font_large_bold (96px) |
| WiFi icon | 448 | 70 | TOP_RIGHT | font_mdi_small (20px) |
| Row icons | 60 | varies | CENTER_LEFT | font_mdi_medium (42px) |
| Row labels | 120 | varies | CENTER_LEFT | font_name (24px) |
| Row values | 420 | varies | CENTER_RIGHT | font_medium_bold (32px) |
| Divider lines | 40-440 | 250, 503 | - | 1px line |
| Footer | 240 | 721 | TOP_CENTER | font_small_book (18px) |

### Available Fonts

| ID | Font | Size | Usage |
|----|------|------|-------|
| font_large_bold | GothamRnd-Bold | 96px | Temperature display |
| font_title | GothamRnd-Bold | 44px | Section headers (WEATHER, ENERGY, GATES) |
| font_medium_bold | GothamRnd-Bold | 32px | Row values (kW, OPEN/CLOSED) |
| font_name | GothamRnd-Book | 24px | Row labels (sensor names) |
| font_small_bold | GothamRnd-Bold | 20px | (available, currently unused) |
| font_small_book | GothamRnd-Book | 18px | Footer text |
| font_mdi_large | MDI | 84px | Weather icons |
| font_mdi_medium | MDI | 42px | Row icons (solar, gates, etc.) |
| font_mdi_small | MDI | 20px | WiFi signal indicator |

### UI Design Guidelines

1. **No grayscale** - Only pure black (#000) and white (#FFF) render correctly
2. **Font glyphs must be declared** - Large fonts only include specific characters to save memory
3. **MDI icons need Unicode** - Use `\U000FXXXX` format, check MDI codepoint reference
4. **Vertical spacing** - Rows are ~43-48px apart; maintain consistency
5. **Margins** - Content stays within x=40 to x=440 (400px usable width)
6. **Refresh is expensive** - ~4 seconds blocking; minimize unnecessary updates

## Adding New Sensors

Three steps (example for `homink-entrance.h`):

1. **Add to device .h file:**
   ```cpp
   #define S_NEW_SENSOR new_sensor
   SENSOR_THRESHOLD(S_NEW_SENSOR, "Display Name", "sensor.entity_id", 1.0)
   ```
   Then add `SENSOR_INIT_THRESHOLD(S_NEW_SENSOR)` to the `SENSOR_INIT_ALL()` macro.

2. **Add substitutions to device YAML:**
   ```yaml
   substitutions:
     new_sensor_var: "new_sensor"
     new_sensor_entity: "sensor.entity_id"
   ```

3. **Add YAML sensor in homink-common.inc:**
   ```yaml
   - platform: homeassistant
     entity_id: ${new_sensor_entity}
     id: _${new_sensor_var}
     on_value:
       - lambda: 'SENSOR_UPDATE_CALLBACK(${new_sensor_var})'
   ```

4. **Add rendering** to display lambda in `homink-common.inc`

**Sensor types available:**
- `SENSOR_BINARY(var, name, entity)` - Binary state sensors
- `SENSOR_TEXT(var, name, entity)` - Text state sensors
- `SENSOR_TEXT_FILTERED(var, name, entity, ignored_value)` - Text sensor ignoring specific value
- `SENSOR_THRESHOLD(var, name, entity, threshold)` - Numeric with change threshold
- `SENSOR_PASSIVE(var, name, entity)` - Tracks HA connection only
- `SENSOR_WIFI(var, name, entity)` - WiFi signal sensor

## Key Thresholds & Configuration

| Parameter | Value | Location |
|-----------|-------|----------|
| Temperature threshold | 1.0°F | device .h files |
| Solar power threshold | 0.5 kW | device .h files |
| Charging power threshold | 100W | device .h files |
| Tesla power factor | 0.789 | device YAML substitutions |
| Polling interval | 15 seconds | homink-common.inc |
| Forced refresh interval | 1800s (30 min) | homink-common.inc |
| HA connection timeout | 60s (1 min) | device YAML substitutions |
| HA response wait | 2 seconds | homink-common.inc |

## Hardware

**Each device:**
- ESP32 DevKit
- Waveshare 7.5" e-Paper V2 (model: 7.50inv2)
- SPI pins: CLK=GPIO13, MOSI=GPIO14, CS=GPIO15, DC=GPIO27, RST=GPIO26, BUSY=GPIO25 (inverted)

**Shared resources:**
- Fonts in `fonts/` directory (GothamRnd, MDI icons)

## Design Decisions & Rationale

### Why C++ sensor system instead of pure YAML?

The original implementation used pure YAML with ESPHome's built-in templating. This was replaced with C++ for:
1. **Unified sensor iteration** - One linked list for all sensors regardless of type
2. **Threshold-based updates** - Prevent screen refresh for minor value changes (extends display life)
3. **Filtered state handling** - Ignore transient "unavailable" states from flaky sensors
4. **Compile-time type safety** - Catch errors before deployment

### Why prioritize power reading over charger status?

The Tesla Wall Connector occasionally reports incorrect status (e.g., "not_connected" while actively charging). The display logic checks `charging_power > 100W` first, only falling back to status text when power is low. This handles:
- Status reporting delays
- Momentary status glitches
- More accurate "is it actually charging?" detection

### Why 15-second polling + push callbacks?

- **Push callbacks** provide instant response to state changes
- **Polling backup** catches missed updates (network hiccups, HA slowness, lost UDP packets)
- **15 seconds** balances responsiveness vs. unnecessary HA API calls
- **2-second wait** after polling gives HA time to respond before checking for changes

### Why forced 30-minute refresh?

E-ink displays can develop "ghosting" if static too long. The forced refresh:
- Clears any accumulated ghosting
- Updates the footer timestamp (proves display is alive)
- Catches any edge cases where sensors stopped updating silently

### Why sun elevation for day/night detection?

Using `sun.sun` elevation attribute instead of time-based logic:
- Accounts for seasonal variation in sunrise/sunset
- Handles geographic location automatically
- Golden hour detection (-6° to +6°) for sunset icons
- Fallback to 8PM-6AM if sun sensor unavailable

## Current State & Future Plans

- Both `homink-entrance` and `homink-slider` display identical data from the same Home Assistant sensors
- Future: Devices may be customized to show different information based on their physical location
- The substitution pattern in YAML files is designed to eventually allow per-device sensor customization

## Home Assistant Entities Required

The display expects the following types of entities (actual entity IDs are configured in device YAML files):

**Weather:**
- Weather condition sensor (text) - e.g., OpenWeatherMap condition
- Temperature sensor (°F)
- `sun.sun` (elevation attribute) - Day/night detection

**Solar:**
- Current solar power output (kW)
- Solar production last 24 hours (kWh)
- Home consumption last 24 hours (kWh)

**EV Charging:**
- Charger status sensor (text)
- Charging power sensor (W)

**Gates & Security:**
- Binary sensors for each gate (open/closed)
- Lock entity for gate with lock detection

---

*Last updated: January 2025. Remember to update this file when making significant changes to the project architecture, display layout, or sensor configuration.*
