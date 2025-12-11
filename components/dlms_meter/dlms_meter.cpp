#include "dlms_meter.h"
#include "dlms_constants.h"
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"

namespace esphome {
namespace dlms_meter {

void DlmsMeter::setup() {
  ESP_LOGI(TAG, "DLMS smart meter component v%s started", VERSION);
  mbedtls_gcm_init(&aes_);
}

void DlmsMeter::dump_config() {
  ESP_LOGCONFIG(TAG, "DLMS Meter:");
  ESP_LOGCONFIG(TAG, "  Key configured: %s", key_length_ > 0 ? "YES" : "NO");
  if (!mqtt_topic_.empty()) {
    ESP_LOGCONFIG(TAG, "  MQTT Topic: %s", mqtt_topic_.c_str());
  }
}

void DlmsMeter::loop() {
  uint32_t current_time = millis();

  while (available()) {
    if (receive_buffer_index_ >= RECEIVE_BUFFER_SIZE) {
      ESP_LOGE(TAG, "Buffer overflow");
      receive_buffer_index_ = 0;
    }

    receive_buffer_[receive_buffer_index_] = read();
    receive_buffer_index_++;

    last_read_ = current_time;
    delay(10);  // Fix for ESPHome 2022.12+
  }

  if (receive_buffer_index_ > 0 && current_time - last_read_ > read_timeout_) {
    ESP_LOGV(TAG, "receiveBufferIndex: %d", receive_buffer_index_);

    if (receive_buffer_index_ < 256) {
      ESP_LOGE(TAG, "Received packet with invalid size");
      abort_read();
      return;
    }

    ESP_LOGD(TAG, "Handling packet");
    log_packet(receive_buffer_, receive_buffer_index_);

    // Decrypting
    uint16_t payload_length = 243;  // Fixed for Kaifa MA309M

    if (receive_buffer_index_ <= payload_length) {
      ESP_LOGV(TAG, "receiveBufferIndex: %d, payloadLength: %d", receive_buffer_index_, payload_length);
      ESP_LOGE(TAG, "Payload length is too big for received data");
      abort_read();
      return;
    }

    uint16_t payload_length1 = 228;
    uint16_t payload_length2 = payload_length - payload_length1;

    if (payload_length2 >= receive_buffer_index_ - DLMS_HEADER2_OFFSET - DLMS_HEADER2_LENGTH) {
      ESP_LOGV(TAG, "receiveBufferIndex: %d, payloadLength2: %d", receive_buffer_index_, payload_length2);
      ESP_LOGE(TAG, "Payload length 2 is too big");
      abort_read();
      return;
    }

    // Build IV
    uint8_t iv[12];
    memcpy(&iv[0], &receive_buffer_[DLMS_SYST_OFFSET], DLMS_SYST_LENGTH);
    memcpy(&iv[8], &receive_buffer_[DLMS_IC_OFFSET], DLMS_IC_LENGTH);

    // Build ciphertext from two parts
    uint8_t ciphertext[payload_length];
    memcpy(&ciphertext[0], &receive_buffer_[DLMS_HEADER1_OFFSET + DLMS_HEADER1_LENGTH], payload_length1);
    memcpy(&ciphertext[payload_length1], &receive_buffer_[DLMS_HEADER2_OFFSET + DLMS_HEADER2_LENGTH], payload_length2);

    // Decrypt
    uint8_t plaintext[payload_length];

    mbedtls_gcm_setkey(&aes_, MBEDTLS_CIPHER_ID_AES, key_, key_length_ * 8);
    mbedtls_gcm_auth_decrypt(&aes_, payload_length, iv, sizeof(iv), NULL, 0, NULL, 0, ciphertext, plaintext);

    if (plaintext[0] != 0x0F || plaintext[5] != 0x0C) {
      ESP_LOGE(TAG, "Packet was decrypted but data is invalid");
      abort_read();
      return;
    }

    // Decode
    ESP_LOGV(TAG, "Plaintext Packet:");
    log_packet(plaintext, 300);

    int current_position = DECODER_START_OFFSET;

    do {
      ESP_LOGV(TAG, "currentPosition: %d", current_position);
      ESP_LOGV(TAG, "OBIS header type: %d", plaintext[current_position + OBIS_TYPE_OFFSET]);

      if (plaintext[current_position + OBIS_TYPE_OFFSET] != DataType::OctetString) {
        ESP_LOGE(TAG, "Unsupported OBIS header type");
        abort_read();
        return;
      }

      uint8_t obis_code_length = plaintext[current_position + OBIS_LENGTH_OFFSET];
      ESP_LOGV(TAG, "OBIS code/header length: %d", obis_code_length);

      if (obis_code_length != 0x06 && obis_code_length != 0x0C) {
        ESP_LOGE(TAG, "Unsupported OBIS header length");
        abort_read();
        return;
      }

      uint8_t obis_code[obis_code_length];
      memcpy(&obis_code[0], &plaintext[current_position + OBIS_CODE_OFFSET], obis_code_length);

      bool timestamp_found = false;
      bool meter_number_found = false;

      if ((obis_code_length == 0x0C) && (current_position == DECODER_START_OFFSET)) {
        timestamp_found = true;
      } else if ((current_position != DECODER_START_OFFSET) && plaintext[current_position - 1] == 0xFF) {
        meter_number_found = true;
      } else {
        current_position += obis_code_length + 2;
      }

      uint8_t data_type = plaintext[current_position];
      current_position++;

      uint8_t data_length = 0x00;
      CodeType code_type = CodeType::Unknown;

      ESP_LOGV(TAG, "obisCode (OBIS_A): %d", obis_code[OBIS_A]);
      ESP_LOGV(TAG, "currentPosition: %d", current_position);

      if (obis_code[OBIS_A] == Medium::Electricity) {
        if (memcmp(&obis_code[OBIS_C], ESPDM_VOLTAGE_L1, 2) == 0)
          code_type = CodeType::VoltageL1;
        else if (memcmp(&obis_code[OBIS_C], ESPDM_VOLTAGE_L2, 2) == 0)
          code_type = CodeType::VoltageL2;
        else if (memcmp(&obis_code[OBIS_C], ESPDM_VOLTAGE_L3, 2) == 0)
          code_type = CodeType::VoltageL3;
        else if (memcmp(&obis_code[OBIS_C], ESPDM_CURRENT_L1, 2) == 0)
          code_type = CodeType::CurrentL1;
        else if (memcmp(&obis_code[OBIS_C], ESPDM_CURRENT_L2, 2) == 0)
          code_type = CodeType::CurrentL2;
        else if (memcmp(&obis_code[OBIS_C], ESPDM_CURRENT_L3, 2) == 0)
          code_type = CodeType::CurrentL3;
        else if (memcmp(&obis_code[OBIS_C], ESPDM_ACTIVE_POWER_PLUS, 2) == 0)
          code_type = CodeType::ActivePowerPlus;
        else if (memcmp(&obis_code[OBIS_C], ESPDM_ACTIVE_POWER_MINUS, 2) == 0)
          code_type = CodeType::ActivePowerMinus;
        else if (memcmp(&obis_code[OBIS_C], ESPDM_POWER_FACTOR, 2) == 0)
          code_type = CodeType::PowerFactor;
        else if (memcmp(&obis_code[OBIS_C], ESPDM_ACTIVE_ENERGY_PLUS, 2) == 0)
          code_type = CodeType::ActiveEnergyPlus;
        else if (memcmp(&obis_code[OBIS_C], ESPDM_ACTIVE_ENERGY_MINUS, 2) == 0)
          code_type = CodeType::ActiveEnergyMinus;
        else if (memcmp(&obis_code[OBIS_C], ESPDM_REACTIVE_ENERGY_PLUS, 2) == 0)
          code_type = CodeType::ReactiveEnergyPlus;
        else if (memcmp(&obis_code[OBIS_C], ESPDM_REACTIVE_ENERGY_MINUS, 2) == 0)
          code_type = CodeType::ReactiveEnergyMinus;
        else
          ESP_LOGW(TAG, "Unsupported OBIS code OBIS_C: %d, OBIS_D: %d", obis_code[OBIS_C], obis_code[OBIS_D]);
      } else if (obis_code[OBIS_A] == Medium::Abstract) {
        if (memcmp(&obis_code[OBIS_C], ESPDM_TIMESTAMP, 2) == 0)
          code_type = CodeType::Timestamp;
        else if (memcmp(&obis_code[OBIS_C], ESPDM_SERIAL_NUMBER, 2) == 0)
          code_type = CodeType::SerialNumber;
        else if (memcmp(&obis_code[OBIS_C], ESPDM_DEVICE_NAME, 2) == 0)
          code_type = CodeType::DeviceName;
        else
          ESP_LOGW(TAG, "Unsupported OBIS code OBIS_C: %d, OBIS_D: %d", obis_code[OBIS_C], obis_code[OBIS_D]);
      } else if (timestamp_found) {
        ESP_LOGV(TAG, "Found Timestamp without obisMedium");
        code_type = CodeType::Timestamp;
      } else if (meter_number_found) {
        ESP_LOGV(TAG, "Found MeterNumber without obisMedium");
        code_type = CodeType::MeterNumber;
      } else {
        ESP_LOGE(TAG, "Unsupported OBIS medium");
        abort_read();
        return;
      }

      uint16_t uint16_value;
      uint32_t uint32_value;
      float float_value;

      switch (data_type) {
        case DataType::DoubleLongUnsigned:
          data_length = 4;
          memcpy(&uint32_value, &plaintext[current_position], 4);
          uint32_value = swap_uint32(uint32_value);
          float_value = uint32_value;

          if (code_type == CodeType::ActivePowerPlus && active_power_plus_ != nullptr)
            active_power_plus_->publish_state(float_value);
          else if (code_type == CodeType::ActivePowerMinus && active_power_minus_ != nullptr)
            active_power_minus_->publish_state(float_value);
          else if (code_type == CodeType::ActiveEnergyPlus && active_energy_plus_ != nullptr)
            active_energy_plus_->publish_state(float_value);
          else if (code_type == CodeType::ActiveEnergyMinus && active_energy_minus_ != nullptr)
            active_energy_minus_->publish_state(float_value);
          else if (code_type == CodeType::ReactiveEnergyPlus && reactive_energy_plus_ != nullptr)
            reactive_energy_plus_->publish_state(float_value);
          else if (code_type == CodeType::ReactiveEnergyMinus && reactive_energy_minus_ != nullptr)
            reactive_energy_minus_->publish_state(float_value);
          break;

        case DataType::LongUnsigned:
          data_length = 2;
          memcpy(&uint16_value, &plaintext[current_position], 2);
          uint16_value = swap_uint16(uint16_value);

          if (plaintext[current_position + 5] == Accuracy::SingleDigit)
            float_value = uint16_value / 10.0;
          else if (plaintext[current_position + 5] == Accuracy::DoubleDigit)
            float_value = uint16_value / 100.0;
          else
            float_value = uint16_value;

          if (code_type == CodeType::VoltageL1 && voltage_l1_ != nullptr)
            voltage_l1_->publish_state(float_value);
          else if (code_type == CodeType::VoltageL2 && voltage_l2_ != nullptr)
            voltage_l2_->publish_state(float_value);
          else if (code_type == CodeType::VoltageL3 && voltage_l3_ != nullptr)
            voltage_l3_->publish_state(float_value);
          else if (code_type == CodeType::CurrentL1 && current_l1_ != nullptr)
            current_l1_->publish_state(float_value);
          else if (code_type == CodeType::CurrentL2 && current_l2_ != nullptr)
            current_l2_->publish_state(float_value);
          else if (code_type == CodeType::CurrentL3 && current_l3_ != nullptr)
            current_l3_->publish_state(float_value);
          else if (code_type == CodeType::PowerFactor && power_factor_ != nullptr)
            power_factor_->publish_state(float_value / 1000.0);
          break;

        case DataType::OctetString:
          ESP_LOGV(TAG, "Arrived on OctetString");
          data_length = plaintext[current_position];
          current_position++;

          if (code_type == CodeType::Timestamp && timestamp_ != nullptr) {
            char timestamp_str[21];
            uint16_t year;
            uint8_t month, day, hour, minute, second;

            memcpy(&uint16_value, &plaintext[current_position], 2);
            year = swap_uint16(uint16_value);
            memcpy(&month, &plaintext[current_position + 2], 1);
            memcpy(&day, &plaintext[current_position + 3], 1);
            memcpy(&hour, &plaintext[current_position + 5], 1);
            memcpy(&minute, &plaintext[current_position + 6], 1);
            memcpy(&second, &plaintext[current_position + 7], 1);

            sprintf(timestamp_str, "%04u-%02u-%02uT%02u:%02u:%02uZ", year, month, day, hour, minute, second);
            timestamp_->publish_state(timestamp_str);
          } else if (code_type == CodeType::MeterNumber && meter_number_ != nullptr) {
            ESP_LOGV(TAG, "Constructing MeterNumber...");
            char meter_number_str[13];
            memcpy(meter_number_str, &plaintext[current_position], data_length);
            meter_number_str[12] = '\0';
            meter_number_->publish_state(meter_number_str);
          }
          break;

        default:
          ESP_LOGE(TAG, "Unsupported OBIS data type");
          abort_read();
          return;
      }

      current_position += data_length;

      if (!timestamp_found) {
        current_position += 2;
      }

      if (plaintext[current_position] == 0x0F) {
        current_position += 4;
      }
    } while (current_position <= payload_length);

    receive_buffer_index_ = 0;
    ESP_LOGI(TAG, "Received valid data");
  }
}

void DlmsMeter::set_key(const std::vector<uint8_t> &key) {
  if (key.size() == 16) {
    memcpy(key_, key.data(), 16);
    key_length_ = 16;
  }
}

void DlmsMeter::abort_read() {
  receive_buffer_index_ = 0;
}

uint16_t DlmsMeter::swap_uint16(uint16_t val) {
  return (val << 8) | (val >> 8);
}

uint32_t DlmsMeter::swap_uint32(uint32_t val) {
  val = ((val << 8) & 0xFF00FF00) | ((val >> 8) & 0xFF00FF);
  return (val << 16) | (val >> 16);
}

void DlmsMeter::log_packet(uint8_t *array, size_t length) {
  if (!ESP_LOG_LEVEL_IS(ESP_LOG_VERBOSE, TAG))
    return;

  char buffer[length * 3];
  for (size_t i = 0; i < length; i++) {
    uint8_t nib1 = (array[i] >> 4) & 0x0F;
    uint8_t nib2 = (array[i] >> 0) & 0x0F;
    buffer[i * 3] = nib1 < 0xA ? '0' + nib1 : 'A' + nib1 - 0xA;
    buffer[i * 3 + 1] = nib2 < 0xA ? '0' + nib2 : 'A' + nib2 - 0xA;
    buffer[i * 3 + 2] = ' ';
  }
  buffer[(length * 3) - 1] = '\0';
  ESP_LOGV(TAG, "%s", buffer);
}

}  // namespace dlms_meter
}  // namespace esphome
