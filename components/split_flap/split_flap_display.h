#pragma once

#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "esphome/components/text/text.h"
#include "esphome/components/i2c/i2c.h"
#include "split_flap_module.h"
#include <string>
#include <vector>

namespace esphome {
namespace split_flap {

enum State {
  STATE_IDLE,
  STATE_START_STEPS,
  STATE_STEPPING,
  STATE_SETTLE,
  STATE_STOPPING
};

class SplitFlapDisplay : public Component, public text::Text {
 public:
  SplitFlapDisplay() = default;
  ~SplitFlapDisplay();

  void setup() override;
  void loop() override;
  void dump_config() override;

  // text::Text implementation
  void control(const std::string &value) override;

  // Configurations
  void set_i2c_bus(i2c::I2CBus *bus) { this->bus_ = bus; }
  void set_steps_per_rot(int steps) { this->steps_per_rot_ = steps; }
  void set_magnet_position(int pos) { this->magnet_position_ = pos; }
  void set_display_offset(int offset) { this->display_offset_ = offset; }
  void set_max_vel(float max_vel) { this->max_vel_ = max_vel; }
  void set_charset(const std::string &charset) { this->charset_ = charset; }
  void set_home_on_startup(bool home_on_startup) { this->home_on_startup_ = home_on_startup; }

  void add_module(uint8_t address, int offset);

  // Operational methods
  void write_string(const std::string &input_string, float speed = -1.0f, bool centering = true);
  void home(float speed = -1.0f);
  void home_to_string(const std::string &home_string, float speed = -1.0f);

 protected:
  void start_motors();
  void stop_motors();
  void start_movement();

  i2c::I2CBus *bus_{nullptr};
  int steps_per_rot_{2048};
  int magnet_position_{730};
  int display_offset_{0};
  float max_vel_{15.0f};
  std::string charset_{" ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789':/?!.->$#%"};
  bool home_on_startup_{true};

  std::vector<SplitFlapModule *> modules_;

  // State Machine Variables
  State state_{STATE_IDLE};

  // Diagnostic tracking for loop jitter
  unsigned long max_step_delay_us_{0};

  std::vector<int> target_positions_;
  std::vector<bool> needs_stepping_;
  std::vector<unsigned long> last_step_times_;
  std::vector<bool> reset_latches_;
  unsigned long last_sensor_check_time_{0};
  unsigned long state_timer_{0};
  float current_speed_{15.0f};
  unsigned long time_per_step_us_{0};
  bool release_motors_{true};
  bool homing_stage_2_pending_{false};
  std::string pending_string_;
  std::string current_displayed_text_;

  HighFrequencyLoopRequester hf_requester_;
};

// Automation Actions
template<typename... Ts>
class WriteStringAction : public Action<Ts...>, public Parented<SplitFlapDisplay> {
 public:
  TEMPLATABLE_VALUE(std::string, value)
  TEMPLATABLE_VALUE(float, speed)
  TEMPLATABLE_VALUE(bool, centering)

  void play(Ts... x) override {
    auto val = this->value_.value(x...);
    float spd = this->speed_.has_value() ? this->speed_.value(x...) : -1.0f;
    bool cent = this->centering_.has_value() ? this->centering_.value(x...) : true;
    this->parent_->write_string(val, spd, cent);
  }
};

template<typename... Ts>
class HomeAction : public Action<Ts...>, public Parented<SplitFlapDisplay> {
 public:
  TEMPLATABLE_VALUE(float, speed)

  void play(Ts... x) override {
    float spd = this->speed_.has_value() ? this->speed_.value(x...) : -1.0f;
    this->parent_->home(spd);
  }
};

template<typename... Ts>
class HomeToStringAction : public Action<Ts...>, public Parented<SplitFlapDisplay> {
 public:
  TEMPLATABLE_VALUE(std::string, value)
  TEMPLATABLE_VALUE(float, speed)

  void play(Ts... x) override {
    auto val = this->value_.value(x...);
    float spd = this->speed_.has_value() ? this->speed_.value(x...) : -1.0f;
    this->parent_->home_to_string(val, spd);
  }
};

}  // namespace split_flap
}  // namespace esphome
