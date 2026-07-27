#pragma once

#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "esphome/components/text/text.h"
#include "esphome/components/i2c/i2c.h"
#include "esphome/core/preferences.h"
#include "split_flap_module.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string>
#include <vector>

namespace esphome {
namespace split_flap {

enum State { STATE_IDLE, STATE_NETWORK_COOLDOWN, STATE_START_STEPS, STATE_STEPPING, STATE_SETTLE, STATE_STOPPING };

class SplitFlapDisplay;

class SplitFlapPageTimeNumber : public number::Number, public Component {
 public:
  SplitFlapPageTimeNumber() = default;
  void set_parent(SplitFlapDisplay *parent) { this->parent_ = parent; }
  void set_initial_value(float value) { this->initial_value_ = value; }
  void set_restore_value(bool restore) { this->restore_value_ = restore; }
  void setup() override;

 protected:
  void control(float value) override;
  SplitFlapDisplay *parent_{nullptr};
  float initial_value_{3.0f};
  bool restore_value_{true};
  ESPPreferenceObject pref_;
};

class SplitFlapModuleOffsetNumber : public number::Number, public Component {
 public:
  SplitFlapModuleOffsetNumber() = default;
  void set_parent(SplitFlapDisplay *parent) { this->parent_ = parent; }
  void set_module_index(size_t index) { this->module_index_ = index; }
  void set_initial_value(float value) { this->initial_value_ = value; }
  void set_restore_value(bool restore) { this->restore_value_ = restore; }
  void setup() override;

 protected:
  void control(float value) override;
  SplitFlapDisplay *parent_{nullptr};
  size_t module_index_{0};
  float initial_value_{0.0f};
  bool restore_value_{true};
  ESPPreferenceObject pref_;
};

class SplitFlapDisplay : public Component, public text::Text {
 public:
  SplitFlapDisplay() = default;
  ~SplitFlapDisplay();

  void setup() override;
  void loop() override __attribute__((hot));
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
  void set_startup_string(const std::string &startup_string);
  void set_page_time(uint32_t ms) { this->default_page_time_ms_ = ms; }
  void set_page_time_number(number::Number *num) { this->page_time_number_ = num; }

  void add_module(uint8_t address, int offset);
  void add_module(uint8_t address, number::Number *offset_number);
  void add_module_offset_number(size_t index, number::Number *offset_number);

  uint32_t get_page_time_ms() const;

  // Operational methods
  void write_string(const std::string &input_string, float speed = -1.0f, bool centering = true);
  void write_paginated(const std::string &input_string, int32_t page_time_ms = -1, float speed = -1.0f,
                       bool centering = true);
  void clear_pagination();
  void home(float speed = -1.0f, bool use_startup_string = false);
  void home_to_string(const std::string &home_string, float speed = -1.0f);
  void step_9_test();

 protected:
  void start_motors();
  void stop_motors();
  void start_movement();
  void write_page_internal(const std::string &input_string, float speed = -1.0f, bool centering = true);

  i2c::I2CBus *bus_{nullptr};
  int steps_per_rot_{2048};
  int magnet_position_{730};
  int display_offset_{0};
  float max_vel_{15.0f};
  std::string charset_{" ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789':/?!.->$#%"};
  bool home_on_startup_{true};
  std::string startup_string_{""};
  std::vector<std::string> startup_lines_;
  size_t startup_line_idx_{0};

  number::Number *page_time_number_{nullptr};
  uint32_t default_page_time_ms_{3000};
  std::vector<std::string> paginated_lines_;
  size_t paginated_line_idx_{0};
  uint32_t current_page_interval_ms_{3000};
  unsigned long last_page_advance_time_ms_{0};
  float paginated_speed_{-1.0f};
  bool paginated_centering_{true};

  std::vector<SplitFlapModule *> modules_;

  // State Machine Variables
  State state_{STATE_IDLE};

  // Diagnostic tracking for loop jitter
  unsigned long max_step_delay_us_{0};

  std::vector<int> target_positions_;
  std::vector<bool> needs_stepping_;
  std::vector<unsigned long> last_step_times_;
  std::vector<bool> calibrated_this_move_;
  std::vector<bool> was_magnet_present_;

  struct CalibrationEvent {
    size_t module_index;
    int current_pos;
    int reset_pos;
    int target_pos;
    bool triggered;
  };
  std::vector<CalibrationEvent> calibration_events_;

  unsigned long last_sensor_check_time_{0};
  unsigned long state_timer_{0};
  float current_speed_{15.0f};
  unsigned long time_per_step_us_{0};
  bool release_motors_{true};
  bool homing_stage_2_pending_{false};
  std::string pending_string_;
  std::string current_displayed_text_;
  size_t test_step_index_{1};

  HighFrequencyLoopRequester hf_requester_;

  static void step_task_fn(void *param) __attribute__((hot));
  TaskHandle_t step_task_handle_{nullptr};
};

// Automation Actions
template<typename... Ts> class WriteStringAction : public Action<Ts...>, public Parented<SplitFlapDisplay> {
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

template<typename... Ts> class WritePaginatedAction : public Action<Ts...>, public Parented<SplitFlapDisplay> {
 public:
  TEMPLATABLE_VALUE(std::string, value)
  TEMPLATABLE_VALUE(uint32_t, page_time)
  TEMPLATABLE_VALUE(float, speed)
  TEMPLATABLE_VALUE(bool, centering)

  void play(Ts... x) override {
    auto val = this->value_.value(x...);
    int32_t page_tm = this->page_time_.has_value() ? (int32_t) this->page_time_.value(x...) : -1;
    float spd = this->speed_.has_value() ? this->speed_.value(x...) : -1.0f;
    bool cent = this->centering_.has_value() ? this->centering_.value(x...) : true;
    this->parent_->write_paginated(val, page_tm, spd, cent);
  }
};

template<typename... Ts> class HomeAction : public Action<Ts...>, public Parented<SplitFlapDisplay> {
 public:
  TEMPLATABLE_VALUE(float, speed)

  void play(Ts... x) override {
    float spd = this->speed_.has_value() ? this->speed_.value(x...) : -1.0f;
    this->parent_->home(spd);
  }
};

template<typename... Ts> class HomeToStringAction : public Action<Ts...>, public Parented<SplitFlapDisplay> {
 public:
  TEMPLATABLE_VALUE(std::string, value)
  TEMPLATABLE_VALUE(float, speed)

  void play(Ts... x) override {
    auto val = this->value_.value(x...);
    float spd = this->speed_.has_value() ? this->speed_.value(x...) : -1.0f;
    this->parent_->home_to_string(val, spd);
  }
};

template<typename... Ts> class Step9TestAction : public Action<Ts...>, public Parented<SplitFlapDisplay> {
 public:
  void play(Ts... x) override { this->parent_->step_9_test(); }
};

}  // namespace split_flap
}  // namespace esphome
