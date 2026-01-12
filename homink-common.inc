# vim: ft=yaml
#
# ═══════════════════════════════════════════════════════════════════════════════
# HOMINK COMMON CONFIGURATION
# ═══════════════════════════════════════════════════════════════════════════════
# Shared config for all Homink e-ink dashboards. Device-specific files provide:
# - device_name, device_ip
# - tesla_power_factor (installation-specific)
# - ha_timeout_seconds (HA connection timeout)
# - Sensor definitions (*_var, *_entity for all sensors)
#
# Display Sections: Weather (temp, condition, wifi) | Energy (solar, home, EV) |
#                   Gates (3 gates with lock detection) | Footer (refresh time)
#
# Update Strategy: Push (instant on state change) + Poll (15s backup) + Threshold
#                  (temp ≥1°F, solar ≥0.5kW, charging ≥100W) + Forced (30min)
# ═══════════════════════════════════════════════════════════════════════════════

esphome:
  on_boot:
      priority: 200.0
      then:
        - lambda: 'SENSOR_INIT_ALL();'  # X-macro initializes all sensors
        - lambda: 'ISensor::validate_all();'  # Log any initialization failures
        - logger.log: "Boot complete, triggering initial display update..."
        - script.execute: update_screen

esp32:
  board: esp32dev
  framework:
    type: arduino

logger:

api:

ota:
  platform: esphome

button:
  - platform: shutdown
    name: "${device_name} - Shutdown"
  - platform: restart
    name: "${device_name} - Restart"
  - platform: template
    name: "${device_name} - Refresh Screen"
    entity_category: config
    on_press:
      - logger.log: "Manual refresh button pressed"
      - script.execute: update_screen

globals:
  - id: data_updated
    type: bool
    restore_value: no
    initial_value: 'false'

  - id: recorded_display_refresh
    type: int
    restore_value: yes
    initial_value: '0'

  - id: tesla_power_factor  # Installation-specific: apparent → real power
    type: float
    restore_value: no
    initial_value: '${tesla_power_factor}'

  - id: last_display_refresh_time
    type: long
    restore_value: yes
    initial_value: '0'

  - id: threshold_forced_refresh_interval  # Force refresh after this many seconds
    type: long
    restore_value: no
    initial_value: '${forced_refresh_interval_seconds}'

  - id: threshold_ha_timeout  # HA connection timeout (no sensor updates)
    type: long
    restore_value: no
    initial_value: '${ha_timeout_seconds}'

  - id: threshold_min_update_interval  # Minimum seconds between display refreshes
    type: long
    restore_value: no
    initial_value: '${min_update_interval_seconds}'

  - id: ha_connected
    type: bool
    restore_value: no
    initial_value: 'false'

  - id: last_ha_connection_time
    type: long
    restore_value: yes
    initial_value: '0'

script:
  - id: update_screen
    mode: single  # Prevent overlapping executions (default, but explicit for clarity)
    then:
      - lambda: 'id(data_updated) = false;'  # Close race condition window

      # Poll HA for latest values
      - homeassistant.service:
          service: homeassistant.update_entity
          data:
            entity_id: !lambda 'return ISensor::get_ha_entity_list();'

      - delay: 2s  # Wait for HA to respond

      # Cache timestamp and sensor values
      - lambda: |-
          long refresh_time = id(homeassistant_time).now().timestamp;
          id(last_display_refresh_time) = refresh_time;
          id(display_last_update).publish_state(refresh_time);
          ESP_LOGD("main", "Caching values at timestamp: %ld", refresh_time);

      - lambda: 'ISensor::update_all();'
      - component.update: eink_display  # Blocks ~4s
      - lambda: 'id(recorded_display_refresh) += 1;'

