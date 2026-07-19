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

  std::string display_string = input_string.substr(0, this->modules_.size());
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
  this->reset_latches_.resize(this->modules_.size());

  unsigned long current_time = micros();
  bool any_needs_stepping = false;

  for (size_t i = 0; i < this->modules_.size(); i++) {
    char current_char = display_string[i];
    this->target_positions_[i] = this->modules_[i]->get_char_position(current_char);
    this->reset_latches_[i] = true;
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

  this->target_positions_.resize(this->modules_.size());
  this->needs_stepping_.resize(this->modules_.size());
  this->last_step_times_.resize(this->modules_.size());
  this->reset_latches_.resize(this->modules_.size());

  unsigned long current_time = micros();
  for (size_t i = 0; i < this->modules_.size(); i++) {
    this->target_positions_[i] = (this->modules_[i]->get_position() - 1 + this->steps_per_rot_) % this->steps_per_rot_;
    this->reset_latches_[i] = true;
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
  this->reset_latches_.resize(this->modules_.size());

  unsigned long current_time = micros();
  for (size_t i = 0; i < this->modules_.size(); i++) {
    this->target_positions_[i] = (this->modules_[i]->get_position() - 1 + this->steps_per_rot_) % this->steps_per_rot_;
    this->reset_latches_[i] = true;
    this->last_step_times_[i] = current_time;
    this->needs_stepping_[i] = true;
  }

  this->release_motors_ = false;
  this->homing_stage_2_pending_ = true;

  this->pending_string_ = home_string.substr(0, this->modules_.size());
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
          if (now_us - this->last_step_times_[i] >= this->time_per_step_us_) {
            this->modules_[i]->step();
            this->last_step_times_[i] = now_us;
            if (this->modules_[i]->get_position() == this->target_positions_[i]) {
              this->needs_stepping_[i] = false;
            }
          }
        }
      }

      // Check hall sensors on every loop call during stepping (1ms debounce guard).
      // The magnet window at 15 RPM is only ~6ms (3 steps × ~1953µs/step).
      // A 20ms poll interval was far too slow and reliably missed the magnet.
      if (now_us - this->last_sensor_check_time_ >= 1000) {
        for (size_t i = 0; i < this->modules_.size(); i++) {
          if (this->needs_stepping_[i]) {
            bool sensor_val = this->modules_[i]->read_hall_effect_sensor(); // true if NO magnet, false if magnet
            if (sensor_val) {
              if (!this->reset_latches_[i]) {
                this->modules_[i]->magnet_detected(); // Recalibrate spools to magnet position
                this->reset_latches_[i] = true;
              }
            } else {
              // Sensor went low (magnet detected) - arm edge trigger
              this->reset_latches_[i] = false;
            }
          }
        }
        this->last_sensor_check_time_ = now_us;
      }

      if (all_finished) {
        if (this->homing_stage_2_pending_) {
          this->homing_stage_2_pending_ = false;
          
          // Start Stage 2 of Homing
          this->target_positions_.resize(this->modules_.size());
          this->needs_stepping_.resize(this->modules_.size());
          this->last_step_times_.resize(this->modules_.size());
          this->reset_latches_.resize(this->modules_.size());

          unsigned long current_time = micros();
          for (size_t i = 0; i < this->modules_.size(); i++) {
            char current_char = this->pending_string_[i];
            this->target_positions_[i] = this->modules_[i]->get_char_position(current_char);
            this->reset_latches_[i] = true;
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
