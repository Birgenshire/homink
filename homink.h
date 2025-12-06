// ============================================================================
// HOMINK PROJECT-SPECIFIC SENSOR DEFINITIONS  
// ============================================================================

#include "homink_sensor.h"

// ============================================================================
// SENSOR DECLARATIONS
// ============================================================================

// Binary Sensors (Gates)
BinaryStateSensor gate1("Sidewalk", "binary_sensor.aqara_door_and_window_sensor_p2_door_2");
BinaryStateSensor gate2("Driveway", "binary_sensor.aqara_door_and_window_sensor_p2_door_3");
BinaryStateSensor gate3("Side", "binary_sensor.aqara_door_and_window_sensor_p2_door");

// Text Sensors (Status strings)
TextStateSensor lock("Lock", "lock.shed_lock");
TextStateSensor weather("Weather", "sensor.openweathermap_condition");
TextStateSensor charger("Charger", "sensor.tesla_wall_connector_status");

// Threshold Sensors (Numeric values with change thresholds)
FloatThresholdSensor temperature("Temperature", "sensor.birgenshire_temp", 1.0);
FloatThresholdSensor solar_power("Solar Power", "sensor.birgenshire_solar_power", 0.5);
FloatThresholdSensor charging_power("Charging Power", "sensor.tesla_wall_connector_current_power", 100.0);

// Passive Sensors (Track HA connection but never trigger display updates)
FloatPassiveSensor sun_elev("Sun Elevation", "sun.sun");
FloatPassiveSensor solar_energy("Solar Energy Today", "sensor.solar_production_last_24h_2");
FloatPassiveSensor home_consumption("Home Consumption Today", "sensor.home_consumption_last_24h_2");
WiFiPassiveSensor wifi_rssi("WiFi Signal", "wifisignal");
