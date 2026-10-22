#include "dlms_meter.h"
#include "dlms_constants.h"
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"

namespace esphome {
namespace dlms_meter {

// Kaifa MA309M: the 243 byte payload is split over two M-Bus frames
static const uint16_t PAYLOAD_LENGTH = 243;
static const uint16_t PAYLOAD_LENGTH1 = 228;
static const uint16_t PAYLOAD_LENGTH2 = PAYLOAD_LENGTH - PAYLOAD_LENGTH1;

void DlmsMeter::setup() {
  ESP_LOGI(TAG, "DLMS smart meter component v%s started", VERSION);
  mbedtls_aes_init(&aes_);
  if (key_length_ > 0 && mbedtls_aes_setkey_enc(&aes_, key_, key_length_ * 8) != 0) {
    ESP_LOGE(TAG, "Failed to set decryption key");
    key_length_ = 0;
  }
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
  }

  // A frame is complete once the line has been idle for read_timeout_ ms
  if (receive_buffer_index_ > 0 && current_time - last_read_ > read_timeout_) {
    process_packet();
    abort_read();
  }
}

void DlmsMeter::process_packet() {
  ESP_LOGV(TAG, "receiveBufferIndex: %d", receive_buffer_index_);

  if (receive_buffer_index_ < 256) {
    ESP_LOGE(TAG, "Received packet with invalid size (%d bytes)", receive_buffer_index_);
    return;
  }

  ESP_LOGD(TAG, "Handling packet");
  log_packet(receive_buffer_, receive_buffer_index_);

  if (key_length_ == 0) {
    ESP_LOGE(TAG, "No valid decryption key configured");
    return;
  }

  uint8_t plaintext[PAYLOAD_LENGTH];
  size_t plaintext_len = 0;
  if (!decrypt_packet(receive_buffer_, receive_buffer_index_, plaintext, &plaintext_len))
    return;

  ESP_LOGV(TAG, "Plaintext Packet:");
  log_packet(plaintext, plaintext_len);

  parse_data(plaintext, plaintext_len);
}

bool DlmsMeter::decrypt_packet(uint8_t *encrypted, size_t encrypted_len, uint8_t *decrypted,
                               size_t *decrypted_len) {
  if (encrypted_len < DLMS_HEADER2_OFFSET + DLMS_HEADER2_LENGTH + PAYLOAD_LENGTH2) {
    ESP_LOGE(TAG, "Packet too short for payload (%u bytes)", (unsigned) encrypted_len);
    return false;
  }

  // Ciphertext is split around the second M-Bus frame header
  uint8_t ciphertext[PAYLOAD_LENGTH];
  memcpy(&ciphertext[0], &encrypted[DLMS_HEADER1_OFFSET + DLMS_HEADER1_LENGTH], PAYLOAD_LENGTH1);
  memcpy(&ciphertext[PAYLOAD_LENGTH1], &encrypted[DLMS_HEADER2_OFFSET + DLMS_HEADER2_LENGTH], PAYLOAD_LENGTH2);

  // DLMS frames carry no GCM auth tag, so decrypt as plain AES-CTR. The GCM
  // keystream starts at counter IV || 00000002, IV = system title || invocation
  // counter. mbedtls >= 3 rejects a zero-length tag in mbedtls_gcm_auth_decrypt().
  uint8_t nonce_counter[16];
  uint8_t stream_block[16];
  size_t nc_off = 0;
  memcpy(&nonce_counter[0], &encrypted[DLMS_SYST_OFFSET], DLMS_SYST_LENGTH);
  memcpy(&nonce_counter[8], &encrypted[DLMS_IC_OFFSET], DLMS_IC_LENGTH);
  nonce_counter[12] = 0x00;
  nonce_counter[13] = 0x00;
  nonce_counter[14] = 0x00;
  nonce_counter[15] = 0x02;

  int ret = mbedtls_aes_crypt_ctr(&aes_, PAYLOAD_LENGTH, &nc_off, nonce_counter, stream_block, ciphertext, decrypted);
  if (ret != 0) {
    ESP_LOGE(TAG, "Decryption failed (mbedtls error -0x%04X)", (unsigned) -ret);
    return false;
  }

  if (decrypted[0] != 0x0F || decrypted[5] != 0x0C) {
    ESP_LOGE(TAG, "Packet was decrypted but data is invalid (wrong key?)");
    return false;
  }

  *decrypted_len = PAYLOAD_LENGTH;
  return true;
}

void DlmsMeter::parse_data(uint8_t *plaintext, size_t length) {
  const int payload_length = length;
  int current_position = DECODER_START_OFFSET;

  // Returns false if fewer than n bytes are left at pos
  auto has = [payload_length](int pos, int n) { return pos >= 0 && pos + n <= payload_length; };

  while (has(current_position, OBIS_CODE_OFFSET)) {
    ESP_LOGV(TAG, "currentPosition: %d", current_position);
    ESP_LOGV(TAG, "OBIS header type: %d", plaintext[current_position + OBIS_TYPE_OFFSET]);

    if (plaintext[current_position + OBIS_TYPE_OFFSET] != DataType::OctetString) {
      ESP_LOGE(TAG, "Unsupported OBIS header type");
      return;
    }

    uint8_t obis_code_length = plaintext[current_position + OBIS_LENGTH_OFFSET];
    ESP_LOGV(TAG, "OBIS code/header length: %d", obis_code_length);

    if (obis_code_length != 0x06 && obis_code_length != 0x0C) {
      ESP_LOGE(TAG, "Unsupported OBIS header length");
      return;
    }

    if (!has(current_position + OBIS_CODE_OFFSET, obis_code_length)) {
      ESP_LOGE(TAG, "Truncated OBIS code at %d", current_position);
      return;
    }

    uint8_t obis_code[0x0C];
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

    if (!has(current_position, 1)) {
      ESP_LOGE(TAG, "Truncated data type at %d", current_position);
      return;
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
      return;
    }

    uint16_t uint16_value;
    uint32_t uint32_value;
    float float_value;

    switch (data_type) {
      case DataType::DoubleLongUnsigned:
        data_length = 4;
        if (!has(current_position, data_length)) {
          ESP_LOGE(TAG, "Truncated value at %d", current_position);
          return;
        }
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
        // Value plus the scaler 5 bytes further on
        if (!has(current_position, 6)) {
          ESP_LOGE(TAG, "Truncated value at %d", current_position);
          return;
        }
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
        if (!has(current_position, 1)) {
          ESP_LOGE(TAG, "Truncated string length at %d", current_position);
          return;
        }
        data_length = plaintext[current_position];
        current_position++;
        if (!has(current_position, data_length)) {
          ESP_LOGE(TAG, "Truncated string at %d", current_position);
          return;
        }

        if (code_type == CodeType::Timestamp && timestamp_ != nullptr && data_length >= 8) {
          char timestamp_str[32];
          uint16_t year;
          uint8_t month, day, hour, minute, second;

          memcpy(&uint16_value, &plaintext[current_position], 2);
          year = swap_uint16(uint16_value);
          month = plaintext[current_position + 2];
          day = plaintext[current_position + 3];
          hour = plaintext[current_position + 5];
          minute = plaintext[current_position + 6];
          second = plaintext[current_position + 7];

          snprintf(timestamp_str, sizeof(timestamp_str), "%04u-%02u-%02uT%02u:%02u:%02uZ", year, month, day, hour,
                   minute, second);
          timestamp_->publish_state(timestamp_str);
        } else if (code_type == CodeType::MeterNumber && meter_number_ != nullptr) {
          ESP_LOGV(TAG, "Constructing MeterNumber...");
          meter_number_->publish_state(std::string(reinterpret_cast<char *>(&plaintext[current_position]), data_length));
        }
        break;

      default:
        ESP_LOGE(TAG, "Unsupported OBIS data type");
        return;
    }

    current_position += data_length;

    if (!timestamp_found) {
      current_position += 2;
    }

    // Skip the scaler/unit structure that follows numeric values
    if (has(current_position, 1) && plaintext[current_position] == 0x0F) {
      current_position += 4;
    }
  }

  ESP_LOGI(TAG, "Received valid data");
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
#if ESPHOME_LOG_LEVEL >= ESPHOME_LOG_LEVEL_VERBOSE
  char buffer[format_hex_pretty_size(RECEIVE_BUFFER_SIZE)];
  ESP_LOGV(TAG, "%s", format_hex_pretty_to(buffer, sizeof(buffer), array, length, ' '));
#endif
}

}  // namespace dlms_meter
}  // namespace esphome