# Polling loop: Runs every min_update_interval_seconds as backup for missed callbacks
# Also handles forced refresh (30 min) and HA timeout detection
time:
  - platform: homeassistant
    id: homeassistant_time
    on_time:
      - seconds: /${min_update_interval_seconds}
        then:
          - if:
              condition:
                lambda: 'return id(data_updated) == false;'
              then:
                # Poll HA for sensor updates
                - homeassistant.service:
                    service: homeassistant.update_entity
                    data:
                      entity_id: !lambda 'return ISensor::get_ha_entity_list();'

                - delay: 2s

                # Check all sensors for changes
                - lambda: |-
                    if (ISensor::check_all_for_changes()) {
                      id(data_updated) = true;
                      return;
                    }

                # Forced refresh and HA timeout checks
                - if:
                    condition:
                      lambda: 'return id(data_updated) == false;'
                    then:
                      - lambda: |-
                          long current_time = id(homeassistant_time).now().timestamp;
                          long time_since_last_refresh = current_time - id(last_display_refresh_time);
                          long time_since_last_ha_update = current_time - id(last_ha_connection_time);

                          // HA timeout: no sensor updates for configured duration
                          if (id(last_ha_connection_time) > 0 && time_since_last_ha_update >= id(threshold_ha_timeout)) {
                            if (id(ha_connected)) {
                              ESP_LOGW("main", "No HA updates for %ld seconds - marking disconnected", time_since_last_ha_update);
                              id(ha_connected) = false;
                            }
                          }

                          // Edge cases
                          if (id(last_display_refresh_time) == 0) return;
                          if (time_since_last_refresh < 0) {
                            ESP_LOGW("main", "Clock went backward - resetting refresh time");
                            id(last_display_refresh_time) = current_time;
                            return;
                          }

                          // Forced refresh every 30 minutes
                          if (time_since_last_refresh >= id(threshold_forced_refresh_interval)) {
                            ESP_LOGD("main", "Forced refresh: %ld seconds elapsed", time_since_last_refresh);
                            id(data_updated) = true;
                          }
              else:
                - logger.log: "Update already pending, skipping poll"

          # Execute refresh if needed
          - if:
              condition:
                lambda: 'return id(data_updated) == true;'
              then:
                - logger.log: "Sensor data updated: Refreshing display..."
                - script.execute: update_screen
              else:
                - logger.log: "No significant changes - skipping refresh"

wifi:
  ssid: !secret wifi_ssid
  password: !secret wifi_password
  on_connect:
    - lambda: 'ESP_LOGD("wifi", "WiFi connected");'
  on_disconnect:
    - lambda: |-
        ESP_LOGD("wifi", "WiFi disconnected");
        id(ha_connected) = false;
        id(last_ha_connection_time) = id(homeassistant_time).now().timestamp;

font:
  - file: 'fonts/GothamRnd-Book.ttf'
    id: font_small_book
    size: 18
    bpp: 1

  - file: 'fonts/GothamRnd-Bold.ttf'
    id: font_large_bold
    size: 96
    bpp: 1
    glyphs: [' ', '-', '°', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'C', 'F']

  - file: 'fonts/GothamRnd-Bold.ttf'
    id: font_title
    size: 44
    bpp: 1
    glyphs: ['W', 'E', 'A', 'T', 'H', 'R', 'S', 'O', 'L', 'G', 'N', 'I', 'Y', ' ']

  - file: 'fonts/GothamRnd-Bold.ttf'
    id: font_medium_bold
    size: 32
    bpp: 1

  - file: 'fonts/GothamRnd-Bold.ttf'
    id: font_small_bold
    size: 20
    bpp: 1

  - file: 'fonts/GothamRnd-Book.ttf'
    id: font_name
    bpp: 1
    size: 24

  - file: 'fonts/materialdesignicons-webfont.ttf'
    id: font_mdi_large
    size: 84
    bpp: 1
    glyphs:
      - "\U000F0590" # weather-cloudy
      - "\U000F0F2F" # weather-cloudy-alert
      - "\U000F0E6E" # weather-cloudy-arrow-right
      - "\U000F0591" # weather-fog
      - "\U000F0592" # weather-hail
      - "\U000F0F30" # weather-hazy
      - "\U000F0898" # weather-hurricane
      - "\U000F0593" # weather-lightning
      - "\U000F067E" # weather-lightning-rainy
      - "\U000F0594" # weather-night
      - "\U000F0F31" # weather-night-partly-cloudy
      - "\U000F0595" # weather-partly-cloudy
      - "\U000F0F32" # weather-partly-lightning
      - "\U000F0F33" # weather-partly-rainy
      - "\U000F0F34" # weather-partly-snowy
      - "\U000F0F35" # weather-partly-snowy-rainy
      - "\U000F0596" # weather-pouring
      - "\U000F0597" # weather-rainy
      - "\U000F0598" # weather-snowy
      - "\U000F0F36" # weather-snowy-heavy
      - "\U000F067F" # weather-snowy-rainy
      - "\U000F0599" # weather-sunny
      - "\U000F0F37" # weather-sunny-alert
      - "\U000F14E4" # weather-sunny-off
      - "\U000F059A" # weather-sunset
      - "\U000F059B" # weather-sunset-down
      - "\U000F059C" # weather-sunset-up
      - "\U000F0F38" # weather-tornado
      - "\U000F059D" # weather-windy
      - "\U000F059E" # weather-windy-variant
      - "\U000F0026" # alert-circle-outline

  - file: 'fonts/materialdesignicons-webfont.ttf'
    id: font_mdi_medium
    size: 42
    bpp: 1
    glyphs:
      - "\U000F0A72" # solar-power
      - "\U000F140C" # lightning-bolt
      - "\U000F02DC" # home
      - "\U000F081A" # door-closed
      - "\U000F081C" # door-open
      - "\U000F033F" # lock-open-variant
      - "\U000F007D" # ev-station
      - "\U000F0079" # battery
      - "\U000F151C" # power-plug-off
      - "\U000F0205" # help-circle

  - file: 'fonts/materialdesignicons-webfont.ttf'
    id: font_mdi_small
    size: 20
    bpp: 1
    glyphs:
      - "\U000F091F" # wifi-strength-1
      - "\U000F0922" # wifi-strength-2
      - "\U000F0925" # wifi-strength-3
      - "\U000F0928" # wifi-strength-4
      - "\U000F092D" # wifi-strength-off

