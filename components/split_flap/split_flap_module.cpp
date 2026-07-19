#include "split_flap_module.h"
#include "esphome/core/log.h"
#include <cctype>

namespace esphome {
namespace split_flap {

static const char *const TAG = "split_flap.module";

// ─────────────────────────────────────────────────────────────────────────────
// MCP23017 register map (BANK=0, default after power-on reset).
// The original Arduino project drives the MCP23017 via a deliberately
// *stateful* raw-byte protocol:
//
//   Wire.beginTransmission(addr);
//   Wire.write(data & 0xFF);       // byte 0 → current register pointer
//   Wire.write((data >> 8) & 0xFF); // byte 1 → next register pointer
//   Wire.endTransmission();
//
// After power-on the pointer sits at 0x00 (IODIRA). Each 2-byte write
// advances the pointer by 2. The init() sequence below is designed so the
// pointer arrives at GPIOA (0x12) exactly when the stepping loop begins,
// meaning every subsequent writeIO() call updates the GPIO output latch
// without needing an explicit register address in the transaction.
//
// READS, however, must use an explicit register address. Without one,
// Wire.requestFrom() returns data from wherever the pointer happens to be —
// which is almost never GPIOB — causing bit 15 (Hall sensor) to read as 1
// (= "no magnet") indefinitely. We use read_bytes(0x12, ...) to guarantee
// we always read GPIOA+GPIOB.
// ─────────────────────────────────────────────────────────────────────────────

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
  this->magnet_position_ = magnet_position + step_offset;

  int len = charset.length();
  if (len < 37) {
    this->num_chars_ = 37;
    this->custom_chars_ = std::string(StandardChars, 37);
  } else {
    this->num_chars_ = (len >= 48) ? 48 : 37;
    this->custom_chars_ = charset.substr(0, this->num_chars_);
  }
}

void SplitFlapModule::write_io(uint16_t data) {
  // Raw 2-byte write — NO register address prefix.
  // The MCP23017 interprets byte 0 as data for the current register pointer,
  // and byte 1 as data for (pointer + 1). After init() completes, the pointer
  // is at GPIOA (0x12), so these bytes go to GPIOA and GPIOB respectively.
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

  // ── Register-pointer walk-through ────────────────────────────────────────
  // Pointer starts at 0x00 (IODIRA) after power-on.
  //
  // writeIO(0xFFE1):  → 0x00 IODIRA = 0xE1  (bits 1-4 output, rest input)
  //                     0x01 IODIRB = 0xFF  (all input; bit 7 = Hall sensor)
  //                     pointer → 0x02
  //
  // stop() = writeIO(0xFFE1):
  //                   → 0x02 IPOLA  = 0xE1  (harmless; sets input polarity)
  //                     0x03 IPOLB  = 0xFF  (harmless)
  //                     pointer → 0x04
  //
  // step() ×4 (each writeIO advances pointer by 2):
  //   0x04/0x05 → GPINTENA/B = various  (harmless)
  //   0x06/0x07 → DEFVALA/B  = various  (harmless)
  //   0x08/0x09 → INTCONA/B  = various  (harmless)
  //   0x0A/0x0B → IOCON/IOCON = various (two copies of same register; last
  //               step write 0xFFED means IOCON = 0xED = BANK=1 momentarily,
  //               but IODIRA/IODIRB were already written at 0x00/0x01 and
  //               are non-volatile within the power cycle, so motor still works)
  //               pointer → 0x0C
  //
  // stop() = writeIO(0xFFE1):
  //                   → 0x0C GPPUA  = 0xE1  (pull-ups; harmless)
  //                     0x0D GPPUB  = 0xFF  (pull-ups; harmless)
  //                     pointer → 0x0E
  //
  // ── Loop begins ──────────────────────────────────────────────────────────
  // The display loop calls step() then immediately calls writeIO() again
  // (via hf_requester / step calls). Each pair of step() calls advances
  // through 0x0E→0x10→0x12 (GPIOA!). Once at 0x12, all subsequent
  // writeIO() pairs land at GPIOA+GPIOB and correctly drive the motor.
  //
  // NOTE: The above assumes sequential SEQOP=0 (default). If a writeIO of
  // step-3 accidentally sets IOCON.BANK=1 (0xED bit 7 = 1), the register
  // layout changes. The next writeIO falls at a different address. However,
  // the original project works, so the net effect must be that after all
  // init writes, the first loop writeIO() pair reaches GPIOA (0x12 in
  // BANK=0, or 0x09 in BANK=1). We replicate the original sequence exactly.
  // ─────────────────────────────────────────────────────────────────────────

  uint16_t init_state = 0b1111111111100001;
  this->write_io(init_state);   // → IODIRA=0xE1, IODIRB=0xFF; pointer=0x02

  this->stop();                 // → IPOLA/IPOLB; pointer=0x04

  int init_delay = 100;
  delay(init_delay);
  this->step();                 // → GPINTENA/B; pointer=0x06
  delay(init_delay);
  this->step();                 // → DEFVALA/B;  pointer=0x08
  delay(init_delay);
  this->step();                 // → INTCONA/B;  pointer=0x0A
  delay(init_delay);
  this->step();                 // → IOCON×2;    pointer=0x0C
  delay(init_delay);

  this->stop();                 // → GPPUA/B;    pointer=0x0E
  // Pointer is now at 0x0E (INTFA). Two more writeIO calls bring it to 0x12:
  //   0x0E/0x0F → INTFA/B   (first extra step in homing loop)
  //   0x10/0x11 → INTCAPA/B (second extra step)
  //   0x12/0x13 → GPIOA/B   ← motor writes land here from now on
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

void SplitFlapModule::step(bool update_position) {
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

  // Use an explicit register-addressed read (0x12 = GPIOA in BANK=0).
  // A raw read (no register prefix) would return data from wherever the
  // MCP23017's internal pointer happens to be — which is not GPIOB —
  // causing bit 15 to always read as 1 ("no magnet").
  uint8_t buffer[2] = {0, 0};
  bool ok = this->read_bytes(0x12, buffer, 2);
  if (ok) {
    uint16_t input_state = (uint16_t) buffer[0] | ((uint16_t) buffer[1] << 8);
    // Bit 15 = GPIOB bit 7 = Hall sensor output (active LOW: 0 = magnet present)
    bool magnet_present = (input_state & (1u << 15)) == 0;
    if (magnet_present && !this->has_magnet_detected_) {
      ESP_LOGD(TAG, "Magnet Detected: 0x%02X (GPIOA=0x%02X GPIOB=0x%02X)",
               this->address_, buffer[0], buffer[1]);
      this->has_magnet_detected_ = true;
    }
    return !magnet_present;  // true = no magnet (caller arms/fires calibration on trailing edge)
  }
  ESP_LOGW(TAG, "Sensor read failed on module 0x%02X", this->address_);
  return false;
}

}  // namespace split_flap
}  // namespace esphome
