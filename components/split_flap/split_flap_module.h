#pragma once

#include "esphome/core/component.h"
#include "esphome/components/i2c/i2c.h"
#include "esphome/components/number/number.h"
#include <string>
#include <vector>

namespace esphome {
namespace split_flap {

class SplitFlapModule : public i2c::I2CDevice {
 public:
  SplitFlapModule() = default;
  SplitFlapModule(uint8_t address, int steps_per_rotation, int step_offset, int magnet_position, const std::string &charset);
  SplitFlapModule(uint8_t address, int steps_per_rotation, number::Number *offset_number, int magnet_position, const std::string &charset);

  void init();
  void step(bool update_position = true) __attribute__((hot));
  void stop();
  void start();

  int get_magnet_position() const;
  int get_step_offset() const;
  void update_cached_offset();
  int get_char_position(char input_char);
  int get_position() const { return this->position_; }
  int get_charset_size() const { return this->num_chars_; }
  uint8_t get_address() const { return this->address_; }

  bool read_hall_effect_sensor();
  void magnet_detected() { this->position_ = this->get_magnet_position(); }

  void reset_state() {
    this->has_magnet_detected_ = false;
  }

  bool get_has_errored() const { return this->has_errored_; }
  bool get_has_magnet_detected() const { return this->has_magnet_detected_; }

 protected:
  inline void write_io(uint16_t data) __attribute__((always_inline));

  int position_ = 0;
  int step_number_ = 0;
  int steps_per_rot_ = 2048;
  bool has_errored_ = false;
  bool has_magnet_detected_ = false;
  int base_magnet_position_ = 730;
  int step_offset_ = 0;
  number::Number *offset_number_{nullptr};

  std::vector<int> char_positions_;
  int num_chars_ = 0;
  std::string custom_chars_;

  static const char StandardChars[37];
  static const char ExtendedChars[48];
};

}  // namespace split_flap
}  // namespace esphome
