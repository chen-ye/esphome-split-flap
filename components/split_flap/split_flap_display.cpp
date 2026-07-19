#include "split_flap_display.h"
#include "esphome/core/log.h"
#include <algorithm>
#include <sstream>
namespace esphome {
namespace split_flap {

static const char *const TAG = "split_flap.display";

SplitFlapDisplay::~SplitFlapDisplay() {
  for (auto *module : this->modules_) {
    delete module;
  }
}

void SplitFlapDisplay::set_startup_string(const std::string &startup_string) {
  this->startup_string_ = startup_string;
  this->startup_lines_.clear();
  std::stringstream ss(startup_string);
  std::string line;
  while (std::getline(ss, line, '\n')) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    this->startup_lines_.push_back(line);
  }
}

void SplitFlapDisplay::setup() {
  ESP_LOGI(TAG, "Initializing Split Flap Display with %d modules...", (int) this->modules_.size());
  for (auto *module : this->modules_) {
    module->set_i2c_bus(this->bus_);
    module->init();
    module->update_cached_offset();
  }

  this->current_displayed_text_ = std::string(this->modules_.size(), ' ');
  this->publish_state(this->current_displayed_text_);

  // Create High-Priority Stepping Task (Priority 24) on Core 0
  xTaskCreate(SplitFlapDisplay::step_task_fn, "SplitFlapStep", 4096, this, 24, &this->step_task_handle_);

  this->state_timer_ = millis();
  if (this->home_on_startup_) {
    this->home(-1.0f, true);
  }
}

void SplitFlapDisplay::add_module(uint8_t address, int offset) {
  auto *module = new SplitFlapModule(address, this->steps_per_rot_, offset, this->magnet_position_, this->charset_);
  this->modules_.push_back(module);
}

void SplitFlapDisplay::add_module(uint8_t address, number::Number *offset_number) {
  auto *module = new SplitFlapModule(address, this->steps_per_rot_, offset_number, this->magnet_position_, this->charset_);
  this->modules_.push_back(module);
}

void SplitFlapDisplay::dump_config() {
  ESP_LOGCONFIG(TAG, "Split Flap Display:");
  ESP_LOGCONFIG(TAG, "  Steps per Rotation: %d", this->steps_per_rot_);
  ESP_LOGCONFIG(TAG, "  Magnet Position: %d", this->magnet_position_);
  ESP_LOGCONFIG(TAG, "  Display Offset: %d", this->display_offset_);
  ESP_LOGCONFIG(TAG, "  Max Velocity: %.1f RPM", this->max_vel_);
  ESP_LOGCONFIG(TAG, "  Home on Startup: %s", this->home_on_startup_ ? "true" : "false");
  ESP_LOGCONFIG(TAG, "  Modules Count: %d", (int) this->modules_.size());
  for (size_t i = 0; i < this->modules_.size(); ++i) {
    ESP_LOGCONFIG(TAG, "    Module %d: Address 0x%02X", (int) i, this->modules_[i]->get_address());
  }
}

void SplitFlapDisplay::control(const std::string &value) {
  this->write_string(value);
}

void SplitFlapDisplay::write_string(const std::string &input_string, float speed, bool centering) {
  if (speed < 0) {
    speed = this->max_vel_;
  }
  // Clamp speed to [2.0, max_vel_]
  float clamped_speed = speed < 2.0f ? 2.0f : (speed > this->max_vel_ ? this->max_vel_ : speed);
  this->current_speed_ = clamped_speed;

  float steps_per_second = (clamped_speed / 60.0f) * this->steps_per_rot_;
  this->time_per_step_us_ = (unsigned long) (1000000.0f / steps_per_second);

  // Convert input to uppercase for case-insensitivity
  std::string upper_input = input_string;
  for (char &c : upper_input) {
    c = std::toupper(c);
  }

  std::string display_string = upper_input.substr(0, this->modules_.size());
  if (centering) {
    int total_padding = this->modules_.size() - display_string.length();
    int padding_left = total_padding / 2;
    int padding_right = total_padding - padding_left;
    display_string = std::string(padding_left, ' ') + display_string + std::string(padding_right, ' ');
  } else {
    while (display_string.length() < this->modules_.size()) {
      display_string += ' ';
    }
  }

  this->target_positions_.resize(this->modules_.size());
  this->needs_stepping_.resize(this->modules_.size());
  this->last_step_times_.resize(this->modules_.size());
  this->calibrated_this_move_.resize(this->modules_.size());
  this->was_magnet_present_.resize(this->modules_.size());
  this->calibration_events_.clear();
  this->calibration_events_.resize(this->modules_.size());

  unsigned long current_time = micros();
  bool any_needs_stepping = false;

  for (size_t i = 0; i < this->modules_.size(); i++) {
    this->modules_[i]->update_cached_offset();
    this->modules_[i]->reset_state();

    char current_char = display_string[i];
    int char_pos = this->modules_[i]->get_char_position(current_char);
    this->target_positions_[i] = (char_pos - this->modules_[i]->get_step_offset() + this->steps_per_rot_) % this->steps_per_rot_;
    this->calibrated_this_move_[i] = false;
    this->was_magnet_present_[i] = false;
    this->calibration_events_[i].triggered = false;
    this->last_step_times_[i] = current_time;

    if (this->modules_[i]->get_position() != this->target_positions_[i]) {
      this->needs_stepping_[i] = true;
      any_needs_stepping = true;
    } else {
      this->needs_stepping_[i] = false;
    }
  }

  this->current_displayed_text_ = display_string;

  if (any_needs_stepping) {
    this->release_motors_ = true;
    ESP_LOGD(TAG, "Command received. Entering network cooldown for 250ms...");
    this->state_ = STATE_NETWORK_COOLDOWN;
    this->state_timer_ = millis();
  } else {
    this->publish_state(this->current_displayed_text_);
  }
}

void SplitFlapDisplay::home(float speed, bool use_startup_string) {
  if (speed < 0) {
    speed = this->max_vel_;
  }
  float clamped_speed = speed < 2.0f ? 2.0f : (speed > this->max_vel_ ? this->max_vel_ : speed);
  this->current_speed_ = clamped_speed;

  float steps_per_second = (clamped_speed / 60.0f) * this->steps_per_rot_;
  this->time_per_step_us_ = (unsigned long) (1000000.0f / steps_per_second);

  // Log module offsets when starting homing
  std::string offsets_msg = "Starting Homing. Module Offsets: ";
  for (size_t i = 0; i < this->modules_.size(); i++) {
    this->modules_[i]->update_cached_offset();
    offsets_msg += std::to_string(i) + ":" + std::to_string(this->modules_[i]->get_step_offset()) + (i == this->modules_.size() - 1 ? "" : ", ");
  }
  ESP_LOGI(TAG, "%s", offsets_msg.c_str());

  this->target_positions_.resize(this->modules_.size());
  this->needs_stepping_.resize(this->modules_.size());
  this->last_step_times_.resize(this->modules_.size());
  this->calibrated_this_move_.resize(this->modules_.size());
  this->was_magnet_present_.resize(this->modules_.size());
  this->calibration_events_.clear();
  this->calibration_events_.resize(this->modules_.size());

  unsigned long current_time = micros();
  for (size_t i = 0; i < this->modules_.size(); i++) {
    this->modules_[i]->reset_state();
    this->target_positions_[i] = (this->modules_[i]->get_position() - 1 + this->steps_per_rot_) % this->steps_per_rot_;
    this->calibrated_this_move_[i] = false;
    this->was_magnet_present_[i] = false;
    this->calibration_events_[i].triggered = false;
    this->last_step_times_[i] = current_time;
    this->needs_stepping_[i] = true; // force all modules to run calibration rotation
  }

  this->release_motors_ = false; // keep coils energized between stage 1 and stage 2
  this->homing_stage_2_pending_ = true;
  
  if (use_startup_string && !this->startup_lines_.empty()) {
    this->startup_line_idx_ = 0;
    std::string first_line = this->startup_lines_[this->startup_line_idx_++];
    for (char &c : first_line) {
      c = std::toupper(c);
    }
    if (first_line.length() > this->modules_.size()) {
      first_line = first_line.substr(0, this->modules_.size());
    }
    int total_padding = this->modules_.size() - first_line.length();
    int padding_left = total_padding / 2;
    int padding_right = total_padding - padding_left;
    this->pending_string_ = std::string(padding_left, ' ') + first_line + std::string(padding_right, ' ');
  } else {
    this->pending_string_ = std::string(this->modules_.size(), ' ');
  }
  this->current_displayed_text_ = this->pending_string_;

  ESP_LOGD(TAG, "Home command received. Entering network cooldown for 250ms...");
  this->state_ = STATE_NETWORK_COOLDOWN;
  this->state_timer_ = millis();
}

void SplitFlapDisplay::home_to_string(const std::string &home_string, float speed) {
  if (speed < 0) {
    speed = this->max_vel_;
  }
  float clamped_speed = speed < 2.0f ? 2.0f : (speed > this->max_vel_ ? this->max_vel_ : speed);
  this->current_speed_ = clamped_speed;

  float steps_per_second = (clamped_speed / 60.0f) * this->steps_per_rot_;
  this->time_per_step_us_ = (unsigned long) (1000000.0f / steps_per_second);

  this->target_positions_.resize(this->modules_.size());
  this->needs_stepping_.resize(this->modules_.size());
  this->last_step_times_.resize(this->modules_.size());
  this->calibrated_this_move_.resize(this->modules_.size());
  this->was_magnet_present_.resize(this->modules_.size());
  this->calibration_events_.clear();
  this->calibration_events_.resize(this->modules_.size());

  unsigned long current_time = micros();
  for (size_t i = 0; i < this->modules_.size(); i++) {
    this->modules_[i]->update_cached_offset();
    this->modules_[i]->reset_state();
    this->target_positions_[i] = (this->modules_[i]->get_position() - 1 + this->steps_per_rot_) % this->steps_per_rot_;
    this->calibrated_this_move_[i] = false;
    this->was_magnet_present_[i] = false;
    this->calibration_events_[i].triggered = false;
    this->last_step_times_[i] = current_time;
    this->needs_stepping_[i] = true;
  }

  this->release_motors_ = false;
  this->homing_stage_2_pending_ = true;

  // Convert home_string to uppercase for case-insensitivity
  std::string upper_home = home_string;
  for (char &c : upper_home) {
    c = std::toupper(c);
  }

  this->pending_string_ = upper_home.substr(0, this->modules_.size());
  while (this->pending_string_.length() < this->modules_.size()) {
    this->pending_string_ += ' ';
  }
  this->current_displayed_text_ = this->pending_string_;

  ESP_LOGD(TAG, "HomeToString command received. Entering network cooldown for 250ms...");
  this->state_ = STATE_NETWORK_COOLDOWN;
  this->state_timer_ = millis();
}

void SplitFlapDisplay::start_motors() {
  for (auto *module : this->modules_) {
    module->start();
  }
}

void SplitFlapDisplay::stop_motors() {
  for (auto *module : this->modules_) {
    module->stop();
  }
}

void SplitFlapDisplay::start_movement() {
  ESP_LOGD(TAG, "Starting movement...");
  this->hf_requester_.start();
  this->state_ = STATE_START_STEPS;
  this->state_timer_ = millis();
  this->start_motors();
}

void __attribute__((hot)) SplitFlapDisplay::loop() {
  unsigned long now_ms = millis();
  unsigned long now_us = micros();

  switch (this->state_) {
    case STATE_IDLE:
      if (this->startup_line_idx_ < this->startup_lines_.size()) {
        if (now_ms - this->state_timer_ >= 2000) {
          ESP_LOGD(TAG, "Displaying startup sequence line %d: %s", (int) this->startup_line_idx_, this->startup_lines_[this->startup_line_idx_].c_str());
          this->write_string(this->startup_lines_[this->startup_line_idx_++], -1.0f, true);
        }
      }
      break;

    case STATE_NETWORK_COOLDOWN:
      // Allow 250ms for network traffic and encryption processing to settle down
      if (now_ms - this->state_timer_ >= 250) {
        this->start_movement();
      }
      break;

    case STATE_START_STEPS:
      // Allow motors 200ms settling delay to align to magnetic field
      if (now_ms - this->state_timer_ >= 200) {
        ESP_LOGD(TAG, "Transitioning to STATE_STEPPING");
        this->state_ = STATE_STEPPING;
        this->last_sensor_check_time_ = now_us;
        for (size_t i = 0; i < this->modules_.size(); i++) {
          this->last_step_times_[i] = now_us;
        }
      }
      break;
    case STATE_STEPPING:
      break;

    case STATE_SETTLE:
      // Allow motors 200ms to settle to final positions
      if (now_ms - this->state_timer_ >= 200) {
        this->state_ = STATE_STOPPING;
      }
      break;

    case STATE_STOPPING:
      if (this->release_motors_) {
        this->stop_motors();
      }
      this->state_ = STATE_IDLE;
      this->state_timer_ = millis();
      this->hf_requester_.stop();
      this->publish_state(this->current_displayed_text_);
      break;
  }
}

void __attribute__((hot)) SplitFlapDisplay::step_task_fn(void *param) {
  SplitFlapDisplay *this_display = (SplitFlapDisplay *) param;
  while (true) {
    if (this_display->state_ == STATE_STEPPING) {
      unsigned long step_start_us = micros();
      bool all_finished = true;

      // 1. Process steps for any modules that need it
      for (size_t i = 0; i < this_display->modules_.size(); i++) {
        if (this_display->needs_stepping_[i]) {
          all_finished = false;

          // Perform the step
          this_display->modules_[i]->step();

          // Read the sensor
          if (!this_display->calibrated_this_move_[i]) {
            bool is_magnet_present = this_display->modules_[i]->read_hall_effect_sensor();
            if (is_magnet_present) {
              this_display->was_magnet_present_[i] = true;
            } else if (this_display->was_magnet_present_[i]) {
              // Record trailing edge calibration event asynchronously
              this_display->calibration_events_[i].module_index = i;
              this_display->calibration_events_[i].current_pos = this_display->modules_[i]->get_position();
              this_display->calibration_events_[i].reset_pos = this_display->modules_[i]->get_magnet_position();
              this_display->calibration_events_[i].target_pos = this_display->target_positions_[i];
              this_display->calibration_events_[i].triggered = true;

              this_display->modules_[i]->magnet_detected();
              this_display->calibrated_this_move_[i] = true;
              this_display->was_magnet_present_[i] = false;
            }
          }

          // Check if destination reached
          if (this_display->modules_[i]->get_position() == this_display->target_positions_[i]) {
            this_display->needs_stepping_[i] = false;
          }
        }
      }

      // 2. Handle stage completion
      if (all_finished) {
        // Cold-path: Log calibration events
        for (const auto &event : this_display->calibration_events_) {
          if (event.triggered) {
            ESP_LOGD(TAG, "Module %zu calibrated at magnet trailing edge (current pos: %d -> reset to %d, target: %d)", 
                     event.module_index, event.current_pos, event.reset_pos, event.target_pos);
          }
        }

        if (this_display->homing_stage_2_pending_) {
          this_display->homing_stage_2_pending_ = false;

          // Debug log magnet detection status
          std::string magnet_status = "Magnets Detected: ";
          for (size_t i = 0; i < this_display->modules_.size(); i++) {
            if (this_display->modules_[i]->get_has_magnet_detected()) {
              magnet_status += std::to_string(i) + ", ";
            }
          }
          ESP_LOGD(TAG, "%s", magnet_status.c_str());

          // Start Stage 2 of Homing
          this_display->target_positions_.resize(this_display->modules_.size());
          this_display->needs_stepping_.resize(this_display->modules_.size());
          this_display->last_step_times_.resize(this_display->modules_.size());
          this_display->calibrated_this_move_.resize(this_display->modules_.size());
          this_display->was_magnet_present_.resize(this_display->modules_.size());
          this_display->calibration_events_.clear();
          this_display->calibration_events_.resize(this_display->modules_.size());

          unsigned long current_time = micros();
          for (size_t i = 0; i < this_display->modules_.size(); i++) {
            char current_char = this_display->pending_string_[i];
            int char_pos = this_display->modules_[i]->get_char_position(current_char);
            this_display->target_positions_[i] = (char_pos - this_display->modules_[i]->get_step_offset() + this_display->steps_per_rot_) % this_display->steps_per_rot_;
            this_display->calibrated_this_move_[i] = false;
            this_display->was_magnet_present_[i] = false;
            this_display->calibration_events_[i].triggered = false;
            this_display->last_step_times_[i] = current_time;

            if (this_display->modules_[i]->get_position() != this_display->target_positions_[i]) {
              this_display->needs_stepping_[i] = true;
            } else {
              this_display->needs_stepping_[i] = false;
            }
          }
          this_display->release_motors_ = true;
          this_display->state_ = STATE_START_STEPS;
          this_display->state_timer_ = millis();
        } else {
          this_display->state_ = STATE_SETTLE;
          this_display->state_timer_ = millis();
        }
      } else {
        // 3. Precise wait for next step using Hybrid-Yield
        unsigned long next_step_time = step_start_us + this_display->time_per_step_us_;
        while (micros() < next_step_time) {
          if (next_step_time - micros() >= 1500) {
            vTaskDelay(pdMS_TO_TICKS(1)); // Yield 1 tick to let WiFi run
          } else {
            // tight loop for remaining timing fraction
          }
        }
      }
    } else {
      // Idle or other states: Sleep 10ms to save CPU
      vTaskDelay(pdMS_TO_TICKS(10));
    }
  }
}

void SplitFlapDisplay::step_9_test() {
  if (this->charset_.empty()) return;
  char c = this->charset_[this->test_step_index_];
  std::string test_str(this->modules_.size(), c);
  this->write_string(test_str, -1.0f, false);
  this->test_step_index_ = (this->test_step_index_ + 9) % this->charset_.size();
}

}  // namespace split_flap
}  // namespace esphome

