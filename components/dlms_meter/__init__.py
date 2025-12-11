import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart, sensor, text_sensor, mqtt
from esphome.const import (
    CONF_ID,
    CONF_KEY,
    CONF_VOLTAGE,
    CONF_CURRENT,
    UNIT_VOLT,
    UNIT_AMPERE,
    UNIT_WATT,
    UNIT_WATT_HOURS,
    DEVICE_CLASS_VOLTAGE,
    DEVICE_CLASS_CURRENT,
    DEVICE_CLASS_POWER,
    DEVICE_CLASS_ENERGY,
    DEVICE_CLASS_POWER_FACTOR,
    STATE_CLASS_MEASUREMENT,
    STATE_CLASS_TOTAL_INCREASING,
)

CODEOWNERS = ["@packerlschupfer"]
DEPENDENCIES = ["uart"]
AUTO_LOAD = ["sensor", "text_sensor"]

CONF_VOLTAGE_L1 = "voltage_l1"
CONF_VOLTAGE_L2 = "voltage_l2"
CONF_VOLTAGE_L3 = "voltage_l3"
CONF_CURRENT_L1 = "current_l1"
CONF_CURRENT_L2 = "current_l2"
CONF_CURRENT_L3 = "current_l3"
CONF_ACTIVE_POWER_PLUS = "active_power_plus"
CONF_ACTIVE_POWER_MINUS = "active_power_minus"
CONF_POWER_FACTOR = "power_factor"
CONF_ACTIVE_ENERGY_PLUS = "active_energy_plus"
CONF_ACTIVE_ENERGY_MINUS = "active_energy_minus"
CONF_REACTIVE_ENERGY_PLUS = "reactive_energy_plus"
CONF_REACTIVE_ENERGY_MINUS = "reactive_energy_minus"
CONF_TIMESTAMP = "timestamp"
CONF_METER_NUMBER = "meter_number"
CONF_MQTT_TOPIC = "mqtt_topic"

dlms_meter_ns = cg.esphome_ns.namespace("dlms_meter")
DlmsMeter = dlms_meter_ns.class_("DlmsMeter", cg.Component, uart.UARTDevice)

SENSOR_SCHEMA = sensor.sensor_schema(
    accuracy_decimals=1,
    state_class=STATE_CLASS_MEASUREMENT,
)

VOLTAGE_SCHEMA = sensor.sensor_schema(
    unit_of_measurement=UNIT_VOLT,
    accuracy_decimals=1,
    device_class=DEVICE_CLASS_VOLTAGE,
    state_class=STATE_CLASS_MEASUREMENT,
)

CURRENT_SCHEMA = sensor.sensor_schema(
    unit_of_measurement=UNIT_AMPERE,
    accuracy_decimals=2,
    device_class=DEVICE_CLASS_CURRENT,
    state_class=STATE_CLASS_MEASUREMENT,
)

POWER_SCHEMA = sensor.sensor_schema(
    unit_of_measurement=UNIT_WATT,
    accuracy_decimals=0,
    device_class=DEVICE_CLASS_POWER,
    state_class=STATE_CLASS_MEASUREMENT,
)

POWER_FACTOR_SCHEMA = sensor.sensor_schema(
    accuracy_decimals=3,
    device_class=DEVICE_CLASS_POWER_FACTOR,
    state_class=STATE_CLASS_MEASUREMENT,
)

