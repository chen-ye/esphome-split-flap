import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import number
from esphome.const import (
    CONF_MODULE_INDEX,
    CONF_TYPE,
    UNIT_SECOND,
)

from . import split_flap_ns

# C++ Class References
SplitFlapDisplay = split_flap_ns.class_("SplitFlapDisplay", cg.Component)
SplitFlapPageTimeNumber = split_flap_ns.class_(
    "SplitFlapPageTimeNumber", number.Number, cg.Component
)
SplitFlapModuleOffsetNumber = split_flap_ns.class_(
    "SplitFlapModuleOffsetNumber", number.Number, cg.Component
)

CONF_SPLIT_FLAP_ID = "split_flap_id"
TYPE_PAGE_TIME = "page_time"
TYPE_MODULE_OFFSET = "module_offset"

CONFIG_SCHEMA = cv.typed_schema(
    {
        TYPE_PAGE_TIME: number.number_schema(
            SplitFlapPageTimeNumber,
            unit_of_measurement=UNIT_SECOND,
        )
        .extend(
            {
                cv.Required(CONF_SPLIT_FLAP_ID): cv.use_id(SplitFlapDisplay),
            }
        )
        .extend(cv.COMPONENT_SCHEMA),
        TYPE_MODULE_OFFSET: number.number_schema(
            SplitFlapModuleOffsetNumber,
        )
        .extend(
            {
                cv.Required(CONF_SPLIT_FLAP_ID): cv.use_id(SplitFlapDisplay),
                cv.Required(CONF_MODULE_INDEX): cv.positive_int,
            }
        )
        .extend(cv.COMPONENT_SCHEMA),
    },
    key=CONF_TYPE,
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_SPLIT_FLAP_ID])
    if config[CONF_TYPE] == TYPE_PAGE_TIME:
        var = await number.new_number(
            config,
            min_value=1.0,
            max_value=60.0,
            step=0.5,
        )
        await cg.register_component(var, config)
        cg.add(var.set_parent(parent))
        cg.add(parent.set_page_time_number(var))
    elif config[CONF_TYPE] == TYPE_MODULE_OFFSET:
        var = await number.new_number(
            config,
            min_value=-100,
            max_value=100,
            step=1,
        )
        await cg.register_component(var, config)
        cg.add(var.set_parent(parent))
        cg.add(var.set_module_index(config[CONF_MODULE_INDEX]))
        cg.add(parent.add_module_offset_number(config[CONF_MODULE_INDEX], var))
