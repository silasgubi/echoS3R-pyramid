import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import i2c, light
from esphome.const import CONF_OUTPUT_ID

from .. import pyramidrgb_ns

CODEOWNERS = ["@Jasionf"]
DEPENDENCIES = ["i2c"]

PyramidRGBLight = pyramidrgb_ns.class_(
    "PyramidRGBLight", light.AddressableLight, i2c.I2CDevice
)

CONF_STRIP_BRIGHTNESS = "strip_brightness"
CONF_PER_LED_WRITE = "per_led_write"

CONFIG_SCHEMA = (
    light.ADDRESSABLE_LIGHT_SCHEMA.extend(
        {
            cv.GenerateID(CONF_OUTPUT_ID): cv.declare_id(PyramidRGBLight),
            cv.Optional(CONF_STRIP_BRIGHTNESS, default=80): cv.int_range(
                min=0, max=100
            ),
            cv.Optional(CONF_PER_LED_WRITE, default=False): cv.boolean,
        }
    )
    .extend(i2c.i2c_device_schema(0x1A))
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_OUTPUT_ID])
    await light.register_light(var, config)
    await cg.register_component(var, config)
    await i2c.register_i2c_device(var, config)
    cg.add(var.set_initial_brightness(config[CONF_STRIP_BRIGHTNESS]))
    cg.add(var.set_per_led_write(config[CONF_PER_LED_WRITE]))
