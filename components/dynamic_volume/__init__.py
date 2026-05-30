import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import microphone, media_player
from esphome.const import CONF_ID

CODEOWNERS = ["@eddyfussel"]
DEPENDENCIES = ["microphone", "media_player"]

CONF_MICROPHONE = "microphone"
CONF_MEDIA_PLAYER = "media_player"
CONF_SMOOTHING_FACTOR = "smoothing_factor"

dynamic_volume_ns = cg.esphome_ns.namespace("dynamic_volume")
DynamicVolumeComponent = dynamic_volume_ns.class_(
    "DynamicVolumeComponent", cg.Component
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(DynamicVolumeComponent),
        cv.Required(CONF_MICROPHONE): cv.use_id(microphone.Microphone),
        cv.Required(CONF_MEDIA_PLAYER): cv.use_id(media_player.MediaPlayer),
        cv.Optional(CONF_SMOOTHING_FACTOR, default=0.05): cv.float_range(
            min=0.001, max=1.0
        ),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    mic = await cg.get_variable(config[CONF_MICROPHONE])
    cg.add(var.set_microphone(mic))

    mp = await cg.get_variable(config[CONF_MEDIA_PLAYER])
    cg.add(var.set_media_player(mp))

    cg.add(var.set_smoothing_factor(config[CONF_SMOOTHING_FACTOR]))
