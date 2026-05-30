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
- **Stereo 32-bit I2S**: `handle_audio_()` assumes 8 bytes per frame (2 channels × 4 bytes). Matches the mic setup in HA Voice PE (`i2s_mics`).
- **No mocks in tests**: ESPHome components cannot be unit-tested in isolation without the full framework — always test via `esphome compile`.
- **ESPHome version**: Developed against ESPHome 2026.x. The `media_player::MEDIA_PLAYER_STATE_ANNOUNCING` enum value must exist; verify after major ESPHome updates.

## Common tasks

**Add a new tuning parameter**: Add a `number:` entity in `dynamic-volume.yaml`, a setter in the header, read it in `get_target_volume_()`, and wire it up in the `on_boot` lambda.

**Change the measurement algorithm**: Edit `handle_audio_()` in `dynamic_volume.cpp`. Keep the function fast — it runs in the I2S task on every audio buffer (~60 calls/sec at 16 kHz).

**Update the package for a new device**: Only the device YAML needs to change (add the `packages:` line). The component code is fetched from GitHub at compile time.
