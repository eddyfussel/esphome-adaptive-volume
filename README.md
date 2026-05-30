# esphome-dynamic-volume

An ESPHome custom component for Home Assistant Voice PE devices and the FutureProofHomes Satellite1 Speaker that automatically adjusts TTS (text-to-speech) playback volume based on measured ambient noise.

> **Disclaimer:** This project was vibe coded. Use at your own risk. I am not responsible for broken hardware, unexpected device behavior, or any other effects caused by using this component.

## Versioning & Compatibility

Pin the package to a specific ref that matches your ESPHome installation:

| Package ref | ESPHome version | HA Voice PE ref | Status |
|---|---|---|---|
| `@esphome-2026.04` | 2026.4.x | `26.4.0` | maintained |
| `@main` | 2026.5+ | `26.5.0` / `dev` | latest |

**Note:** The official HA Voice PE firmware also enforces a minimum ESPHome version. Pin both refs together — mismatched versions cause the *"Current ESPHome Version is too old"* error.

## Supported devices

| Device | Microphone ID | Package |
|---|---|---|
| HA Voice PE | `i2s_mics` | `dynamic-volume.yaml` |
| FutureProofHomes Satellite1 | `sat1_mics` | `dynamic-volume-satellite1.yaml` |

## Usage

### HA Voice PE

```yaml
packages:
  Nabu Casa.Home Assistant Voice PE: github://esphome/home-assistant-voice-pe/home-assistant-voice.yaml
  dynamic_volume: github://eddyfussel/voice-pe-dynamic-volume/dynamic-volume.yaml@main
```

For ESPHome 2026.4.x:
```yaml
packages:
  Nabu Casa.Home Assistant Voice PE: github://esphome/home-assistant-voice-pe/home-assistant-voice.yaml@<compatible-tag>
  dynamic_volume: github://eddyfussel/voice-pe-dynamic-volume/dynamic-volume.yaml@esphome-2026.04
```

### FutureProofHomes Satellite1 Speaker

Use the dedicated package (tested against Satellite1 firmware v0.2.0):

```yaml
packages:
  dynamic_volume: github://eddyfussel/voice-pe-dynamic-volume/dynamic-volume-satellite1.yaml@main
```

Or override the microphone substitution in your device YAML:

```yaml
substitutions:
  dynamic_volume_mic_id: sat1_mics

packages:
  dynamic_volume: github://eddyfussel/voice-pe-dynamic-volume/dynamic-volume.yaml@main
```

See [example.yaml](example.yaml) and [example-local.yaml](example-local.yaml) for complete device config templates.

## How it works

The component registers an audio callback on the I2S microphone. While the device is idle (not playing TTS), it continuously measures the ambient noise level as a smoothed dBFS value. The moment the media player transitions to the `ANNOUNCING` state, the component calculates a target volume via linear interpolation between configurable quiet/loud thresholds and applies it before the TTS starts playing.

```
ambient dBFS  │  quiet_db (-50)  ────────────  loud_db (-25)
TTS volume    │  min_vol (0.40)  ────────────  max_vol (0.85)
```

Requires a microphone component and a media player named `external_media_player` — both provided by the respective device firmware.

## Home Assistant entities

After flashing, these entities appear in Home Assistant:

| Entity | Type | Description |
|---|---|---|
| Ambient Noise Level | Sensor | Current smoothed ambient level in dBFS (diagnostic) |
| Dynamic TTS Volume | Switch | Enable / disable the feature |
| Dynamic Volume Min | Number | Volume at quiet threshold (0.0–1.0) |
| Dynamic Volume Max | Number | Volume at loud threshold (0.0–1.0) |
| Quiet Threshold (dBFS) | Number | dBFS level that maps to min volume |
| Loud Threshold (dBFS) | Number | dBFS level that maps to max volume |

## Calibration

Typical starting values (adjustable from HA without re-flashing):

- **Quiet Threshold**: -50 dBFS (silent room → min volume)
- **Loud Threshold**: -25 dBFS (noisy environment → max volume)
- **Min Volume**: 0.40
- **Max Volume**: 0.85

Watch the `Ambient Noise Level` sensor in HA to find the right thresholds for your environment. Talk loudly near the device and note the peak values.

## Development

Requires [Task](https://taskfile.dev) and [ESPHome](https://esphome.io):

```shell
brew install go-task          # macOS
uv tool install esphome --with wheel,pip
```

### Available tasks

```shell
task --list
```

| Task | Description |
|---|---|
| `task check:ha-voice-pe` | Validate YAML for HA Voice PE (fast) |
| `task check:satellite1` | Validate YAML for Satellite1 (fast, clones sat1 repo if needed) |
| `task check` | Validate all targets |
| `task compile:ha-voice-pe` | Full C++ compile for HA Voice PE (slow) |
| `task compile:satellite1` | Full C++ compile for Satellite1 (slow) |
| `task compile` | Full compile all targets |
| `task clean` | Remove ESPHome build artifacts |
| `task clean:all` | Remove artifacts + cached Satellite1 repo |

### First-time setup

```shell
# The Taskfile handles everything automatically on first run:
task check:ha-voice-pe    # creates tests/secrets.yaml with dummy values
task check:satellite1     # clones Satellite1 v0.2.0 repo to .sat1-esphome/
```

For actual device compilation, replace `tests/secrets.yaml` with real credentials.

## Repository structure

```
components/
└── dynamic_volume/
    ├── __init__.py                  # ESPHome schema + code generation
    ├── dynamic_volume.h             # C++ component class
    └── dynamic_volume.cpp
dynamic-volume.yaml                  # Package for HA Voice PE (remote include)
dynamic-volume-satellite1.yaml       # Package for Satellite1 (remote include)
dynamic-volume-local.yaml            # Package for local development
example.yaml                         # Example device config (remote)
example-local.yaml                   # Example device config (local dev)
Taskfile.yml                         # Developer workflow tasks
tests/
├── compile-ha-voice-pe.yaml         # HA Voice PE compile/check test
└── compile-satellite1.yaml.tmpl     # Satellite1 compile/check test template
```

## Thread safety

The I2S audio callback runs in a FreeRTOS task at priority 23. The component uses:
- `std::atomic<float> smoothed_dbfs_` — written by I2S task, read by main loop
- `std::atomic<bool> enabled_` — written by main loop (switch), read by I2S task

All other state is only touched from the main loop.
