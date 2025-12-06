#pragma once

#include "esphome.h"
#include <limits>  // For std::numeric_limits (sentinel values)
#include <cstring> // For std::strchr (entity_id checking)

// ============================================================================
// SENSOR STATE SYSTEM - Unified Linked List
// ============================================================================
// ISensor provides ONLY the infrastructure for a single unified linked list.
// All sensors (regardless of type) are in ONE list, enabling truly generic
// iteration: ISensor::update_all() or ISensor::check_all_for_changes()

// ============================================================================
// ISensor - Minimal base class (ONLY for linked list infrastructure)
// ============================================================================
// This class exists for ONE purpose: enable a single unified linked list
// containing all sensors regardless of their template types.
//
// It provides:
// 1. Linked list pointers (_next)
// 2. Static list head (_list_head)
// 3. Auto-registration in constructor
// 4. Static methods to iterate ALL sensors: update_all(), check_all_for_changes()
//
// It does NOT provide virtual methods for sensor operations - those stay
// in the templated base class where they belong.
class ISensor {
public:
  virtual ~ISensor() = default;
  
  // Pure virtual methods - templated base class implements these
  virtual void update() = 0;
  virtual bool should_trigger_update() = 0;
  virtual const char *name() const = 0;
  virtual const char *entity_id() const = 0;  // For generating update lists
  virtual void log_change(const char *reason) const = 0;

  // ========================================================================
  // STATIC METHODS - Iterate ALL sensors in ONE unified list
  // ========================================================================
  
  // Update ALL sensors (all types in one call!)
  static void update_all() {
    for (ISensor *sensor = _list_head; sensor; sensor = sensor->_next) {
      sensor->update();
    }
  }
  
  // Check ALL sensors for changes (all types in one call!)
  // Returns true if any sensor triggered an update
  static bool check_all_for_changes() {
    for (ISensor *sensor = _list_head; sensor; sensor = sensor->_next) {
      if (sensor->should_trigger_update()) {
        sensor->log_change("change detected - triggering update");
        return true;
      }
    }
    return false;
  }
  
  // Generate comma-separated list of Home Assistant entity_ids for polling
  // Only includes entities with '.' (excludes ESPHome built-ins like "wifisignal")
  static std::string get_ha_entity_list() {
    std::string list;
    for (ISensor *sensor = _list_head; sensor; sensor = sensor->_next) {
      const char *entity = sensor->entity_id();
      // Only include if it has a '.' (Home Assistant entity, not ESPHome built-in)
      if (std::strchr(entity, '.') != nullptr) {
        if (!list.empty()) {
          list += ",";
        }
        list += entity;
      }
    }
    return list;
  }

protected:
  // Constructor - automatically adds to unified linked list
  ISensor() : _next(nullptr) {
    if (!_list_head) {
      _list_head = this;
    } else {
      // Find tail and append
      ISensor *tail = _list_head;
      while (tail->_next) {
        tail = tail->_next;
      }
      tail->_next = this;
    }
  }

private:
  ISensor *_next;                    // Next sensor in unified list
  static ISensor *_list_head;        // Head of unified list (all sensors)
};

// Initialize static member - ONE list for ALL sensors
ISensor *ISensor::_list_head = nullptr;

// ============================================================================
// Templated base class - Contains ALL sensor logic
// ============================================================================
template<typename ValueType, typename SensorType>
class BaseSensor : public ISensor {
public:
  // Constructor
  BaseSensor(const char *n, const char *entity, ValueType initial_val = ValueType())
    : ISensor(),  // Calls ISensor constructor - auto-registers in unified list
      _name(n), _entity_id(entity), _has_state(false),
      _value(initial_val), _sensor(nullptr) {}
  
  virtual ~BaseSensor() = default;
  
  // Public getters (const)
  const char *name() const override { return _name; }
  const char *entity_id() const override { return _entity_id; }
  
  bool has_state() const { return _has_state; }
  const ValueType& value() const { return _value; }
  
  // Setter for sensor pointer (assigned from YAML)
  void set_sensor(SensorType *s) { _sensor = s; }
  
  // Public interface (called from YAML or macros)
  void log_change(const char *reason) const override {
    ESP_LOGD("main", "%s: %s", _name, reason);
  }
  
  void update() override {
    if (!_sensor) return;
    
    _has_state = _sensor && _sensor->has_state();
    
    if (_has_state) {
      update_value_from_sensor();
    }
  }
  
  // UNIFIED CHANGE DETECTION - Used by both callbacks and polling
  // Base class checks availability (common to all sensors)
  // Then calls derived class to check value significance
  bool should_trigger_update() override {
    if (!_sensor) return false;
    
    // Check if availability changed (ALWAYS significant)
    // This catches: available→unavailable (show "UNKNOWN") and unavailable→available
    bool current_has_state = _sensor->has_state();
    if (current_has_state != _has_state) {
      ESP_LOGD("main", "%s: Availability changed (%s -> %s)", 
               _name, 
               _has_state ? "available" : "unavailable",
               current_has_state ? "available" : "unavailable");
      return true;
    }
    
    // Sensor doesn't have data (and never did) - no change
    if (!current_has_state) return false;
    
    // Sensor has data - check if value change is significant (derived class decides)
    // NOTE: By this point we know: _sensor != null && sensor has state
    // Derived class doesn't need to check these again
    return is_value_change_significant();
  }

protected:
  // Protected helpers (only called internally or by derived classes)
  void update_value_from_sensor() {
    _value = _sensor->state;
  }
  
