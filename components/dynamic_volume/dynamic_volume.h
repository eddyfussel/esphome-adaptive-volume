#pragma once

#ifdef USE_ESP32

#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/components/microphone/microphone.h"
#include "esphome/components/media_player/media_player.h"
#include "esphome/components/number/number.h"

#include <atomic>
#include <cmath>
#include <cstring>
#include <vector>

namespace esphome {
namespace dynamic_volume {

class DynamicVolumeComponent : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;

  float get_ambient_db() const { return this->smoothed_dbfs_.load(std::memory_order_relaxed); }

  void set_microphone(microphone::Microphone *mic) { this->microphone_ = mic; }
  void set_media_player(media_player::MediaPlayer *player) { this->media_player_ = player; }
  void set_smoothing_factor(float alpha) { this->smoothing_alpha_ = alpha; }

  // Called from switch on_turn_on / on_turn_off (main loop task)
  void set_enabled(bool enabled) { this->enabled_.store(enabled, std::memory_order_relaxed); }

  // Wired via on_boot lambda — number states are read directly when needed
  void set_min_volume_number(number::Number *n) { this->min_volume_number_ = n; }
  void set_max_volume_number(number::Number *n) { this->max_volume_number_ = n; }
  void set_quiet_db_number(number::Number *n) { this->quiet_db_number_ = n; }
  void set_loud_db_number(number::Number *n) { this->loud_db_number_ = n; }

 protected:
  void handle_audio_(const std::vector<uint8_t> &data);
  float get_target_volume_() const;
  void apply_volume_();

  microphone::Microphone *microphone_{nullptr};
  media_player::MediaPlayer *media_player_{nullptr};
  float smoothing_alpha_{0.05f};

  // Rising-edge detection: fires apply_volume_() when announcing starts
  bool was_announcing_{false};

  // Written by main loop (switch), read by I2S task — must be atomic
  std::atomic<bool> enabled_{true};
  // Written by I2S task, read by main loop — must be atomic
  std::atomic<float> smoothed_dbfs_{-60.0f};

  // Only read on main loop — plain pointers are safe
  number::Number *min_volume_number_{nullptr};
  number::Number *max_volume_number_{nullptr};
  number::Number *quiet_db_number_{nullptr};
  number::Number *loud_db_number_{nullptr};
};

}  // namespace dynamic_volume
}  // namespace esphome

#endif  // USE_ESP32
