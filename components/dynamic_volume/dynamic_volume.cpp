#include "dynamic_volume.h"

#ifdef USE_ESP32

#include "esphome/core/log.h"

#include <cmath>
#include <cstring>

namespace esphome {
namespace dynamic_volume {

static const char *const TAG = "dynamic_volume";

static constexpr float INT32_FS  = 2147483648.0f;
static constexpr float DBFS_FLOOR = -90.0f;

void DynamicVolumeComponent::setup() {
  if (this->microphone_ == nullptr) {
    ESP_LOGE(TAG, "No microphone configured");
    this->mark_failed();
    return;
  }
  if (this->media_player_ == nullptr) {
    ESP_LOGE(TAG, "No media player configured");
    this->mark_failed();
    return;
  }

  // Register audio callback — fires from I2S mic FreeRTOS task (priority 23)
  // whenever the mic is running (kept alive by micro_wake_word).
  this->microphone_->add_data_callback([this](const std::vector<uint8_t> &data) {
    this->handle_audio_(data);
  });

  ESP_LOGCONFIG(TAG, "Dynamic Volume ready");
}

void DynamicVolumeComponent::loop() {
  // Detect rising edge: media player enters ANNOUNCING state (TTS starts)
  bool now_announcing = (this->media_player_->state == media_player::MEDIA_PLAYER_STATE_ANNOUNCING);

  if (now_announcing && !this->was_announcing_) {
    this->apply_volume_();
  }
  this->was_announcing_ = now_announcing;
}

void DynamicVolumeComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Dynamic Volume:");
  ESP_LOGCONFIG(TAG, "  Smoothing factor: %.3f", this->smoothing_alpha_);
}

void DynamicVolumeComponent::handle_audio_(const std::vector<uint8_t> &data) {
  // Skip if disabled
  if (!this->enabled_.load(std::memory_order_relaxed))
    return;

  // Skip while TTS is playing — avoids measuring speaker output reflected back
  // into the mic. Reading state from the I2S task is safe: state is a single
  // uint8_t (aligned), a stale read only skips one audio callback.
  if (this->media_player_->state == media_player::MEDIA_PLAYER_STATE_ANNOUNCING)
    return;

  // Stereo 32-bit I2S: 8 bytes per frame, left channel first
  const size_t frame_bytes = 8;
  const size_t num_frames  = data.size() / frame_bytes;
  if (num_frames == 0)
    return;

  double sum_sq = 0.0;
  const uint8_t *ptr = data.data();
  for (size_t i = 0; i < num_frames; i++) {
    int32_t sample;
    std::memcpy(&sample, ptr + i * frame_bytes, 4);
    float n = static_cast<float>(sample) / INT32_FS;
    sum_sq += static_cast<double>(n) * static_cast<double>(n);
  }

  float rms  = static_cast<float>(std::sqrt(sum_sq / static_cast<double>(num_frames)));
  float dbfs = (rms > 1e-10f) ? (20.0f * std::log10f(rms)) : DBFS_FLOOR;
  if (dbfs < DBFS_FLOOR)
    dbfs = DBFS_FLOOR;

  float current = this->smoothed_dbfs_.load(std::memory_order_relaxed);
  float updated = current + this->smoothing_alpha_ * (dbfs - current);
  this->smoothed_dbfs_.store(updated, std::memory_order_relaxed);
}

float DynamicVolumeComponent::get_target_volume_() const {
  float db       = this->smoothed_dbfs_.load(std::memory_order_relaxed);
  float quiet_db = (this->quiet_db_number_   != nullptr) ? this->quiet_db_number_->state   : -50.0f;
  float loud_db  = (this->loud_db_number_    != nullptr) ? this->loud_db_number_->state    : -25.0f;
  float min_vol  = (this->min_volume_number_ != nullptr) ? this->min_volume_number_->state : 0.4f;
  float max_vol  = (this->max_volume_number_ != nullptr) ? this->max_volume_number_->state : 0.85f;

  if (loud_db <= quiet_db)
    return min_vol;

  float ratio = (db - quiet_db) / (loud_db - quiet_db);
  if (ratio < 0.0f) ratio = 0.0f;
  if (ratio > 1.0f) ratio = 1.0f;

  return min_vol + ratio * (max_vol - min_vol);
}

void DynamicVolumeComponent::apply_volume_() {
  if (!this->enabled_.load(std::memory_order_relaxed))
    return;

  float target = this->get_target_volume_();
  float db     = this->smoothed_dbfs_.load(std::memory_order_relaxed);

  ESP_LOGI(TAG, "TTS volume → %.2f  (ambient %.1f dBFS)", target, db);

  this->media_player_->make_call().set_volume(target).perform();
}

}  // namespace dynamic_volume
}  // namespace esphome

#endif  // USE_ESP32