  // Pure virtual - derived classes define what "significant value change" means
  // Base class handles availability, derived class handles value
  virtual bool is_value_change_significant() = 0;
  
  // Protected direct access to private members for derived classes
  SensorType *_get_sensor() { return _sensor; }
  ValueType& _get_value() { return _value; }

private:
  // Private data members (leading underscore convention)
  const char *_name;
  const char *_entity_id;
  bool _has_state;
  ValueType _value;
  SensorType *_sensor;
};

// ============================================================================
// StateSensor - Simple sensors (gates, lock, weather, charger)
// ============================================================================
template<typename ValueType, typename SensorType>
class StateSensor : public BaseSensor<ValueType, SensorType> {
public:
  StateSensor(const char *n, const char *entity, ValueType initial_val = ValueType())
    : BaseSensor<ValueType, SensorType>(n, entity, initial_val) {}

protected:
  // For stateful sensors: ANY value change is significant
  // NOTE: Base class guarantees _sensor exists and has state when this is called
  bool is_value_change_significant() override {
    return this->_get_sensor()->state != this->_get_value();
  }
};

// ============================================================================
// Type aliases for common sensor types
// ============================================================================
// Binary sensors (gates, motion, etc.)
using BinaryStateSensor = StateSensor<bool, esphome::homeassistant::HomeassistantBinarySensor>;

// Text sensors (status strings, weather conditions, etc.)
using TextStateSensor = StateSensor<std::string, esphome::homeassistant::HomeassistantTextSensor>;

// ============================================================================
// Threshold sensor - Only triggers on threshold-exceeding changes
// ============================================================================
template<typename ValueType, typename SensorType>
class ThresholdSensor : public BaseSensor<ValueType, SensorType> {
public:
  ThresholdSensor(const char *n, const char *entity, ValueType t)
    : BaseSensor<ValueType, SensorType>(n, entity, std::numeric_limits<ValueType>::max()),
      _threshold(t) {}

protected:
  // For threshold sensors: Only trigger if change exceeds threshold
  // NOTE: Base class guarantees _sensor exists and has state when this is called
  // NOTE: ValueType must support arithmetic operations (numeric types: int, float, double)
  bool is_value_change_significant() override {
    ValueType current = this->_get_sensor()->state;
    
    // First reading - check if still at sentinel value (uninitialized)
    // Using max() as sentinel means "no baseline set yet"
    if (this->_get_value() == std::numeric_limits<ValueType>::max()) {
      this->_get_value() = current;
      ESP_LOGD("main", "%s: Initialized with first value - triggering update", this->name());
      return true;
    }
    
    // Check if change exceeds threshold (requires numeric ValueType)
    if (std::abs(current - this->_get_value()) > _threshold) {
      ESP_LOGD("main", "%s: Threshold exceeded - triggering update", this->name());
      this->_get_value() = current;
      return true;
    }
    
    return false;
  }

private:
  ValueType _threshold;
};

// Type alias for float-based threshold sensors
using FloatThresholdSensor = ThresholdSensor<float, esphome::homeassistant::HomeassistantSensor>;

// ============================================================================
// Passive sensor - Tracks connection time but never triggers display updates
// ============================================================================
// Use for sensors that need to be monitored (for HA connection tracking) but
// should never trigger display refreshes (e.g., sun_elevation, energy totals)
template<typename ValueType, typename SensorType>
class PassiveSensor : public BaseSensor<ValueType, SensorType> {
public:
  PassiveSensor(const char *n, const char *entity, ValueType initial = ValueType())
    : BaseSensor<ValueType, SensorType>(n, entity, initial) {}

protected:
  // Passive sensors NEVER trigger display updates
  // They track HA connection time (via callback) but don't affect display
  bool is_value_change_significant() override {
    return false;  // Never significant - never triggers update
  }
};

// Type alias for float-based passive sensors (most common use case)
using FloatPassiveSensor = PassiveSensor<float, esphome::homeassistant::HomeassistantSensor>;

// Type alias for WiFi signal sensor (ESPHome built-in, not HomeAssistant)
using WiFiPassiveSensor = PassiveSensor<float, esphome::wifi_signal::WiFiSignalSensor>;

// ============================================================================
// MACROS
// ============================================================================

// Unified callback for ALL sensor types (binary, text, threshold)
// Uses sensor's should_trigger_update() which checks BOTH availability and value
#define SENSOR_UPDATE_CALLBACK(sensor_var) \
  id(last_ha_connection_time) = id(homeassistant_time).now().timestamp; \
  if (!id(ha_connected)) { \
    ESP_LOGD("main", "Received sensor data - marking HA as connected"); \
    id(ha_connected) = true; \
  } \
  if (id(data_updated)) return; \
  if (sensor_var.should_trigger_update()) { \
    ESP_LOGD("main", "%s: Value changed - triggering update", sensor_var.name()); \
    id(data_updated) = true; \
  }
