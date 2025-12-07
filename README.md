# Homink

ESPHome-based e-ink dashboard for Home Assistant, displaying solar energy, weather, gate status, and EV charging information on Waveshare 7.5" e-Paper displays.

Based on [esphome-weatherman-dashboard](https://github.com/Madelena/esphome-weatherman-dashboard).

## Features

### Display Sections

**Weather**
- Current condition with weather icon (50+ conditions supported)
- Day/night/sunset icon variants
- Current temperature
- WiFi signal strength indicator

**Energy**
- Real-time solar power output (kW)
- Solar production last 24 hours (kWh)
- Home consumption last 24 hours (kWh)
- EV charger status with real-time charging power
  - Prioritizes power reading over status to handle status glitches
  - Shows actual charging power with Tesla power factor correction

**Gates & Security**
- Three gate sensors (Sidewalk, Driveway, Side)
- Lock status display
- Visual indicators for open/closed/unlocked states

**Status Footer**
- Last refresh timestamp when connected
- "Last Seen" timestamp when disconnected from Home Assistant
- Automatic connection status tracking

### Smart Update System

**Three-layer update mechanism:**
1. **Push updates** - Instant refresh on significant sensor changes via callbacks
2. **Polling fallback** - 15-second polling catches any missed updates
3. **Forced refresh** - Every 30 minutes regardless of changes

**Intelligent thresholds prevent unnecessary refreshes:**
- Temperature: ≥1°F change
- Solar power: ≥0.5kW change
- Charging power: ≥100W change
- Gates/Lock: Any state change
- Weather: Any condition change
- Charger status: Ignores "unavailable" glitches

### Connection Monitoring

- Tracks Home Assistant connectivity
- 30-minute timeout for connection loss detection
- Displays "Last Seen" timestamp when disconnected
- Automatic reconnection handling

## Hardware

**Per device:**
- ESP32 DevKit
- Waveshare 7.5" e-Paper V2 (model: 7.50inv2, 800x480 B/W)
- SPI wiring:
  - CLK: GPIO13
  - MOSI: GPIO14
  - CS: GPIO15
  - DC: GPIO27
  - RST: GPIO26
  - BUSY: GPIO25 (inverted)

**Deployed devices:**
- `homink-entrance` (192.168.85.151)
- `homink-slider` (192.168.85.185)

Currently both devices display identical information.

## Build & Deploy

### Prerequisites

1. ESPHome installed
2. `secrets.yaml` file with:
   ```yaml
   wifi_ssid: "your_wifi_ssid"
   wifi_password: "your_wifi_password"
   ```
3. Fonts in `fonts/` directory:
   - GothamRnd-Bold.ttf
   - GothamRnd-Book.ttf
   - materialdesignicons-webfont.ttf

### Commands

```bash
# Compile and upload to device
esphome run homink-entrance.yaml
esphome run homink-slider.yaml

# Compile only (check for errors)
esphome compile homink-entrance.yaml

# View logs
esphome logs homink-entrance.yaml
```

### OTA Updates

After initial USB flash, devices support Over-The-Air updates via WiFi.

## Architecture

### File Organization

**Shared configuration (97% of config):**
- `homink-common.yaml` - All sensors, display rendering, update logic, scripts
- `homink_sensor.h` - C++ sensor infrastructure (templates, base classes, macros)

**Device-specific (26 lines each):**
- `homink-entrance.yaml` / `homink-entrance.h`
- `homink-slider.yaml` / `homink-slider.h`

Device files only contain: device name, IP address, and C++ includes.

### Sensor System

Custom C++ template-based sensor tracking with unified linked list:

**Sensor types:**
- **StateSensor** - Any change triggers update (gates, lock, weather)
- **ThresholdSensor** - Only triggers on threshold-exceeding changes (temp, solar, charging)
- **PassiveSensor** - Tracks connection but never triggers updates (sun elevation, energy totals)
- **FilteredTextStateSensor** - Ignores specific state values (charger ignores "unavailable")

**X-macro pattern** for automatic sensor registration - sensors defined once in device `.h` files, automatically registered and initialized at boot.

## Adding Sensors

Three steps to add a new sensor:

1. **Define in device `.h` file** (e.g., `homink-entrance.h`):
   ```cpp
   #define S_NEW_SENSOR new_sensor
   SENSOR_THRESHOLD(S_NEW_SENSOR, "Display Name", "sensor.entity_id", 1.0)
   ```
   Then add to `SENSOR_INIT_ALL()` macro.

2. **Add YAML sensor** in `homink-common.yaml`:
   ```yaml
   - platform: homeassistant
     entity_id: "sensor.entity_id"
     id: _new_sensor  # ESPHome adds _ prefix
     on_value:
       - lambda: 'SENSOR_UPDATE_CALLBACK(new_sensor)'
   ```

3. **Add rendering** to display lambda in `homink-common.yaml`

**Available sensor macros:**
- `SENSOR_BINARY(var, name, entity)` - Binary sensors
- `SENSOR_TEXT(var, name, entity)` - Text sensors
- `SENSOR_TEXT_FILTERED(var, name, entity, ignored_value)` - Text with value filtering
- `SENSOR_THRESHOLD(var, name, entity, threshold)` - Numeric with threshold
- `SENSOR_PASSIVE(var, name, entity)` - Connection tracking only
- `SENSOR_WIFI(var, name, entity)` - WiFi signal (ESPHome built-in)

## Configuration Values

**Thresholds:**
- Temperature: 1.0°F
- Solar power: 0.5kW
- Charging power: 100W
- Tesla power factor: 0.789 (installation-specific)

**Timers:**
- Polling interval: 15s
- Forced refresh: 1800s (30 min)
- HA connection timeout: 1800s (30 min)

**Display:**
- Dimensions: 800x480 pixels
- Colors: Black/White (bistable e-ink)
- Refresh time: ~4 seconds (blocking operation)

## Home Assistant Integration

Requires the following Home Assistant entities:

**Weather:**
- `sensor.openweathermap_condition`
- `sensor.birgenshire_temp`

**Solar:**
- `sensor.birgenshire_solar_power`
- `sensor.solar_production_last_24h_2`
- `sensor.home_consumption_last_24h_2`

**EV Charging:**
- `sensor.tesla_wall_connector_status`
- `sensor.tesla_wall_connector_current_power`

**Gates & Security:**
- `binary_sensor.aqara_door_and_window_sensor_p2_door` (Side gate)
- `binary_sensor.aqara_door_and_window_sensor_p2_door_2` (Sidewalk gate)
- `binary_sensor.aqara_door_and_window_sensor_p2_door_3` (Driveway gate)
- `lock.shed_lock`

**Other:**
- `sun.sun` (elevation attribute for day/night detection)
- ESPHome built-in: WiFi signal sensor

## License

See original [esphome-weatherman-dashboard](https://github.com/Madelena/esphome-weatherman-dashboard) for license information.
