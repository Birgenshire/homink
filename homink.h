// ============================================================================
// HOMINK PROJECT-SPECIFIC SENSOR DEFINITIONS
// ============================================================================
// Single source of truth for all sensors using X-macro pattern.
//
// To add a new sensor:
//   1. Add S_* define and SENSOR_* entry below
//   2. Add YAML sensor block with id: _varname
//   That's it! Declarations and boot init are automatic.

#include "homink_sensor.h"

// ============================================================================
// SENSOR NAME DEFINITIONS - Single source of truth for sensor identifiers
// ============================================================================
// Define once, use in both SENSOR_* declaration and SENSOR_INIT_ALL()
// ESPHome id is automatically generated as _name (e.g., gate1 -> _gate1)

#define S_GATE1            gate1
#define S_GATE2            gate2
#define S_GATE3            gate3
#define S_LOCK             lock
#define S_WEATHER          weather
#define S_CHARGER          charger
#define S_TEMPERATURE      temperature
#define S_SOLAR_POWER      solar_power
#define S_CHARGING_POWER   charging_power
#define S_SUN_ELEV         sun_elev
#define S_SOLAR_ENERGY     solar_energy
#define S_HOME_CONSUMPTION home_consumption
#define S_WIFI_RSSI        wifi_rssi

// ============================================================================
// SENSOR DECLARATIONS - Creates C++ global variables
// ============================================================================
// Format: SENSOR_TYPE(S_NAME, "Display Name", "entity_id", [threshold])

// Binary Sensors (Gates)
SENSOR_BINARY(S_GATE1, "Sidewalk", "binary_sensor.aqara_door_and_window_sensor_p2_door_2")
SENSOR_BINARY(S_GATE2, "Driveway", "binary_sensor.aqara_door_and_window_sensor_p2_door_3")
SENSOR_BINARY(S_GATE3, "Side", "binary_sensor.aqara_door_and_window_sensor_p2_door")

// Text Sensors (Status strings)
SENSOR_TEXT(S_LOCK, "Lock", "lock.shed_lock")
SENSOR_TEXT(S_WEATHER, "Weather", "sensor.openweathermap_condition")
SENSOR_TEXT(S_CHARGER, "Charger", "sensor.tesla_wall_connector_status")

// Threshold Sensors (Numeric with change thresholds)
SENSOR_THRESHOLD(S_TEMPERATURE, "Temperature", "sensor.birgenshire_temp", 1.0)
SENSOR_THRESHOLD(S_SOLAR_POWER, "Solar Power", "sensor.birgenshire_solar_power", 0.5)
SENSOR_THRESHOLD(S_CHARGING_POWER, "Charging Power", "sensor.tesla_wall_connector_current_power", 100.0)

// Passive Sensors (Track HA connection, never trigger updates)
SENSOR_PASSIVE(S_SUN_ELEV, "Sun Elevation", "sun.sun")
SENSOR_PASSIVE(S_SOLAR_ENERGY, "Solar Energy Today", "sensor.solar_production_last_24h_2")
SENSOR_PASSIVE(S_HOME_CONSUMPTION, "Home Consumption Today", "sensor.home_consumption_last_24h_2")

// WiFi Sensor (ESPHome built-in, not HomeAssistant)
SENSOR_WIFI(S_WIFI_RSSI, "WiFi Signal", "wifisignal")

// ============================================================================
// SENSOR_INIT_ALL - Links C++ sensors to ESPHome sensors at boot
// ============================================================================
// Call this in on_boot lambda. ESPHome id (_var) is auto-generated via token pasting.
#define SENSOR_INIT_ALL() \
  SENSOR_INIT_BINARY(S_GATE1) \
  SENSOR_INIT_BINARY(S_GATE2) \
  SENSOR_INIT_BINARY(S_GATE3) \
  SENSOR_INIT_TEXT(S_LOCK) \
  SENSOR_INIT_TEXT(S_WEATHER) \
  SENSOR_INIT_TEXT(S_CHARGER) \
  SENSOR_INIT_THRESHOLD(S_TEMPERATURE) \
  SENSOR_INIT_THRESHOLD(S_SOLAR_POWER) \
  SENSOR_INIT_THRESHOLD(S_CHARGING_POWER) \
  SENSOR_INIT_PASSIVE(S_SUN_ELEV) \
  SENSOR_INIT_PASSIVE(S_SOLAR_ENERGY) \
  SENSOR_INIT_PASSIVE(S_HOME_CONSUMPTION) \
  SENSOR_INIT_WIFI(S_WIFI_RSSI)
