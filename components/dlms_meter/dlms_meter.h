#pragma once

#include "esphome/core/component.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "mbedtls/gcm.h"

namespace esphome {
namespace dlms_meter {

static const char *const TAG = "dlms_meter";
static const char *const VERSION = "1.0.0";

class DlmsMeter : public Component, public uart::UARTDevice {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  // Key setter
  void set_key(const std::vector<uint8_t> &key);

  // Voltage sensor setters
  void set_voltage_l1_sensor(sensor::Sensor *sensor) { voltage_l1_ = sensor; }
  void set_voltage_l2_sensor(sensor::Sensor *sensor) { voltage_l2_ = sensor; }
  void set_voltage_l3_sensor(sensor::Sensor *sensor) { voltage_l3_ = sensor; }

  // Current sensor setters
  void set_current_l1_sensor(sensor::Sensor *sensor) { current_l1_ = sensor; }
  void set_current_l2_sensor(sensor::Sensor *sensor) { current_l2_ = sensor; }
  void set_current_l3_sensor(sensor::Sensor *sensor) { current_l3_ = sensor; }

  // Power sensor setters
  void set_active_power_plus_sensor(sensor::Sensor *sensor) { active_power_plus_ = sensor; }
  void set_active_power_minus_sensor(sensor::Sensor *sensor) { active_power_minus_ = sensor; }
  void set_power_factor_sensor(sensor::Sensor *sensor) { power_factor_ = sensor; }

  // Energy sensor setters
  void set_active_energy_plus_sensor(sensor::Sensor *sensor) { active_energy_plus_ = sensor; }
  void set_active_energy_minus_sensor(sensor::Sensor *sensor) { active_energy_minus_ = sensor; }
  void set_reactive_energy_plus_sensor(sensor::Sensor *sensor) { reactive_energy_plus_ = sensor; }
  void set_reactive_energy_minus_sensor(sensor::Sensor *sensor) { reactive_energy_minus_ = sensor; }

  // Text sensor setters
  void set_timestamp_sensor(text_sensor::TextSensor *sensor) { timestamp_ = sensor; }
  void set_meter_number_sensor(text_sensor::TextSensor *sensor) { meter_number_ = sensor; }

  // MQTT topic setter
  void set_mqtt_topic(const std::string &topic) { mqtt_topic_ = topic; }

 protected:
  // Receive buffer
  static const int RECEIVE_BUFFER_SIZE = 1024;
  uint8_t receive_buffer_[RECEIVE_BUFFER_SIZE];
  int receive_buffer_index_ = 0;
  uint32_t last_read_ = 0;
  int read_timeout_ = 1000;

  // Decryption key
  uint8_t key_[16];
  size_t key_length_ = 0;

  // MQTT topic
  std::string mqtt_topic_;

  // AES context
  mbedtls_gcm_context aes_;

  // Voltage sensors
  sensor::Sensor *voltage_l1_{nullptr};
  sensor::Sensor *voltage_l2_{nullptr};
  sensor::Sensor *voltage_l3_{nullptr};

  // Current sensors
  sensor::Sensor *current_l1_{nullptr};
  sensor::Sensor *current_l2_{nullptr};
  sensor::Sensor *current_l3_{nullptr};

  // Power sensors
  sensor::Sensor *active_power_plus_{nullptr};
  sensor::Sensor *active_power_minus_{nullptr};
  sensor::Sensor *power_factor_{nullptr};

  // Energy sensors
  sensor::Sensor *active_energy_plus_{nullptr};
  sensor::Sensor *active_energy_minus_{nullptr};
  sensor::Sensor *reactive_energy_plus_{nullptr};
  sensor::Sensor *reactive_energy_minus_{nullptr};

  // Text sensors
  text_sensor::TextSensor *timestamp_{nullptr};
  text_sensor::TextSensor *meter_number_{nullptr};

  // Helper functions
  uint16_t swap_uint16(uint16_t val);
  uint32_t swap_uint32(uint32_t val);
  void log_packet(uint8_t *array, size_t length);
  void abort_read();
  void process_packet();
  bool decrypt_packet(uint8_t *encrypted, size_t encrypted_len, uint8_t *decrypted, size_t *decrypted_len);
  void parse_data(uint8_t *data, size_t length);
};

}  // namespace dlms_meter
}  // namespace esphome
