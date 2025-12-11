# ESPHome DLMS Smart Meter Component

A component for ESPHome for reading meter data sent by DLMS smart meters (e.g., Kaifa MA309M) via M-Bus.

**This fork has been updated to use the new ESPHome `external_components` format**, replacing the deprecated `custom_component` approach that was removed in ESPHome 2024.x.

## Features

* Exposes all data sent from the smart meter as sensors
* Easy configuration using ESPHome's declarative YAML syntax
* All sensors are optional - only configure what you need
* Proper device class and state class for Home Assistant integration

## Supported meters

* Kaifa MA309M

## Supported providers

* [EVN AG](https://www.evn.at)

## Exposed sensors

* Voltage L1, L2, L3
* Current L1, L2, L3
* Active Power Plus/Minus
* Power Factor
* Active Energy Plus/Minus
* Reactive Energy Plus/Minus
* Timestamp
* Meter Number

## Requirements

* ESP32 ([supported by ESPHome](https://esphome.io/#devices))
* RJ11 cable
* M-Bus to UART board (e.g. https://www.mikroe.com/m-bus-slave-click)
* RJ11 breakout board **or** soldering iron with some wires
* ESPHome 2023.12 or newer

## Installation

### Using external_components (recommended)

Add the following to your ESPHome configuration:

```yaml
external_components:
  - source: github://packerlschupfer/esphome-dlms-meter
    components: [dlms_meter]

uart:
  tx_pin: GPIO4
  rx_pin: GPIO36
  baud_rate: 2400
  rx_buffer_size: 2048
  id: mbus

dlms_meter:
  uart_id: mbus
  key: [0x3E, 0xC8, 0xFB, 0x32, 0x83, 0x69, 0x07, 0xBA, 0x04, 0xBA, 0x53, 0x8B, 0x63, 0x32, 0x91, 0xFF]

  # All sensors are optional
  voltage_l1:
    name: "Voltage L1"
  voltage_l2:
    name: "Voltage L2"
  voltage_l3:
    name: "Voltage L3"

  current_l1:
    name: "Current L1"
  current_l2:
    name: "Current L2"
  current_l3:
    name: "Current L3"

  active_power_plus:
    name: "Active Power Import"
  active_power_minus:
    name: "Active Power Export"
  power_factor:
    name: "Power Factor"

  active_energy_plus:
    name: "Total Energy Import"
  active_energy_minus:
    name: "Total Energy Export"
  reactive_energy_plus:
    name: "Reactive Energy Import"
  reactive_energy_minus:
    name: "Reactive Energy Export"

  timestamp:
    name: "Meter Timestamp"
  meter_number:
    name: "Meter Number"
```

See `example_external_component.yaml` for a complete example configuration.

### Configuration options

| Option | Required | Description |
|--------|----------|-------------|
| `uart_id` | Yes | ID of the UART bus |
| `key` | Yes | 16-byte decryption key from your energy provider |
| `voltage_l1/l2/l3` | No | Voltage sensors for each phase |
| `current_l1/l2/l3` | No | Current sensors for each phase |
| `active_power_plus` | No | Active power import sensor |
| `active_power_minus` | No | Active power export sensor |
| `power_factor` | No | Power factor sensor |
| `active_energy_plus` | No | Total energy import sensor |
| `active_energy_minus` | No | Total energy export sensor |
| `reactive_energy_plus` | No | Reactive energy import sensor |
| `reactive_energy_minus` | No | Reactive energy export sensor |
| `timestamp` | No | Meter timestamp text sensor |
| `meter_number` | No | Meter number text sensor |

## Hardware installation

* Cut one end of the RJ11 cable and connect wires to pin 3 & 4 **OR** Plug RJ11 into a breakout board
* Connect everything according to this table:

| **ESP32** | **M-Bus Board** | **RJ11** | **Notes** |
| --------- | --------------- | -------- | --------- |
| 3.3V      | 3V3             | -        | 3.3V power |
| GND       | GND             | -        | Ground |
| GPIO4     | RX              | -        | Connect RX from the M-Bus board to TX from the ESP |
| GPIO36    | TX              | -        | Connect TX from the M-Bus board to RX from the ESP |
| -         | MBUS1           | 3        | Connect M-Bus board to smart meter, polarity does not matter |
| -         | MBUS2           | 4        | Connect M-Bus board to smart meter, polarity does not matter |

## Notes

* Tested with [ESP32-POE](https://www.olimex.com/Products/IoT/ESP32/ESP32-POE/open-source-hardware) (should work without problems over wifi)
* This is a fork of [DomiStyle/esphome-dlms-meter](https://github.com/DomiStyle/esphome-dlms-meter) updated for modern ESPHome

## Migration from custom_component

If you're upgrading from the old `custom_component` format, you need to:

1. Remove the `includes:` section from `esphome:`
2. Remove the `custom_component:` section with the lambda
3. Remove all the `platform: template` sensor definitions
4. Add `external_components:` pointing to this repository
5. Add the new `dlms_meter:` configuration block

The new format is much cleaner and all sensors are configured declaratively.
