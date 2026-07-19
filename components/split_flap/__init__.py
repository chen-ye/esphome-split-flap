import esphome.codegen as cg

# Define the global C++ namespace for our component
split_flap_ns = cg.esphome_ns.namespace("split_flap")

# Declare dependencies (this component uses the global I2C bus)
DEPENDENCIES = ["i2c"]
