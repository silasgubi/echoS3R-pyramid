# echoS3R-pyramid

ESPHome firmware for the **M5Stack Atom EchoS3R (C126-ECHO)** mounted on a **Voice Pyramid Base (A167)**, running as a local voice satellite for [Home Assistant](https://www.home-assistant.io/).

This combination is **not the standard setup** — M5Stack's official firmware targets the AtomS3R (with display). The EchoS3R has no display, no LP5562 LED controller, and a different internal audio chip. Getting it to work on the Pyramid requires several non-obvious changes. This repository documents what those changes are and why.

---

## Hardware

| Component | Model |
|---|---|
| Main board | M5Stack Atom EchoS3R (C126-ECHO) |
| Base | M5Stack Voice Pyramid Base (A167) |

The EchoS3R mounts into the Pyramid's center slot. The Pyramid adds a quad-mic array (ES7210), a DAC/amp chain (ES8311 + AW87559), four groups of RGB LEDs (STM32-controlled), touch strips, and a Grove port.

---

## Why This Combination?

The EchoS3R offers on-device wake word detection and integrates cleanly with Home Assistant's voice pipeline. The Pyramid Base adds far-field mic pickup and a distinctive visual form factor. But because the EchoS3R lacks a display, M5Stack's official `echo-pyramid.yaml` (built for the AtomS3R with LCD) doesn't apply directly.

When we built this, we couldn't find any existing firmware or documentation for this specific hardware pair. This repo is what we wish had existed.

---

## What's Different from the Standard Firmware

### 1. Audio chain is entirely the Pyramid's

The EchoS3R has its own internal ES8311 DAC and NS4150B amplifier. On the Pyramid, those must be **disabled** — otherwise both audio paths compete and you get noise or silence.

```yaml
output:
  - platform: gpio
    pin: GPIO18        # EchoS3R internal amp enable — pull LOW to disable
    id: internal_amp_enable
```

All mic, DAC, and speaker I/O goes through the Pyramid's ES7210, ES8311, and AW87559.

### 2. No LP5562

The AtomS3R has an LP5562 RGB LED driver that the standard firmware uses for status indication. The EchoS3R does not have one. All LED feedback runs through the Pyramid's STM32-controlled strips via the `pyramidrgb` component.

### 3. Wake word chime must be serialized

Playing the wake word chime and starting the voice pipeline simultaneously causes I2S contention on the shared ES8311 codec. The fix is to wait for the chime to finish before starting the pipeline:

```yaml
on_wake_word_detected:
  - media_player.play_media: !lambda return id(wake_chime_url);
  - wait_until:
      condition:
        media_player.is_announcing: media_player_id
  - wait_until:
      condition:
        not:
          media_player.is_announcing: media_player_id
  - voice_assistant.start:
```

### 4. Touch strips use `publish_swipe_event`

The `pyramidtouch` component's `on_value` callback fires continuously during a touch, making it unreliable for discrete actions like volume steps. Use `publish_swipe_event: true` instead — it emits a single event per completed swipe gesture (code 1 = swipe up/vol+, code 2 = swipe down/vol−).

### 5. Don't use `grove_bus` (GPIO1/GPIO2)

Using these pins as a software I2C bus causes a crash. The Grove port on the Pyramid uses a dedicated hardware I2C bus — reference it by its bus ID instead of defining a new `grove_bus`.

---

## LED Animations

All voice state feedback is through the Pyramid's four RGB LED groups (7 LEDs each, two per strip).

| Phase | Animation |
|---|---|
| Idle | Off |
| Wake word detected | White front→back sweep |
| Listening | Cyan breathing (~2.3s cycle) |
| Thinking | Amber clockwise chase |
| Replying | White static |
| 2 min idle timeout | 3s fade-out |

Touch strips are suspended during active voice phases to prevent accidental volume changes.

---

## `components/pyramidrgb/` — LED Fix

The upstream `pyramidrgb` component had a flickering issue caused by the way ESPHome's `rgb` light platform drives outputs. Each color update calls `write_state()` separately for R, G, and B — and the upstream component immediately issued I2C writes on each call, producing two incorrect intermediate colors per update frame and up to 84+ I2C transactions per animation tick.

**Fix:** Deferred writes via dirty flags. `write_state()` updates an in-memory buffer and sets a dirty flag. `loop()` flushes dirty channels once per tick, after all three components have settled. One correct write per channel per frame, zero flickering.

See [`components/README.md`](components/README.md) for the full technical write-up.

> This component fix was developed on top of the upstream `pyramidrgb` component from [m5stack/esphome-yaml](https://github.com/m5stack/esphome-yaml). Credit to the original authors; the dirty-flag approach and per-LED write pattern are our additions.

---

## Setup

### 1. Prerequisites

- [ESPHome](https://esphome.io/) installed
- Home Assistant with the [Voice Assistant](https://www.home-assistant.io/voice_control/) pipeline configured
- M5Stack Atom EchoS3R mounted on Voice Pyramid Base

### 2. Secrets

Copy `secrets.yaml.example` to `secrets.yaml` and fill in your values:

```yaml
wifi_ssid: "your_wifi"
wifi_password: "your_password"
echo_pyramid_api_key: ""      # run: esphome generate-api-key
echo_pyramid_ota_password: ""
echo_pyramid_ap_password: ""
```

### 3. Compile and upload

```bash
# First flash (USB):
esphome run echos3r-satellite.yaml

# OTA updates:
esphome upload echos3r-satellite.yaml --device <IP_ADDRESS>
```

> **Build cache note:** Avoid wiping the full `.esphome/build/` cache. ESPHome 2026.x uses the pioarduino platform which ships esptool 5.x — incompatible with its own build scripts for bootloader generation. If the cache is cleared and the build breaks at `bootloader.bin`, replace the `esptool/` module in the pioarduino PlatformIO package with the standard PlatformIO esptool 4.x version.

---

## Repository Structure

```
echos3r-satellite.yaml      ← Main firmware (v0.3.0)
components/pyramidrgb/      ← Local component override: deferred I2C writes
components/README.md        ← Detailed pyramidrgb fix write-up
secrets.yaml.example        ← Credentials template
```

---

## Credits

- [m5stack/esphome-yaml](https://github.com/m5stack/esphome-yaml) — original `pyramidrgb` component and Pyramid hardware bring-up
- [ESPHome](https://esphome.io/) — firmware framework
- [Home Assistant](https://www.home-assistant.io/) — voice pipeline
