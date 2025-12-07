// ============================================================================
// HOMINK SENSOR DEFINITIONS
// ============================================================================
// To add a sensor: Add S_* define + SENSOR_* entry + YAML block with id: _varname

#include "homink_sensor.h"

// Sensor name definitions (ESPHome auto-generates id with _ prefix)

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

// Sensor declarations (format: SENSOR_TYPE(var, "Display Name", "entity_id", [threshold]))
// Binary sensors
SENSOR_BINARY(S_GATE1, "Sidewalk", "binary_sensor.aqara_door_and_window_sensor_p2_door_2")
SENSOR_BINARY(S_GATE2, "Driveway", "binary_sensor.aqara_door_and_window_sensor_p2_door_3")
SENSOR_BINARY(S_GATE3, "Side", "binary_sensor.aqara_door_and_window_sensor_p2_door")

// Text sensors
SENSOR_TEXT(S_LOCK, "Lock", "lock.shed_lock")
SENSOR_TEXT(S_WEATHER, "Weather", "sensor.openweathermap_condition")
SENSOR_TEXT_FILTERED(S_CHARGER, "Charger", "sensor.tesla_wall_connector_status", "unavailable")

// Threshold sensors
SENSOR_THRESHOLD(S_TEMPERATURE, "Temperature", "sensor.birgenshire_temp", 1.0)
SENSOR_THRESHOLD(S_SOLAR_POWER, "Solar Output", "sensor.birgenshire_solar_power", 0.5)
SENSOR_THRESHOLD(S_CHARGING_POWER, "Charging", "sensor.tesla_wall_connector_current_power", 100.0)

// Passive sensors (track HA connection, never trigger updates)
SENSOR_PASSIVE(S_SUN_ELEV, "Sun Elevation", "sun.sun")
SENSOR_PASSIVE(S_SOLAR_ENERGY, "Solar 24hr", "sensor.solar_production_last_24h_2")
SENSOR_PASSIVE(S_HOME_CONSUMPTION, "Home 24hr", "sensor.home_consumption_last_24h_2")

// WiFi sensor (ESPHome built-in)
SENSOR_WIFI(S_WIFI_RSSI, "WiFi Signal", "wifisignal")

// Init macro - links C++ sensors to ESPHome sensors (call in on_boot lambda)
#define SENSOR_INIT_ALL() \
  SENSOR_INIT_BINARY(S_GATE1) \
  SENSOR_INIT_BINARY(S_GATE2) \
  SENSOR_INIT_BINARY(S_GATE3) \
  SENSOR_INIT_TEXT(S_LOCK) \
  SENSOR_INIT_TEXT(S_WEATHER) \
  SENSOR_INIT_TEXT_FILTERED(S_CHARGER) \
  SENSOR_INIT_THRESHOLD(S_TEMPERATURE) \
  SENSOR_INIT_THRESHOLD(S_SOLAR_POWER) \
  SENSOR_INIT_THRESHOLD(S_CHARGING_POWER) \
  SENSOR_INIT_PASSIVE(S_SUN_ELEV) \
  SENSOR_INIT_PASSIVE(S_SOLAR_ENERGY) \
  SENSOR_INIT_PASSIVE(S_HOME_CONSUMPTION) \
  SENSOR_INIT_WIFI(S_WIFI_RSSI)
