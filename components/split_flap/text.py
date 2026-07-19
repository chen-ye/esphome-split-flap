import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import text, i2c
from esphome import automation
from esphome.const import CONF_ID, CONF_I2C_ID, CONF_VALUE
from . import split_flap_ns

# C++ Class Reference
SplitFlapDisplay = split_flap_ns.class_("SplitFlapDisplay", cg.Component, text.Text)

# Automation Action References
WriteStringAction = split_flap_ns.class_("WriteStringAction", automation.Action)
HomeAction = split_flap_ns.class_("HomeAction", automation.Action)
HomeToStringAction = split_flap_ns.class_("HomeToStringAction", automation.Action)

# Configuration keys
CONF_STEPS_PER_ROT = "steps_per_rot"
CONF_MAGNET_POSITION = "magnet_position"
CONF_DISPLAY_OFFSET = "display_offset"
CONF_MAX_VEL = "max_vel"
CONF_CHARSET = "charset"
CONF_HOME_ON_STARTUP = "home_on_startup"
CONF_MODULES = "modules"
CONF_OFFSET = "offset"
CONF_ADDRESS = "address"
CONF_SPEED = "speed"
CONF_CENTERING = "centering"

MODULE_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_ADDRESS): cv.i2c_address,
        cv.Optional(CONF_OFFSET, default=0): cv.int_,
    }
)

CONFIG_SCHEMA = (
    text.text_schema(SplitFlapDisplay)
    .extend(
        {
            cv.GenerateID(): cv.declare_id(SplitFlapDisplay),
            cv.GenerateID(CONF_I2C_ID): cv.use_id(i2c.I2CBus),
            cv.Optional(CONF_STEPS_PER_ROT, default=2048): cv.int_,
            cv.Optional(CONF_MAGNET_POSITION, default=730): cv.int_,
            cv.Optional(CONF_DISPLAY_OFFSET, default=0): cv.int_,
            cv.Optional(CONF_MAX_VEL, default=15.0): cv.positive_float,
            cv.Optional(
                CONF_CHARSET, default=" ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789':/?!.->$#%"
            ): cv.string,
            cv.Optional(CONF_HOME_ON_STARTUP, default=True): cv.boolean,
            cv.Required(CONF_MODULES): cv.ensure_list(MODULE_SCHEMA),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await text.register_text(var, config)

    # Resolve and set the I2CBus
    parent = await cg.get_variable(config[CONF_I2C_ID])
    cg.add(var.set_i2c_bus(parent))

    # Set parameters
    cg.add(var.set_steps_per_rot(config[CONF_STEPS_PER_ROT]))
    cg.add(var.set_magnet_position(config[CONF_MAGNET_POSITION]))
    cg.add(var.set_display_offset(config[CONF_DISPLAY_OFFSET]))
    cg.add(var.set_max_vel(config[CONF_MAX_VEL]))
    cg.add(var.set_charset(config[CONF_CHARSET]))
    cg.add(var.set_home_on_startup(config[CONF_HOME_ON_STARTUP]))

    # Add each module configuration
    for module_conf in config[CONF_MODULES]:
        cg.add(var.add_module(module_conf[CONF_ADDRESS], module_conf[CONF_OFFSET]))


# Action Code Gen Registration
@automation.register_action(
    "split_flap.write",
    WriteStringAction,
    cv.Schema(
        {
            cv.Required(CONF_ID): cv.use_id(SplitFlapDisplay),
            cv.Required(CONF_VALUE): cv.templatable(cv.string),
            cv.Optional(CONF_SPEED): cv.templatable(cv.positive_float),
            cv.Optional(CONF_CENTERING): cv.templatable(cv.boolean),
        }
    ),
    synchronous=False,
)
async def split_flap_write_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    template_ = await cg.templatable(config[CONF_VALUE], args, cg.std_string)
    cg.add(var.set_value(template_))
    if CONF_SPEED in config:
        template_ = await cg.templatable(config[CONF_SPEED], args, cg.float_)
        cg.add(var.set_speed(template_))
    if CONF_CENTERING in config:
        template_ = await cg.templatable(config[CONF_CENTERING], args, cg.bool_)
        cg.add(var.set_centering(template_))
    return var


@automation.register_action(
    "split_flap.home",
    HomeAction,
    cv.Schema(
        {
            cv.Required(CONF_ID): cv.use_id(SplitFlapDisplay),
            cv.Optional(CONF_SPEED): cv.templatable(cv.positive_float),
        }
    ),
    synchronous=False,
)
async def split_flap_home_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    if CONF_SPEED in config:
        template_ = await cg.templatable(config[CONF_SPEED], args, cg.float_)
        cg.add(var.set_speed(template_))
    return var


@automation.register_action(
    "split_flap.home_to_string",
    HomeToStringAction,
    cv.Schema(
        {
            cv.Required(CONF_ID): cv.use_id(SplitFlapDisplay),
            cv.Required(CONF_VALUE): cv.templatable(cv.string),
            cv.Optional(CONF_SPEED): cv.templatable(cv.positive_float),
        }
    ),
    synchronous=False,
)
async def split_flap_home_to_string_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    template_ = await cg.templatable(config[CONF_VALUE], args, cg.std_string)
    cg.add(var.set_value(template_))
    if CONF_SPEED in config:
        template_ = await cg.templatable(config[CONF_SPEED], args, cg.float_)
        cg.add(var.set_speed(template_))
    return var
