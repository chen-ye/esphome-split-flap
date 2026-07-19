#include "split_flap_display.h"
#include "esphome/core/log.h"
#include <algorithm>

namespace esphome {
namespace split_flap {

static const char *const TAG = "split_flap.display";

SplitFlapDisplay::~SplitFlapDisplay() {
  for (auto *module : this->modules_) {
    delete module;
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

  if (this->home_on_startup_) {
    this->home();
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
    this->target_positions_[i] = this->modules_[i]->get_char_position(current_char);
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
    this->start_movement();
  } else {
    this->publish_state(this->current_displayed_text_);
  }
}

void SplitFlapDisplay::home(float speed) {
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
  this->pending_string_ = std::string(this->modules_.size(), ' ');
  this->current_displayed_text_ = std::string(this->modules_.size(), ' ');

  this->start_movement();
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

  this->start_movement();
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

void SplitFlapDisplay::loop() {
  unsigned long now_ms = millis();
  unsigned long now_us = micros();

  switch (this->state_) {
    case STATE_IDLE:
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
    case STATE_STEPPING: {
      bool all_finished = true;
      for (size_t i = 0; i < this->modules_.size(); i++) {
        if (this->needs_stepping_[i]) {
          all_finished = false;
          unsigned long delay_since_last_step = now_us - this->last_step_times_[i];

          // Track maximum delay between steps (jitter metric)
          if (delay_since_last_step > this->max_step_delay_us_) {
            this->max_step_delay_us_ = delay_since_last_step;
          }

          if (delay_since_last_step >= this->time_per_step_us_) {
            this->modules_[i]->step();
            this->last_step_times_[i] = now_us;

            // Only read sensor if not already calibrated during this movement
            if (!this->calibrated_this_move_[i]) {
              bool is_magnet_present = this->modules_[i]->read_hall_effect_sensor();
              if (is_magnet_present) {
                this->was_magnet_present_[i] = true;
              } else if (this->was_magnet_present_[i]) {
                // Trailing Edge: Magnet was present, but now it is not!
                // Record calibration details asynchronously (to be printed in cold path)
                this->calibration_events_[i].module_index = i;
                this->calibration_events_[i].current_pos = this->modules_[i]->get_position();
                this->calibration_events_[i].reset_pos = this->modules_[i]->get_magnet_position();
                this->calibration_events_[i].target_pos = this->target_positions_[i];
                this->calibration_events_[i].triggered = true;

                this->modules_[i]->magnet_detected();
                this->calibrated_this_move_[i] = true;
                this->was_magnet_present_[i] = false;
              }
            }

            if (this->modules_[i]->get_position() == this->target_positions_[i]) {
              this->needs_stepping_[i] = false;
            }
          }
        }
      }

      if (all_finished) {
        // We are now in the cold path (all active stepping has finished for this stage).
        // Log all recorded calibration events at once to avoid timing jitter during high-frequency stepping.
        for (const auto &event : this->calibration_events_) {
          if (event.triggered) {
            ESP_LOGD(TAG, "Module %zu calibrated at magnet trailing edge (current pos: %d -> reset to %d, target: %d)", 
                     event.module_index, event.current_pos, event.reset_pos, event.target_pos);
          }
        }

        ESP_LOGD(TAG, "Movement finished. Max step delay was %lu us (Target: %lu us)", 
                 this->max_step_delay_us_, this->time_per_step_us_);
        this->max_step_delay_us_ = 0; // Reset for next movement

        if (this->homing_stage_2_pending_) {
          this->homing_stage_2_pending_ = false;
          
          // Debug log magnet detection status
          std::string magnet_status = "Magnets Detected: ";
          for (size_t i = 0; i < this->modules_.size(); i++) {
            if (this->modules_[i]->get_has_magnet_detected()) {
              magnet_status += std::to_string(i) + ", ";
            }
          }
          ESP_LOGD(TAG, "%s", magnet_status.c_str());

          // Start Stage 2 of Homing
          this->target_positions_.resize(this->modules_.size());
          this->needs_stepping_.resize(this->modules_.size());
          this->last_step_times_.resize(this->modules_.size());
          this->calibrated_this_move_.resize(this->modules_.size());
          this->was_magnet_present_.resize(this->modules_.size());
          this->calibration_events_.clear();
          this->calibration_events_.resize(this->modules_.size());

          unsigned long current_time = micros();
          for (size_t i = 0; i < this->modules_.size(); i++) {
            char current_char = this->pending_string_[i];
            this->target_positions_[i] = this->modules_[i]->get_char_position(current_char);
            this->calibrated_this_move_[i] = false;
            this->was_magnet_present_[i] = false;
            this->calibration_events_[i].triggered = false;
            this->last_step_times_[i] = current_time;

            if (this->modules_[i]->get_position() != this->target_positions_[i]) {
              this->needs_stepping_[i] = true;
            } else {
              this->needs_stepping_[i] = false;
            }
          }
          this->release_motors_ = true;
          this->state_ = STATE_START_STEPS;
          this->state_timer_ = millis();
        } else {
          this->state_ = STATE_SETTLE;
          this->state_timer_ = millis();
        }
      }
      break;
    }

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
      this->hf_requester_.stop();
      this->publish_state(this->current_displayed_text_);
      break;
  }
}

}  // namespace split_flap
}  // namespace esphome