binary_sensor:
  - platform: homeassistant
    entity_id: ${gate1_entity}
    id: _${gate1_var}
    on_state:
      then:
        - lambda: 'SENSOR_UPDATE_CALLBACK(${gate1_var});'

  - platform: homeassistant
    entity_id: ${gate2_entity}
    id: _${gate2_var}
    on_state:
      then:
        - lambda: 'SENSOR_UPDATE_CALLBACK(${gate2_var});'

  - platform: homeassistant
    entity_id: ${gate3_entity}
    id: _${gate3_var}
    on_state:
      then:
        - lambda: 'SENSOR_UPDATE_CALLBACK(${gate3_var});'

text_sensor:
  - platform: homeassistant
    entity_id: ${lock_entity}
    id: _${lock_var}
    on_value:
      then:
        - lambda: 'SENSOR_UPDATE_CALLBACK(${lock_var});'

  - platform: homeassistant
    entity_id: ${weather_entity}
    id: _${weather_var}
    on_value:
      then:
        - lambda: 'SENSOR_UPDATE_CALLBACK(${weather_var});'

  - platform: homeassistant
    entity_id: ${charger_entity}
    id: _${charger_var}
    on_value:
      then:
        - lambda: 'SENSOR_UPDATE_CALLBACK(${charger_var});'

sensor:
  - platform: template
    name: "${device_name} - Display Last Update"
    device_class: timestamp
    entity_category: "diagnostic"
    id: display_last_update

  - platform: template
    name: "${device_name} - Recorded Display Refresh"
    accuracy_decimals: 0
    unit_of_measurement: "Refreshes"
    state_class: "total_increasing"
    entity_category: "diagnostic"
    lambda: 'return id(recorded_display_refresh);'

  - platform: homeassistant
    entity_id: ${refreshes_24h_entity}
    id: refreshes_last_24h_ha
    internal: false
    name: "${device_name} - Refreshes Last 24h"
    unit_of_measurement: "Refreshes"
    accuracy_decimals: 0
    state_class: "measurement"
    entity_category: "diagnostic"

  - platform: wifi_signal
    name: "${device_name} - WiFi Signal Strength"
    id: _${wifi_rssi_var}
    unit_of_measurement: "dBm"
    entity_category: "diagnostic"
    update_interval: 60s

  - platform: homeassistant
    entity_id: ${temperature_entity}
    id: _${temperature_var}
    on_value:
      then:
        - lambda: 'SENSOR_UPDATE_CALLBACK(${temperature_var});'

  - platform: homeassistant
    entity_id: ${solar_power_entity}
    id: _${solar_power_var}
    on_value:
      then:
        - lambda: 'SENSOR_UPDATE_CALLBACK(${solar_power_var});'

  - platform: homeassistant
    entity_id: ${charging_power_entity}
    id: _${charging_power_var}
    on_value:
      then:
        - lambda: 'SENSOR_UPDATE_CALLBACK(${charging_power_var});'

  - platform: homeassistant
    entity_id: ${sun_elev_entity}
    attribute: elevation
    id: _${sun_elev_var}
    on_value:
      then:
        - lambda: 'SENSOR_UPDATE_CALLBACK(${sun_elev_var});'

  - platform: homeassistant
    entity_id: ${solar_energy_entity}
    id: _${solar_energy_var}
    on_value:
      then:
        - lambda: 'SENSOR_UPDATE_CALLBACK(${solar_energy_var});'

  - platform: homeassistant
    entity_id: ${home_consumption_entity}
    id: _${home_consumption_var}
    on_value:
      then:
        - lambda: 'SENSOR_UPDATE_CALLBACK(${home_consumption_var});'