ENERGY_SCHEMA = sensor.sensor_schema(
    unit_of_measurement=UNIT_WATT_HOURS,
    accuracy_decimals=0,
    device_class=DEVICE_CLASS_ENERGY,
    state_class=STATE_CLASS_TOTAL_INCREASING,
)

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(DlmsMeter),
            cv.Required(CONF_KEY): cv.All(
                cv.ensure_list(cv.hex_uint8_t), cv.Length(min=16, max=16)
            ),
            cv.Optional(CONF_VOLTAGE_L1): VOLTAGE_SCHEMA,
            cv.Optional(CONF_VOLTAGE_L2): VOLTAGE_SCHEMA,
            cv.Optional(CONF_VOLTAGE_L3): VOLTAGE_SCHEMA,
            cv.Optional(CONF_CURRENT_L1): CURRENT_SCHEMA,
            cv.Optional(CONF_CURRENT_L2): CURRENT_SCHEMA,
            cv.Optional(CONF_CURRENT_L3): CURRENT_SCHEMA,
            cv.Optional(CONF_ACTIVE_POWER_PLUS): POWER_SCHEMA,
            cv.Optional(CONF_ACTIVE_POWER_MINUS): POWER_SCHEMA,
            cv.Optional(CONF_POWER_FACTOR): POWER_FACTOR_SCHEMA,
            cv.Optional(CONF_ACTIVE_ENERGY_PLUS): ENERGY_SCHEMA,
            cv.Optional(CONF_ACTIVE_ENERGY_MINUS): ENERGY_SCHEMA,
            cv.Optional(CONF_REACTIVE_ENERGY_PLUS): ENERGY_SCHEMA,
            cv.Optional(CONF_REACTIVE_ENERGY_MINUS): ENERGY_SCHEMA,
            cv.Optional(CONF_TIMESTAMP): text_sensor.text_sensor_schema(),
            cv.Optional(CONF_METER_NUMBER): text_sensor.text_sensor_schema(),
            cv.Optional(CONF_MQTT_TOPIC): cv.string,
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(uart.UART_DEVICE_SCHEMA)
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)

    # Set decryption key
    key = config[CONF_KEY]
    cg.add(var.set_key(key))

    # Voltage sensors
    if CONF_VOLTAGE_L1 in config:
        sens = await sensor.new_sensor(config[CONF_VOLTAGE_L1])
        cg.add(var.set_voltage_l1_sensor(sens))
    if CONF_VOLTAGE_L2 in config:
        sens = await sensor.new_sensor(config[CONF_VOLTAGE_L2])
        cg.add(var.set_voltage_l2_sensor(sens))
    if CONF_VOLTAGE_L3 in config:
        sens = await sensor.new_sensor(config[CONF_VOLTAGE_L3])
        cg.add(var.set_voltage_l3_sensor(sens))

    # Current sensors
    if CONF_CURRENT_L1 in config:
        sens = await sensor.new_sensor(config[CONF_CURRENT_L1])
        cg.add(var.set_current_l1_sensor(sens))
    if CONF_CURRENT_L2 in config:
        sens = await sensor.new_sensor(config[CONF_CURRENT_L2])
        cg.add(var.set_current_l2_sensor(sens))
    if CONF_CURRENT_L3 in config:
        sens = await sensor.new_sensor(config[CONF_CURRENT_L3])
        cg.add(var.set_current_l3_sensor(sens))

    # Power sensors
    if CONF_ACTIVE_POWER_PLUS in config:
        sens = await sensor.new_sensor(config[CONF_ACTIVE_POWER_PLUS])
        cg.add(var.set_active_power_plus_sensor(sens))
    if CONF_ACTIVE_POWER_MINUS in config:
        sens = await sensor.new_sensor(config[CONF_ACTIVE_POWER_MINUS])
        cg.add(var.set_active_power_minus_sensor(sens))
    if CONF_POWER_FACTOR in config:
        sens = await sensor.new_sensor(config[CONF_POWER_FACTOR])
        cg.add(var.set_power_factor_sensor(sens))

    # Energy sensors
    if CONF_ACTIVE_ENERGY_PLUS in config:
        sens = await sensor.new_sensor(config[CONF_ACTIVE_ENERGY_PLUS])
        cg.add(var.set_active_energy_plus_sensor(sens))
    if CONF_ACTIVE_ENERGY_MINUS in config:
        sens = await sensor.new_sensor(config[CONF_ACTIVE_ENERGY_MINUS])
        cg.add(var.set_active_energy_minus_sensor(sens))
    if CONF_REACTIVE_ENERGY_PLUS in config:
        sens = await sensor.new_sensor(config[CONF_REACTIVE_ENERGY_PLUS])
        cg.add(var.set_reactive_energy_plus_sensor(sens))
    if CONF_REACTIVE_ENERGY_MINUS in config:
        sens = await sensor.new_sensor(config[CONF_REACTIVE_ENERGY_MINUS])
        cg.add(var.set_reactive_energy_minus_sensor(sens))

    # Text sensors
    if CONF_TIMESTAMP in config:
        sens = await text_sensor.new_text_sensor(config[CONF_TIMESTAMP])
        cg.add(var.set_timestamp_sensor(sens))
    if CONF_METER_NUMBER in config:
        sens = await text_sensor.new_text_sensor(config[CONF_METER_NUMBER])
        cg.add(var.set_meter_number_sensor(sens))

    # MQTT topic
    if CONF_MQTT_TOPIC in config:
        cg.add(var.set_mqtt_topic(config[CONF_MQTT_TOPIC]))
