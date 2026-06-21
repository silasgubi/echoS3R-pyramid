# echoS3R-pyramid

ESPHome firmware for the **M5Stack Atom EchoS3R (C126-ECHO)** mounted on a **Voice Pyramid Base (A167)**, running as a local voice satellite for [Home Assistant](https://www.home-assistant.io/).

This combination is **not the standard setup** — M5Stack's official firmware targets the AtomS3R (with display). The EchoS3R has no display, no LP5562 LED controller, and a different internal audio chip. Getting it to work on the Pyramid requires several non-obvious changes, most of which we discovered by trial and error. This repository documents what those changes are and why.

When we built this, we couldn't find any existing firmware or documentation for this hardware pair. This repo is what we wish had existed.

---

## Hardware

| Component | Model |
|---|---|
| Main board | M5Stack Atom EchoS3R (C126-ECHO) |
| Base | M5Stack Voice Pyramid Base (A167) |

The EchoS3R mounts into the Pyramid's center slot. The Pyramid adds a quad-mic array (ES7210), a DAC/amp chain (ES8311 + AW87559), four groups of RGB LEDs (STM32-controlled), two touch strips, and a Grove port.

---

## GPIO Map

| GPIO | Function |
|---|---|
| GPIO5 | I2S DIN — ES7210 mic input |
| GPIO6 | I2S BCLK |
| GPIO7 | I2S DOUT — ES8311 speaker output |
| GPIO8 | I2S LRCLK |
| GPIO18 | NS4150B internal amp enable — **must be pulled LOW permanently** |
| GPIO38 | I2C SDA — Pyramid ext_bus |
| GPIO39 | I2C SCL — Pyramid ext_bus |
| GPIO41 | Physical button |
| GPIO47 | IR TX — Daikin (optional) |

---

## I2C Bus (ext_bus — GPIO38/GPIO39, 50kHz)

| Address | Device |
|---|---|
| 0x1A | STM32 — PyramidRGB + PyramidTouch |
| 0x40 | ES7210 ADC (mic array) |
| 0x43 | PI4IOE5V6408 GPIO expander |
| 0x5B | AW87559 amplifier |
| 0x60 | SI5351 clock generator |

> Note: address 0x18 appears on I2C scan but is unidentified. Address 0x77 (BME688 ENV Pro) is absent on this hardware — don't reference it.

---

## What's Different from the Standard Firmware

### 1. Audio chain is entirely the Pyramid's

The EchoS3R has its own internal ES8311 DAC and NS4150B amplifier. On the Pyramid, those **must be disabled** — otherwise both audio paths compete and you get noise or silence.

```yaml
output:
  - platform: gpio
    pin: GPIO18        # EchoS3R internal amp enable — pull LOW to disable
    id: internal_amp_enable
```

All mic, DAC, and speaker I/O goes through the Pyramid's ES7210 + ES8311 + AW87559.

### 2. No LP5562

The AtomS3R has an LP5562 RGB LED driver used for status indication in the standard firmware. The EchoS3R does not have one. All LED feedback runs through the Pyramid's STM32-controlled strips via the `pyramidrgb` component.

### 3. Wake word chime must be serialized

Playing the chime and starting the voice pipeline simultaneously causes I2S contention on the shared ES8311 codec. The mic (ES7210) and speaker (ES8311) share the same bus and the STM32's AEC is weak — it doesn't cleanly cancel the chime echo, which causes the voice pipeline to hang or the wake word detector to stop responding after a few interactions.

The fix is to wait for the chime to complete before starting the pipeline:

```yaml
on_wake_word_detected:
  - script.execute: wake_word_sweep     # LED animation — GPIO only, no I2S conflict
  - media_player.play_media: !lambda return id(wake_chime_url);
  - wait_until:
      condition:
        media_player.is_announcing: media_player_id
      timeout: 500ms
  - wait_until:
      condition:
        not:
          media_player.is_announcing: media_player_id
  - voice_assistant.start:
```

> **Rule:** On any device with a shared mic/speaker codec and weak AEC, never fire `voice_assistant.start` while audio is playing.

### 4. Volume: use `volume_max`, avoid dual attenuation

The ES8311 + AW87559 chain distorts above ~60% of full scale. Set `volume_max: 0.6` on the media player and leave it there. Do not add a separate volume number entity that also attenuates — stacking two attenuations (e.g. `set_level(0.05)` × slider) results in barely-visible LEDs or nearly-inaudible audio.

### 5. Touch strips use `publish_swipe_event`

The `pyramidtouch` component's `on_value` callback fires continuously during a touch, making it unreliable for discrete actions like volume steps. Use `publish_swipe_event: true` — it emits a single event per completed swipe gesture:

| Code | Gesture | Action |
|---|---|---|
| 1 | Left strip, swipe up | Volume + |
| 2 | Left strip, swipe down | Volume − |

Touch 3 and 4 (right strip, STM32 TSC-based) are **disabled** — they behave erratically due to the STM32's internal touch controller. Only the external PT2042AD4-based pads on the left strip are reliable.

Touch is suspended (`component.suspend`) during all active voice phases and resumed on idle to prevent accidental volume changes mid-conversation.

### 6. Don't use `grove_bus` (GPIO1/GPIO2)

Defining a software I2C bus on GPIO1/GPIO2 causes a crash on boot. The ESP-IDF OTA rollback then reverts to the previous firmware silently — you'll see a successful upload followed by the old firmware date in the logs. Use the Pyramid's hardware I2C bus (`ext_bus`, GPIO38/GPIO39) for all Pyramid peripherals including the Grove port.

### 7. Don't compile with the HA ESPHome Builder add-on

This firmware includes TF Lite Micro for on-device wake word detection. The HA Builder runs on a Raspberry Pi with limited RAM — the compiler (`cc1plus`) gets OOM-killed mid-build. **Always compile on a PC** with the ESPHome CLI and upload via OTA.

---

## LED Animations

The Pyramid has two physical strips, each with two independently-controllable channel groups — four zones total. Physical mapping: ch0=back-left, ch1=front-left, ch2=front-right, ch3=back-right.

| Phase | Animation | Color |
|---|---|---|
| Idle | Amber static (brightness controlled by HA slider, gamma 2.0) | Amber (r=1.0 g=0.55 b=0.05) |
| Idle → sleep (2 min) | 3s fade-out, then off | — |
| Wake word detected | White front→back sweep | White |
| Listening | Stepped breathing, ~2.3s cycle, 3 steps each way | Cyan |
| Thinking | Clockwise chase: FL→FR→BR→BL, ~760ms/rotation | Amber |
| Replying | Static 100% | Green |
| Error | Static 100% | Red |
| Muted | Static dim | Dark red |
| Timer | Static 100% | Cyan |

The `idle_sleep_timer` script (mode: restart) counts 2 minutes of idle/muted inactivity, then fades out all strips over 3s. Any touch or wake word calls `wake_from_sleep`, which cancels the timer and restores the idle state. This prevents the amber idle glow from staying on all night.

The HA "RGB Master Brightness" slider (0–100) controls idle brightness with a gamma 2.0 curve for perceptually linear response.

---

## `components/pyramidrgb/` — LED Fix

The upstream `pyramidrgb` component had a fundamental timing problem. ESPHome's `rgb` light platform calls `write_state()` separately for R, G, and B on each frame. The upstream component immediately issued I2C writes on each call — meaning every color update produced two frames of incorrect intermediate colors and up to 84+ I2C transactions per animation tick. This caused visible flickering and, unexpectedly, added measurable latency to the voice pipeline by starving I2C bus activity.

**Fix (from [malonestar/echo-pyramid](https://github.com/malonestar/echo-pyramid)):** Deferred writes via dirty flags. `write_state()` updates an in-memory buffer and sets a dirty flag. `loop()` flushes dirty channels once per tick, after R, G, and B have all settled. One correct I2C write per channel per frame, zero intermediate garbage states.

An additional fix reverts an attempted burst write optimization: the STM32's I2C slave has an auto-increment limit shorter than a full 28-byte burst, causing pink corruption on specific LEDs. The component uses 7 individual per-LED writes per channel flush instead.

See [`components/README.md`](components/README.md) for the complete technical write-up.

> The `pyramidrgb` fix was developed by [malonestar/echo-pyramid](https://github.com/malonestar/echo-pyramid), building on the original component from [m5stack/esphome-yaml](https://github.com/m5stack/esphome-yaml). We adapted it for the EchoS3R satellite configuration.

---

## IR (Optional — Daikin)

GPIO47 drives an IR LED for Daikin split AC control using the native ARC protocol. Important notes:

- The onboard IR LED is **weak** — only reliable when aimed directly at the unit from close range. For room-scale reliability, use an external IR LED or a dedicated IR blaster.
- Set `non_blocking: false` on the `remote_transmitter`. With `non_blocking: true` and a small RMT buffer (48 symbols), the long Daikin frame gets truncated and commands are silently dropped.

---

## OTA Rollback

If new firmware crashes within 60 seconds of boot, ESP-IDF automatically reverts to the previous firmware. Symptom: OTA upload reports success, but logs show the old build date. Fix the crash first — reflashing the same broken firmware won't help.

---

## Setup

### 1. Prerequisites

- [ESPHome](https://esphome.io/) CLI installed on a PC (not the HA add-on — see above)
- Home Assistant with a configured [Voice Assistant pipeline](https://www.home-assistant.io/voice_control/)
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
# First flash via USB:
esphome run echos3r-satellite.yaml

# Subsequent updates via OTA:
esphome upload echos3r-satellite.yaml --device <IP_ADDRESS>
```

**Build cache note:** Avoid wiping the full `.esphome/build/` cache. ESPHome 2026.x uses the pioarduino platform which ships esptool 5.x — incompatible with its own build scripts for bootloader generation. If the cache is cleared and the build fails at `bootloader.bin` with `unrecognized arguments: --flash-mode --flash-freq`, replace the `esptool/` module in the pioarduino PlatformIO package with the standard PlatformIO esptool 4.x version.

---

## Known Issues

- **Audio interference:** Suspected crosstalk from the Grove cable running near the speaker. Present but minor.
- **IR range:** Onboard IR LED only reliable at close range, aimed directly at the AC unit. External LED needed for room-scale operation.
- **Boot pop/click:** Amplitude reduced but a faint click on power-up remains.

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

- [malonestar/echo-pyramid](https://github.com/malonestar/echo-pyramid) — primary reference: `pyramidrgb` fix (dirty flags, per-LED writes, stepped animations), voice assistant timing patterns, and Pyramid hardware bring-up. This project would not exist without that work.
- [m5stack/esphome-yaml](https://github.com/m5stack/esphome-yaml) — original `pyramidrgb` component upstream
- [ESPHome](https://esphome.io/) — firmware framework
- [Home Assistant](https://www.home-assistant.io/) — voice pipeline
