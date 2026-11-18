#include "dynamic_volume.h"

#ifdef USE_ESP32

#include "esphome/core/log.h"

#include <cmath>
#include <cstring>

namespace esphome {
namespace dynamic_volume {

static const char *const TAG = "dynamic_volume";

static constexpr float INT16_FS  = 32768.0f;
static constexpr float INT32_FS  = 2147483648.0f;
static constexpr float DBFS_FLOOR = -90.0f;
static constexpr uint8_t MAX_CHANNELS = 8;

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
#if ESPHOME_VERSION_CODE >= VERSION_CODE(2026, 11, 0)
  // 2026.11+ hands out the buffer as a view plus its format; the std::vector
  // callback still works but copies every buffer and is removed in 2027.5.0.
  this->microphone_->add_data_callback([this](const microphone::AudioCapture &capture) {
    this->handle_audio_(capture.data.data(), capture.data.size(), capture.stream_info);
  });
#else
  this->microphone_->add_data_callback([this](const std::vector<uint8_t> &data) {
    this->handle_audio_(data.data(), data.size(), this->microphone_->get_audio_stream_info());
  });
#endif

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
  if (this->microphone_ != nullptr) {
    audio::AudioStreamInfo info = this->microphone_->get_audio_stream_info();
    ESP_LOGCONFIG(TAG, "  Microphone: %u-bit, %u channel(s), %u Hz", static_cast<unsigned>(info.get_bits_per_sample()),
                  static_cast<unsigned>(info.get_channels()), static_cast<unsigned>(info.get_sample_rate()));
  }
}

void DynamicVolumeComponent::handle_audio_(const uint8_t *data, size_t len, const audio::AudioStreamInfo &info) {
  // Skip if disabled
  if (!this->enabled_.load(std::memory_order_relaxed))
    return;

  // Skip while TTS is playing — avoids measuring speaker output reflected back
  // into the mic. Reading state from the I2S task is safe: state is a single
  // uint8_t (aligned), a stale read only skips one audio callback.
  if (this->media_player_->state == media_player::MEDIA_PLAYER_STATE_ANNOUNCING)
    return;

  // A muted microphone delivers zeros. Measuring them would drag the ambient
  // level to the floor and play the next TTS at min volume, so keep the last value.
  if (this->microphone_->get_mute_state())
    return;

  const uint8_t bits     = info.get_bits_per_sample();
  const uint8_t channels = info.get_channels();
  if ((bits != 16 && bits != 32) || channels == 0 || channels > MAX_CHANNELS)
    return;

  const size_t sample_bytes = bits / 8;
  const size_t frame_bytes  = sample_bytes * channels;
  const size_t num_frames   = len / frame_bytes;
  if (num_frames == 0)
    return;

  double sum_sq[MAX_CHANNELS] = {};
  for (size_t i = 0; i < num_frames; i++) {
    const uint8_t *frame = data + i * frame_bytes;
    for (uint8_t ch = 0; ch < channels; ch++) {
      float n;
      if (bits == 32) {
        int32_t sample;
        std::memcpy(&sample, frame + ch * sample_bytes, sizeof(sample));
        n = static_cast<float>(sample) / INT32_FS;
      } else {
        int16_t sample;
        std::memcpy(&sample, frame + ch * sample_bytes, sizeof(sample));
        n = static_cast<float>(sample) / INT16_FS;
      }
      sum_sq[ch] += static_cast<double>(n) * static_cast<double>(n);
    }
  }

  // Measure the first channel that carries signal. A live mic never returns
  // exact zeros for a whole buffer, so an all-zero channel is muted: Satellite1
  // v0.2.2+ zeroes channel 0 during software mute and keeps channel 1 live for
  // wake word. If every channel is silent, keep the last value.
  int channel = -1;
  for (uint8_t ch = 0; ch < channels; ch++) {
    if (sum_sq[ch] > 0.0) {
      channel = ch;
      break;
    }
  }
  if (channel < 0)
    return;

  float rms  = static_cast<float>(std::sqrt(sum_sq[channel] / static_cast<double>(num_frames)));
  float dbfs = (rms > 1e-10f) ? (20.0f * std::log10f(rms)) : DBFS_FLOOR;
  if (dbfs < DBFS_FLOOR)
    dbfs = DBFS_FLOOR;

  float current = this->smoothed_dbfs_.load(std::memory_order_relaxed);
  float updated = current + this->smoothing_alpha_ * (dbfs - current);
  this->smoothed_dbfs_.store(updated, std::memory_order_relaxed);
}

VolumeCalc DynamicVolumeComponent::compute_volume_() const {
  VolumeCalc calc{};
  calc.db       = this->smoothed_dbfs_.load(std::memory_order_relaxed);
  calc.quiet_db = (this->quiet_db_number_   != nullptr) ? this->quiet_db_number_->state   : -55.0f;
  calc.loud_db  = (this->loud_db_number_    != nullptr) ? this->loud_db_number_->state    : -38.0f;
  float min_vol  = (this->min_volume_number_ != nullptr) ? this->min_volume_number_->state : 0.4f;
  float max_vol  = (this->max_volume_number_ != nullptr) ? this->max_volume_number_->state : 0.85f;
  float exponent = (this->curve_exponent_number_ != nullptr) ? this->curve_exponent_number_->state : 1.0f;
  if (exponent <= 0.0f)
    exponent = 1.0f;

  if (calc.loud_db <= calc.quiet_db) {
    calc.ratio  = 0.0f;
    calc.target = min_vol;
    return calc;
  }

  float ratio = (calc.db - calc.quiet_db) / (calc.loud_db - calc.quiet_db);
  if (ratio < 0.0f) ratio = 0.0f;
  if (ratio > 1.0f) ratio = 1.0f;
  if (exponent != 1.0f)
    ratio = std::pow(ratio, exponent);

  calc.ratio  = ratio;
  calc.target = min_vol + ratio * (max_vol - min_vol);
  return calc;
}

void DynamicVolumeComponent::apply_volume_() {
  if (!this->enabled_.load(std::memory_order_relaxed))
    return;

  VolumeCalc calc = this->compute_volume_();

  ESP_LOGI(TAG, "TTS volume → %.2f  (ambient %.1f dBFS, quiet=%.0f loud=%.0f, ratio=%.2f)",
           calc.target, calc.db, calc.quiet_db, calc.loud_db, calc.ratio);

  this->media_player_->make_call().set_volume(calc.target).perform();
}

}  // namespace dynamic_volume
}  // namespace esphome

#endif  // USE_ESP32