color:
  - id: color_bg
    red: 0%
    green: 0%
    blue: 0%
    white: 0%
  - id: color_text
    red: 0%
    green: 0%
    blue: 0%
    white: 100%

spi:
  clk_pin: GPIO13
  mosi_pin: GPIO14

# Waveshare 7.5" e-Paper V2 (800x480, B/W, 7.50inv2)
display:
  - platform: waveshare_epaper
    id: eink_display
    cs_pin: GPIO15
    dc_pin: GPIO27
    busy_pin:
      number: GPIO25
      inverted: true
    reset_pin: GPIO26
    reset_duration: 10ms
    model: 7.50inv2
    update_interval: never
    rotation: 90°
    lambda: |-
      // =========================================================================
      // DISPLAY PHYSICAL CONSTRAINTS - DO NOT RENDER OUTSIDE VISIBLE AREA
      // =========================================================================
      // Display is mounted behind a mat/frame that obscures edges.
      // Physical spec: 0.205mm dot pitch = 123.9 DPI
      //
      // VISIBLE AREA (portrait orientation after 90° rotation):
      //   Top boundary:    Y = 60   (content above this is hidden by mat)
      //   Bottom boundary: Y = 741  (content below this is hidden by mat)
      //   Visible height:  681 pixels (741 - 60)
      //   Full width:      480 pixels (no horizontal cropping)
      //
      // Uncomment these lines to see the mat boundaries during development:
      // it.line(0, 60, 480, 60, color_text);    // Top boundary
      // it.line(0, 741, 480, 741, color_text);  // Bottom boundary
      // =========================================================================

      // =========================================================================
      // LAYOUT CONSTANTS
      // =========================================================================
      // Display boundaries (visible area within frame)
      constexpr int DISPLAY_WIDTH = 480;
      constexpr int VISIBLE_Y_MIN = 60;
      constexpr int VISIBLE_Y_MAX = 741;

      // X positions (horizontal layout)
      constexpr int X_CENTER = 240;
      constexpr int X_LEFT_MARGIN = 40;
      constexpr int X_RIGHT_MARGIN = 440;
      constexpr int X_ROW_ICON = 60;
      constexpr int X_ROW_LABEL = 120;
      constexpr int X_ROW_VALUE = 420;
      constexpr int X_WIFI_ICON = 448;

      // Section title Y positions
      constexpr int Y_WEATHER_TITLE = 85;
      constexpr int Y_ENERGY_TITLE = 268;
      constexpr int Y_GATES_TITLE = 521;

      // Weather section
      constexpr int Y_WEATHER_CONTENT = 140;
      constexpr int X_WEATHER_ICON = 100;
      constexpr int X_TEMPERATURE = 300;
      constexpr int Y_WIFI_ICON = 70;

      // Divider lines
      constexpr int Y_DIVIDER_1 = 250;
      constexpr int Y_DIVIDER_2 = 503;

      // Energy section row Y positions (icon is ~3px above label/value for visual alignment)
      constexpr int Y_SOLAR_OUTPUT_ICON = 339;
      constexpr int Y_SOLAR_OUTPUT_TEXT = 342;
      constexpr int Y_SOLAR_24HR_ICON = 382;
      constexpr int Y_SOLAR_24HR_TEXT = 385;
      constexpr int Y_HOME_24HR_ICON = 425;
      constexpr int Y_HOME_24HR_TEXT = 428;
      constexpr int Y_CHARGING_ICON = 468;
      constexpr int Y_CHARGING_TEXT = 471;

      // Gates section row Y positions
      constexpr int Y_GATE1_ICON = 592;
      constexpr int Y_GATE1_TEXT = 595;
      constexpr int Y_GATE2_ICON = 640;
      constexpr int Y_GATE2_TEXT = 643;
      constexpr int Y_GATE3_ICON = 688;
      constexpr int Y_GATE3_TEXT = 691;

      // Footer
      constexpr int Y_FOOTER = 721;
      // =========================================================================

      // Helper: Check if nighttime (sun elevation < -6° or 8PM-6AM fallback)
      auto is_nighttime = []() -> bool {
        if (sun_elev.has_state()) return sun_elev.value() < -6.0;
        auto time = id(homeassistant_time).now();
        return (time.hour >= 20 || time.hour < 6);
      };

      // Helper: Check if sunrise/sunset (golden hour: -6° to +6°)
      auto is_sunset = []() -> bool {
        if (sun_elev.has_state()) {
          float elev = sun_elev.value();
          return (elev >= -6.0 && elev <= 6.0);
        }
        return false;
      };

      // Weather icon map - Home Assistant weather conditions (static to avoid reallocation on every render)
      static const std::map<std::string, std::string> weather_icon_map {
        // Standard conditions
        {"clear", "\U000F0599"},
        {"clear-night", "\U000F0594"},
        {"cloudy", "\U000F0590"},
        {"cloudy-alert", "\U000F0F2F"},
        {"fog", "\U000F0591"},
        {"hail", "\U000F0592"},
        {"hazy", "\U000F0F30"},
        {"hurricane", "\U000F0898"},
        {"lightning", "\U000F0593"},
        {"lightning-rainy", "\U000F067E"},
        {"night", "\U000F0594"},
        {"night-partly-cloudy", "\U000F0F31"},
        {"partlycloudy", "\U000F0595"},
        {"partly-cloudy", "\U000F0595"},
        {"pouring", "\U000F0596"},
        {"rainy", "\U000F0597"},
        {"snowy", "\U000F0598"},
        {"snowy-heavy", "\U000F0F36"},
        {"snowy-rainy", "\U000F067F"},
        {"sunny", "\U000F0599"},
        {"sunset", "\U000F059A"},
        {"tornado", "\U000F0F38"},
        {"windy", "\U000F059D"},
        // OpenWeatherMap-specific alternatives
        {"clearsky", "\U000F0599"},
        {"clear_day", "\U000F0599"},
        {"clear_night", "\U000F0594"},
        {"few_clouds", "\U000F0595"},
        {"scattered_clouds", "\U000F0595"},
        {"broken_clouds", "\U000F0590"},
        {"shower_rain", "\U000F0597"},
        {"rain", "\U000F0597"},
        {"thunderstorm", "\U000F0593"},
        {"snow", "\U000F0598"},
        {"mist", "\U000F0591"},
        {"drizzle", "\U000F0597"},
        {"smoke", "\U000F0591"},
        {"haze", "\U000F0F30"},
        {"dust", "\U000F0F30"},
        {"ash", "\U000F0591"},
        {"squall", "\U000F059D"},
      };

      // === WEATHER SECTION ===
      it.printf(X_CENTER, Y_WEATHER_TITLE, id(font_title), color_text, TextAlign::TOP_CENTER, "WEATHER");

      // WiFi signal indicator (upper right)
      std::string wifi_icon = "\U000F092D";  // Default: off
      if (wifi_rssi.has_state()) {
        float rssi = wifi_rssi.value();
        if (rssi > -50) wifi_icon = "\U000F0928";      // 4 bars
        else if (rssi > -60) wifi_icon = "\U000F0925"; // 3 bars
        else if (rssi > -70) wifi_icon = "\U000F0922"; // 2 bars
        else wifi_icon = "\U000F091F";                 // 1 bar
      }
      it.printf(X_WIFI_ICON, Y_WIFI_ICON, id(font_mdi_small), color_text, TextAlign::TOP_RIGHT, "%s", wifi_icon.c_str());

      // Weather icon with day/night/sunset logic
      if (weather.has_state() && weather.value() != "") {
        std::string condition = weather.value();
        std::string icon_to_use = weather_icon_map.count(condition) > 0
          ? weather_icon_map.at(condition) : "\U000F0026";  // Alert if unknown

        // Apply time-of-day logic for clear/partly-cloudy
        if (condition == "clear" || condition == "clearsky" || condition == "sunny" ||
            condition == "clear_day" || condition == "clear_night") {
          if (is_sunset()) icon_to_use = "\U000F059A";      // Sunset
          else if (is_nighttime()) icon_to_use = "\U000F0594"; // Moon
        } else if (is_nighttime() && (condition == "partlycloudy" || condition == "partly-cloudy" ||
                                       condition == "few_clouds" || condition == "scattered_clouds" ||
                                       condition == "broken_clouds")) {
          icon_to_use = "\U000F0F31";  // Night partly cloudy
        }

        it.printf(X_WEATHER_ICON, Y_WEATHER_CONTENT, id(font_mdi_large), color_text, TextAlign::TOP_CENTER, "%s", icon_to_use.c_str());
      } else {
        it.printf(X_WEATHER_ICON, Y_WEATHER_CONTENT, id(font_mdi_large), color_text, TextAlign::TOP_CENTER, "\U000F0026");
      }

      // Temperature
      if (temperature.has_state()) {
        if (temperature.value() >= 100 || temperature.value() <= -10) {
          it.printf(X_TEMPERATURE, Y_WEATHER_CONTENT, id(font_large_bold), color_text, TextAlign::TOP_CENTER, "%.0f°F", temperature.value());
        } else {
          it.printf(X_TEMPERATURE, Y_WEATHER_CONTENT, id(font_large_bold), color_text, TextAlign::TOP_CENTER, "%2.0f°F", temperature.value());
        }
      } else {
        it.printf(X_TEMPERATURE, Y_WEATHER_CONTENT, id(font_large_bold), color_text, TextAlign::TOP_CENTER, "--°F");
      }

      it.line(X_LEFT_MARGIN, Y_DIVIDER_1, X_RIGHT_MARGIN, Y_DIVIDER_1, color_text);

      // === ENERGY SECTION ===
      it.printf(X_CENTER, Y_ENERGY_TITLE, id(font_title), color_text, TextAlign::TOP_CENTER, "ENERGY");

      // Solar Output
      it.printf(X_ROW_ICON, Y_SOLAR_OUTPUT_ICON, id(font_mdi_medium), color_text, TextAlign::CENTER_LEFT, "\U000F0A72");
      it.printf(X_ROW_LABEL, Y_SOLAR_OUTPUT_TEXT, id(font_name), color_text, TextAlign::CENTER_LEFT, "%s", solar_power.name());
      if (solar_power.has_state()) {
        float current_solar = solar_power.value() < 0 ? 0.0 : solar_power.value();
        it.printf(X_ROW_VALUE, Y_SOLAR_OUTPUT_TEXT, id(font_medium_bold), color_text, TextAlign::CENTER_RIGHT, "%.1f kW", current_solar);
      } else {
        it.printf(X_ROW_VALUE, Y_SOLAR_OUTPUT_TEXT, id(font_medium_bold), color_text, TextAlign::CENTER_RIGHT, "-- kW");
      }

      // Solar 24hr
      it.printf(X_ROW_ICON, Y_SOLAR_24HR_ICON, id(font_mdi_medium), color_text, TextAlign::CENTER_LEFT, "\U000F140C");
      it.printf(X_ROW_LABEL, Y_SOLAR_24HR_TEXT, id(font_name), color_text, TextAlign::CENTER_LEFT, "%s", solar_energy.name());
      if (solar_energy.has_state()) {
        it.printf(X_ROW_VALUE, Y_SOLAR_24HR_TEXT, id(font_medium_bold), color_text, TextAlign::CENTER_RIGHT, "%.0f kWh", solar_energy.value());
      } else {
        it.printf(X_ROW_VALUE, Y_SOLAR_24HR_TEXT, id(font_medium_bold), color_text, TextAlign::CENTER_RIGHT, "-- kWh");
      }

      // Home 24hr
      it.printf(X_ROW_ICON, Y_HOME_24HR_ICON, id(font_mdi_medium), color_text, TextAlign::CENTER_LEFT, "\U000F02DC");
      it.printf(X_ROW_LABEL, Y_HOME_24HR_TEXT, id(font_name), color_text, TextAlign::CENTER_LEFT, "%s", home_consumption.name());
      if (home_consumption.has_state()) {
        it.printf(X_ROW_VALUE, Y_HOME_24HR_TEXT, id(font_medium_bold), color_text, TextAlign::CENTER_RIGHT, "%.0f kWh", home_consumption.value());
      } else {
        it.printf(X_ROW_VALUE, Y_HOME_24HR_TEXT, id(font_medium_bold), color_text, TextAlign::CENTER_RIGHT, "-- kWh");
      }

      // EV Charging - prioritize power reading over status (handles status glitches)
      if (charging_power.has_state() && charging_power.value() > 100.0) {
        // Active charging
        it.printf(X_ROW_ICON, Y_CHARGING_ICON, id(font_mdi_medium), color_text, TextAlign::CENTER_LEFT, "\U000F007D");
        float real_power_kw = (charging_power.value() * id(tesla_power_factor)) / 1000.0;
        it.printf(X_ROW_VALUE, Y_CHARGING_TEXT, id(font_medium_bold), color_text, TextAlign::CENTER_RIGHT, "%.1f kW", real_power_kw);
      } else if (charger.has_state() && charger.value() != "unavailable") {
        std::string status = charger.value();
        if (status == "not_connected" || status == "booting") {
          it.printf(X_ROW_ICON, Y_CHARGING_ICON, id(font_mdi_medium), color_text, TextAlign::CENTER_LEFT, "\U000F151C");
          it.printf(X_ROW_VALUE, Y_CHARGING_TEXT, id(font_medium_bold), color_text, TextAlign::CENTER_RIGHT, "-- kW");
        } else if (status == "error") {
          it.printf(X_ROW_ICON, Y_CHARGING_ICON, id(font_mdi_medium), color_text, TextAlign::CENTER_LEFT, "X");
          it.printf(X_ROW_VALUE, Y_CHARGING_TEXT, id(font_medium_bold), color_text, TextAlign::CENTER_RIGHT, "X");
        } else {
          // Plugged in, not charging
          it.printf(X_ROW_ICON, Y_CHARGING_ICON, id(font_mdi_medium), color_text, TextAlign::CENTER_LEFT, "\U000F0079");
          it.printf(X_ROW_VALUE, Y_CHARGING_TEXT, id(font_medium_bold), color_text, TextAlign::CENTER_RIGHT, "0.0 kW");
        }
      } else {
        // Unavailable
        it.printf(X_ROW_ICON, Y_CHARGING_ICON, id(font_mdi_medium), color_text, TextAlign::CENTER_LEFT, "\U000F151C");
        it.printf(X_ROW_VALUE, Y_CHARGING_TEXT, id(font_medium_bold), color_text, TextAlign::CENTER_RIGHT, "-- kW");
      }
      it.printf(X_ROW_LABEL, Y_CHARGING_TEXT, id(font_name), color_text, TextAlign::CENTER_LEFT, "%s", charging_power.name());

      it.line(X_LEFT_MARGIN, Y_DIVIDER_2, X_RIGHT_MARGIN, Y_DIVIDER_2, color_text);

      // === GATES SECTION ===
      it.printf(X_CENTER, Y_GATES_TITLE, id(font_title), color_text, TextAlign::TOP_CENTER, "GATES");

      // Gate 1
      if (gate1.has_state()) {
        it.printf(X_ROW_ICON, Y_GATE1_ICON, id(font_mdi_medium), color_text, TextAlign::CENTER_LEFT,
          gate1.value() ? "\U000F081C" : "\U000F081A");
        it.printf(X_ROW_VALUE, Y_GATE1_TEXT, id(font_medium_bold), color_text, TextAlign::CENTER_RIGHT,
          gate1.value() ? "OPEN" : "CLOSED");
      } else {
        it.printf(X_ROW_ICON, Y_GATE1_ICON, id(font_mdi_medium), color_text, TextAlign::CENTER_LEFT, "\U000F0205");
        it.printf(X_ROW_VALUE, Y_GATE1_TEXT, id(font_medium_bold), color_text, TextAlign::CENTER_RIGHT, "UNKNOWN");
      }
      it.printf(X_ROW_LABEL, Y_GATE1_TEXT, id(font_name), color_text, TextAlign::CENTER_LEFT, "%s", gate1.name());

      // Gate 2
      if (gate2.has_state()) {
        it.printf(X_ROW_ICON, Y_GATE2_ICON, id(font_mdi_medium), color_text, TextAlign::CENTER_LEFT,
          gate2.value() ? "\U000F081C" : "\U000F081A");
        it.printf(X_ROW_VALUE, Y_GATE2_TEXT, id(font_medium_bold), color_text, TextAlign::CENTER_RIGHT,
          gate2.value() ? "OPEN" : "CLOSED");
      } else {
        it.printf(X_ROW_ICON, Y_GATE2_ICON, id(font_mdi_medium), color_text, TextAlign::CENTER_LEFT, "\U000F0205");
        it.printf(X_ROW_VALUE, Y_GATE2_TEXT, id(font_medium_bold), color_text, TextAlign::CENTER_RIGHT, "UNKNOWN");
      }
      it.printf(X_ROW_LABEL, Y_GATE2_TEXT, id(font_name), color_text, TextAlign::CENTER_LEFT, "%s", gate2.name());

      // Gate 3 with lock detection
      if (gate3.has_state() && lock.has_state()) {
        if (gate3.value()) {
          it.printf(X_ROW_ICON, Y_GATE3_ICON, id(font_mdi_medium), color_text, TextAlign::CENTER_LEFT, "\U000F081C");
          it.printf(X_ROW_VALUE, Y_GATE3_TEXT, id(font_medium_bold), color_text, TextAlign::CENTER_RIGHT, "OPEN");
        } else if (lock.value() == "unlocked") {
          it.printf(X_ROW_ICON, Y_GATE3_ICON, id(font_mdi_medium), color_text, TextAlign::CENTER_LEFT, "\U000F033F");
          it.printf(X_ROW_VALUE, Y_GATE3_TEXT, id(font_medium_bold), color_text, TextAlign::CENTER_RIGHT, "UNLOCKED");
        } else {
          it.printf(X_ROW_ICON, Y_GATE3_ICON, id(font_mdi_medium), color_text, TextAlign::CENTER_LEFT, "\U000F081A");
          it.printf(X_ROW_VALUE, Y_GATE3_TEXT, id(font_medium_bold), color_text, TextAlign::CENTER_RIGHT, "CLOSED");
        }
      } else {
        it.printf(X_ROW_ICON, Y_GATE3_ICON, id(font_mdi_medium), color_text, TextAlign::CENTER_LEFT, "\U000F0205");
        it.printf(X_ROW_VALUE, Y_GATE3_TEXT, id(font_medium_bold), color_text, TextAlign::CENTER_RIGHT, "UNKNOWN");
      }
      it.printf(X_ROW_LABEL, Y_GATE3_TEXT, id(font_name), color_text, TextAlign::CENTER_LEFT, "%s", gate3.name());

      // === FOOTER ===
      char str[40];
      struct tm timeinfo;
      time_t displayTime;

      if (id(ha_connected)) {
        displayTime = id(last_display_refresh_time);
        if (displayTime > 0) {
          localtime_r(&displayTime, &timeinfo);
          strftime(str, sizeof(str), "%b %d, %Y %I:%M:%S %p", &timeinfo);
          it.printf(X_CENTER, Y_FOOTER, id(font_small_book), color_text, TextAlign::TOP_CENTER, "Refreshed %s", str);
        } else {
          it.printf(X_CENTER, Y_FOOTER, id(font_small_book), color_text, TextAlign::TOP_CENTER, "Initializing...");
        }
      } else {
        displayTime = id(last_ha_connection_time);
        if (displayTime > 0) {
          localtime_r(&displayTime, &timeinfo);
          strftime(str, sizeof(str), "%b %d, %Y %I:%M:%S %p", &timeinfo);
          it.printf(X_CENTER, Y_FOOTER, id(font_small_book), color_text, TextAlign::TOP_CENTER, "Last Seen %s", str);
        } else {
          it.printf(X_CENTER, Y_FOOTER, id(font_small_book), color_text, TextAlign::TOP_CENTER, "No Connection");
        }
      }
