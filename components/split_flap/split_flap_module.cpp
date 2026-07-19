#include "split_flap_module.h"
#include "esphome/core/log.h"
#include <cctype>

namespace esphome {
namespace split_flap {

static const char *const TAG = "split_flap.module";

const char SplitFlapModule::StandardChars[37] = {
    ' ', 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L',
    'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y',
    'Z', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9'
};

const char SplitFlapModule::ExtendedChars[48] = {
    ' ', 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O',
    'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', '0', '1', '2', '3', '4',
    '5', '6', '7', '8', '9', '\'', ':', '/', '?', '!', '.', '-', '>', '$', '#', '%'
};

SplitFlapModule::SplitFlapModule(uint8_t address, int steps_per_rotation, int step_offset, int magnet_position, const std::string &charset) {
  this->set_i2c_address(address);
  this->steps_per_rot_ = steps_per_rotation;
  this->step_offset_ = step_offset;
  this->base_magnet_position_ = magnet_position;

  int len = charset.length();
  if (len < 37) {
    this->num_chars_ = 37;
    this->custom_chars_ = std::string(StandardChars, 37);
  } else {
    this->num_chars_ = (len >= 48) ? 48 : 37;
    this->custom_chars_ = charset.substr(0, this->num_chars_);
  }
}

SplitFlapModule::SplitFlapModule(uint8_t address, int steps_per_rotation, number::Number *offset_number, int magnet_position, const std::string &charset) {
  this->set_i2c_address(address);
  this->steps_per_rot_ = steps_per_rotation;
  this->offset_number_ = offset_number;
  this->base_magnet_position_ = magnet_position;

  int len = charset.length();
  if (len < 37) {
    this->num_chars_ = 37;
    this->custom_chars_ = std::string(StandardChars, 37);
  } else {
    this->num_chars_ = (len >= 48) ? 48 : 37;
    this->custom_chars_ = charset.substr(0, this->num_chars_);
  }
}

void SplitFlapModule::update_cached_offset() {
  if (this->offset_number_ != nullptr && !std::isnan(this->offset_number_->state)) {
    this->step_offset_ = (int) this->offset_number_->state;
  }
}

int SplitFlapModule::get_step_offset() const {
  return this->step_offset_;
}

int SplitFlapModule::get_magnet_position() const {
  return this->base_magnet_position_;
}

inline void __attribute__((always_inline)) SplitFlapModule::write_io(uint16_t data) {
  uint8_t buffer[2] = {(uint8_t)(data & 0xFF), (uint8_t)((data >> 8) & 0xFF)};
  auto error = this->write(buffer, 2);
  if (error != i2c::ERROR_OK && !this->has_errored_) {
    this->has_errored_ = true;
    ESP_LOGE(TAG, "Error writing data to module 0x%02X, error code: %d", this->address_, (int) error);
  }
}

void SplitFlapModule::init() {
  // Build char-position lookup table.
  float step_size = (float) this->steps_per_rot_ / (float) this->num_chars_;
  float current_position = 0;
  this->char_positions_.resize(this->num_chars_);
  for (int i = 0; i < this->num_chars_; i++) {
    this->char_positions_[i] = (int) current_position;
    current_position += step_size;
  }

  uint16_t init_state = 0b1111111111100001; // Pin 15 (17) as INPUT, Pins 1-4 as OUTPUT
  this->write_io(init_state);

  this->stop();

  int init_delay = 100;
  delay(init_delay);
  this->step();
  delay(init_delay);
  this->step();
  delay(init_delay);
  this->step();
  delay(init_delay);
  this->step();
  delay(init_delay);

  this->stop();
}

int SplitFlapModule::get_char_position(char input_char) {
  input_char = std::toupper(input_char);
  for (int i = 0; i < this->num_chars_; i++) {
    if (this->custom_chars_[i] == input_char) {
      return this->char_positions_[i];
    }
  }
  return 0;
}

void SplitFlapModule::stop() {
  uint16_t step_state = 0b1111111111100001;
  this->write_io(step_state);
}

void SplitFlapModule::start() {
  this->step_number_ = (this->step_number_ + 3) % 4;
  this->step(false);
}

void __attribute__((hot)) SplitFlapModule::step(bool update_position) {
  uint16_t step_state;
  switch (this->step_number_) {
    case 0: step_state = 0b1111111111100111; break;
    case 1: step_state = 0b1111111111110011; break;
    case 2: step_state = 0b1111111111111001; break;
    case 3: step_state = 0b1111111111101101; break;
    default: step_state = 0b1111111111100001; break;
  }
  this->write_io(step_state);
  if (update_position) {
    this->position_ = (this->position_ + 1) % this->steps_per_rot_;
    this->step_number_ = (this->step_number_ + 1) % 4;
  }
}

bool SplitFlapModule::read_hall_effect_sensor() {
  if (this->has_errored_) {
    return false;
  }

  // Raw read for PCF8575 (no register prefix)
  uint8_t buffer[2] = {0, 0};
  bool ok = this->read(buffer, 2) == i2c::ERROR_OK;
  if (ok) {
    // For PCF8575:
    // buffer[0] is Port 0 (P00-P07), which we use for the motor outputs.
    // buffer[1] is Port 1 (P10-P17).
    // The Hall effect sensor is on P17 (bit 7 of Port 1).
    // An active LOW sensor means it reads 0 when a magnet is present.
    bool magnet_present = (buffer[1] & (1u << 7)) == 0;

    if (magnet_present) {
      this->has_magnet_detected_ = true;
    } else {
      this->has_magnet_detected_ = false;
    }
    return magnet_present; // true = magnet is present
  } else {
    ESP_LOGE(TAG, "I2C ERROR reading hall effect sensor on module 0x%02X!", this->address_);
  }
  
  return false; // I2C error = assume no magnet to prevent hallucinated triggers
}

}  // namespace split_flap
}  // namespace esphome
