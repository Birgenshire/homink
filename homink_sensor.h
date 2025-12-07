#pragma once

#include "esphome.h"
#include <limits>
#include <cstring>

// ============================================================================
// SENSOR STATE SYSTEM
// ============================================================================
// Unified linked list containing all sensors regardless of type.
// Enables generic iteration via ISensor::update_all() and check_all_for_changes().

// ISensor - Base class for linked list infrastructure
class ISensor {
public:
  virtual ~ISensor() = default;

  // Pure virtual methods implemented by templated base class
  virtual void update() = 0;
  virtual bool should_trigger_update() = 0;
  virtual const char *name() const = 0;
  virtual const char *entity_id() const = 0;
  virtual void log_change(const char *reason) const = 0;

  // Static methods - iterate all sensors in unified list
  static void update_all() {
    for (ISensor *sensor = _list_head; sensor; sensor = sensor->_next) {
      sensor->update();
    }
  }

  static bool check_all_for_changes() {
    for (ISensor *sensor = _list_head; sensor; sensor = sensor->_next) {
      if (sensor->should_trigger_update()) {
        sensor->log_change("change detected - triggering update");
        return true;
      }
    }
    return false;
  }

  // Generate comma-separated list of HA entity_ids (excludes ESPHome built-ins)
  static std::string get_ha_entity_list() {
    std::string list;
    for (ISensor *sensor = _list_head; sensor; sensor = sensor->_next) {
      const char *entity = sensor->entity_id();
      if (std::strchr(entity, '.') != nullptr) {  // Has '.' = HA entity
        if (!list.empty()) {
          list += ",";
        }
        list += entity;
      }
    }
    return list;
  }

protected:
  ISensor() : _next(nullptr) {
    if (!_list_head) {
      _list_head = this;
    } else {
      ISensor *tail = _list_head;
      while (tail->_next) {
        tail = tail->_next;
      }
      tail->_next = this;
    }
  }

private:
  ISensor *_next;
  static ISensor *_list_head;
};

ISensor *ISensor::_list_head = nullptr;

// BaseSensor - Templated sensor base class with common logic
template<typename ValueType, typename SensorType>
class BaseSensor : public ISensor {
public:
  BaseSensor(const char *n, const char *entity, ValueType initial_val = ValueType())
    : ISensor(), _name(n), _entity_id(entity), _has_state(false),
      _value(initial_val), _sensor(nullptr) {}

  virtual ~BaseSensor() = default;

  const char *name() const override { return _name; }
  const char *entity_id() const override { return _entity_id; }

  bool has_state() const { return _has_state; }
  const ValueType& value() const { return _value; }

  void set_sensor(SensorType *s) { _sensor = s; }

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

  // Check for changes - base class checks availability, derived class checks value
  bool should_trigger_update() override {
    if (!_sensor) return false;

    bool current_has_state = _sensor->has_state();
    if (current_has_state != _has_state) {
      ESP_LOGD("main", "%s: Availability changed (%s -> %s)",
               _name,
               _has_state ? "available" : "unavailable",
               current_has_state ? "available" : "unavailable");
      return true;
    }

    if (!current_has_state) return false;

    return is_value_change_significant();
  }

protected:
  void update_value_from_sensor() {
    _value = _sensor->state;
  }

  virtual bool is_value_change_significant() = 0;

  SensorType *_get_sensor() { return _sensor; }
  ValueType& _get_value() { return _value; }

private:
  const char *_name;
  const char *_entity_id;
  bool _has_state;
  ValueType _value;
  SensorType *_sensor;
};

// StateSensor - Any value change triggers update
template<typename ValueType, typename SensorType>
class StateSensor : public BaseSensor<ValueType, SensorType> {
public:
  StateSensor(const char *n, const char *entity, ValueType initial_val = ValueType())
    : BaseSensor<ValueType, SensorType>(n, entity, initial_val) {}

protected:
  bool is_value_change_significant() override {
    return this->_get_sensor()->state != this->_get_value();
  }
};

// Type aliases
using BinaryStateSensor = StateSensor<bool, esphome::homeassistant::HomeassistantBinarySensor>;
using TextStateSensor = StateSensor<std::string, esphome::homeassistant::HomeassistantTextSensor>;

