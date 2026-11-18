# CLAUDE.md — esphome-adaptive-volume

Context and guidelines for Claude Code when working in this repository.

## What this repo is

A single ESPHome custom component (`dynamic_volume`) packaged as a reusable ESPHome package. It is consumed by HA Voice PE devices via:

```yaml
packages:
  dynamic_volume: github://eddyfussel/esphome-adaptive-volume/dynamic-volume.yaml@main
```

The component measures ambient noise via the I2S mic and adjusts TTS playback volume before each announcement.

## Key files

| File | Role |
|---|---|
| `components/dynamic_volume/__init__.py` | ESPHome schema + `to_code()` wiring |
| `components/dynamic_volume/dynamic_volume.h` | C++ class declaration |
| `components/dynamic_volume/dynamic_volume.cpp` | Audio measurement + volume logic |
| `dynamic-volume.yaml` | ESPHome package (sensors, switch, numbers, on_boot) |
| `example.yaml` | Template for device configs using this package |

Device-specific YAMLs (e.g. `hav-pe-wohnzimmer.yaml`) are gitignored — they contain secrets and personal config. Edit them in the ESPHome add-on in Home Assistant.

## Architecture decisions

**Why `loop()` instead of `on_tts_start`**: The component detects the rising edge of `MEDIA_PLAYER_STATE_ANNOUNCING` in `loop()`. This avoids needing to extend the `voice_assistant` config in the package, which is fragile across firmware versions.

**Why atomics**: The I2S audio callback runs in a FreeRTOS task (priority 23). `smoothed_dbfs_` is written there and read in the main loop. `enabled_` goes the other way. All number/pointer state is only touched from the main loop.

**Why `path: components/dynamic_volume/..` in the packages**: ESPHome 2026.9+ resolves a local `external_components` path next to the device YAML first, then next to the package file (the cloned repo at the package ref). This pins the component to the package ref without a hard-coded `ref:`. Naming `dynamic_volume` in the path keeps an unrelated `components/` folder next to the device YAML from shadowing it. Do not shorten it to `components`.

**Why number entities instead of compile-time constants**: Thresholds and volume limits are exposed as HA number entities so they can be tuned without re-flashing.

## How to test changes

```shell
# Compile only (fast, no device needed)
esphome compile hav-pe-<device>.yaml

# Compile + flash (device connected via USB)
esphome run hav-pe-<device>.yaml

# Watch logs after flashing
esphome logs hav-pe-<device>.yaml
```

After flashing, verify in ESPHome logs:
- `dynamic_volume: Dynamic Volume:` appears on boot
- `TTS volume → X.XX  (ambient Y.Y dBFS)` appears before each TTS response

## Constraints

- **ESP32 only**: The entire component is wrapped in `#ifdef USE_ESP32`. Do not remove this guard.
- **Mic format**: `handle_audio_()` reads the format from the stream info (16 or 32 bit, up to 8 channels) and measures the first channel that is not all zeros. Both HA Voice PE (`i2s_mics`) and Satellite1 (`sat1_mics`) deliver 32-bit stereo.
- **ESPHome version guard**: The mic callback takes `microphone::AudioCapture` on ESPHome ≥ 2026.11 and `std::vector<uint8_t>` before that (`ESPHOME_VERSION_CODE`). Drop the old branch once 2026.10 support ends; the vector callback is removed in 2027.5.0.
- **No mocks in tests**: ESPHome components cannot be unit-tested in isolation without the full framework — always test via `esphome compile`.
- **ESPHome version**: Developed against ESPHome 2026.x. The `media_player::MEDIA_PLAYER_STATE_ANNOUNCING` enum value must exist; verify after major ESPHome updates.

## Common tasks

**Add a new tuning parameter**: Add a `number:` entity in `dynamic-volume.yaml`, a setter in the header, read it in `get_target_volume_()`, and wire it up in the `on_boot` lambda.

**Change the measurement algorithm**: Edit `handle_audio_()` in `dynamic_volume.cpp`. Keep the function fast — it runs in the I2S task on every audio buffer (~60 calls/sec at 16 kHz).

**Update the package for a new device**: Only the device YAML needs to change (add the `packages:` line). The component code is fetched from GitHub at compile time.