// ThresholdSensor - Only triggers when change exceeds threshold
template<typename ValueType, typename SensorType>
class ThresholdSensor : public BaseSensor<ValueType, SensorType> {
public:
  ThresholdSensor(const char *n, const char *entity, ValueType t)
    : BaseSensor<ValueType, SensorType>(n, entity, std::numeric_limits<ValueType>::max()),
      _threshold(t) {}

protected:
  bool is_value_change_significant() override {
    ValueType current = this->_get_sensor()->state;

    if (this->_get_value() == std::numeric_limits<ValueType>::max()) {
      this->_get_value() = current;
      ESP_LOGD("main", "%s: Initialized with first value - triggering update", this->name());
      return true;
    }

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

using FloatThresholdSensor = ThresholdSensor<float, esphome::homeassistant::HomeassistantSensor>;

// PassiveSensor - Tracks HA connection but never triggers display updates
template<typename ValueType, typename SensorType>
class PassiveSensor : public BaseSensor<ValueType, SensorType> {
public:
  PassiveSensor(const char *n, const char *entity, ValueType initial = ValueType())
    : BaseSensor<ValueType, SensorType>(n, entity, initial) {}

protected:
  bool is_value_change_significant() override {
    return false;
  }
};

using FloatPassiveSensor = PassiveSensor<float, esphome::homeassistant::HomeassistantSensor>;
using WiFiPassiveSensor = PassiveSensor<float, esphome::wifi_signal::WiFiSignalSensor>;

// FilteredTextStateSensor - Ignores transitions to/from specific state values
class FilteredTextStateSensor : public BaseSensor<std::string, esphome::homeassistant::HomeassistantTextSensor> {
public:
  FilteredTextStateSensor(const char *n, const char *entity, const char *ignored_value)
    : BaseSensor<std::string, esphome::homeassistant::HomeassistantTextSensor>(n, entity, ""),
      _ignored_value(ignored_value) {}

protected:
  bool is_value_change_significant() override {
    std::string current = _get_sensor()->state;
    std::string cached = _get_value();

    if (current == _ignored_value) {
      ESP_LOGD("main", "%s: Ignoring transition to '%s'", name(), _ignored_value);
      return false;
    }

    if (cached == _ignored_value) {
      ESP_LOGD("main", "%s: Ignoring transition from '%s'", name(), _ignored_value);
      return false;
    }

    return current != cached;
  }

private:
  const char *_ignored_value;
};

// ============================================================================
// MACROS
// ============================================================================

// Unified callback - checks availability and value changes
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

// ============================================================================
// X-MACRO SYSTEM
// ============================================================================
// SENSOR_* macros expand to C++ variable declarations.
// SENSOR_INIT_* macros expand to set_sensor() calls.
// ESPHome prepends "_" to variable names (gate1 → _gate1).
#define SENSOR_BINARY(var, name, entity)                 BinaryStateSensor var(name, entity);
#define SENSOR_TEXT(var, name, entity)                   TextStateSensor var(name, entity);
#define SENSOR_TEXT_FILTERED(var, name, entity, ignored) FilteredTextStateSensor var(name, entity, ignored);
#define SENSOR_THRESHOLD(var, name, entity, thresh)      FloatThresholdSensor var(name, entity, thresh);
#define SENSOR_PASSIVE(var, name, entity)                FloatPassiveSensor var(name, entity);
#define SENSOR_WIFI(var, name, entity)                   WiFiPassiveSensor var(name, entity);

#define _SENSOR_INIT(var)              var.set_sensor(&id(_##var));
#define SENSOR_INIT_BINARY(var)        _SENSOR_INIT(var)
#define SENSOR_INIT_TEXT(var)          _SENSOR_INIT(var)
#define SENSOR_INIT_TEXT_FILTERED(var) _SENSOR_INIT(var)
#define SENSOR_INIT_THRESHOLD(var)     _SENSOR_INIT(var)
#define SENSOR_INIT_PASSIVE(var)       _SENSOR_INIT(var)
#define SENSOR_INIT_WIFI(var)          _SENSOR_INIT(var)
