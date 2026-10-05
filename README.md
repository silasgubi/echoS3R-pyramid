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

## Features (v0.5.2)

- On-device wake word with `micro_wake_word` (`okay_nabu`, `hey_jarvis`, `hey_mycroft`) and a sensitivity select; the engine can also run in Home Assistant
- Full voice pipeline with timers, mute switch and button, and a 10 s long press for factory reset
- 28 individually addressable LEDs with Voice PE-style effects (idle, listening, thinking, timers, errors, offline)
- Touch volume on the left strip: tap, hold to repeat, swipe, with an LED volume bar
- Optional "stop" barge-in while the assistant is replying (experimental)
- Optional IR transmitter for a Daikin AC

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

### 3. Mic and speaker share ONE I2S bus (no 48 kHz, no overlapping chime)

The mic (ES7210) and the speaker DAC (ES8311) sit on the same I2S bus (BCLK GPIO6 / LRCLK GPIO8), so they share a single sample rate and cannot be driven independently. Two consequences:

- **Everything runs at 16 kHz.** `micro_wake_word` needs a 16 kHz mic, so the speaker graph is 16 kHz too. The Voice PE can play at 48 kHz only because its XMOS gives it two I2S buses with independent clocks. Set the media player pipelines to `sample_rate: 16000` (FLAC) so Home Assistant resamples TTS on the server instead of downsampling on the device, which sounds muffled.
- **The wake chime delays listening.** Playing the chime and starting the voice pipeline at the same time makes the mic driver fail and retry (`Driver failed to start; retrying in 1 second`), which cost about 2 s in the logs. The STM32's AEC is also weak, so it does not cleanly cancel the chime echo.

So the firmware has a **Wake Sound** switch that is **OFF by default**: the wake word triggers an LED sweep and listening starts right away. With the switch ON it serializes everything: chime, wait for the announcement to finish, about 700 ms for the I2S TX teardown, then the voice assistant.

> **Rule:** On any device with a shared mic/speaker codec and weak AEC, never fire `voice_assistant.start` while audio is playing.

### 4. Volume: clamp with `volume_max`, avoid dual attenuation

The ES8311 + AW87559 chain eventually distorts near full scale. Clamp it with `volume_max` on the media player and leave it there. Here it is 0.70, tuned by ear (0.4 and 0.55 were too quiet); if yours distorts at maximum, back off to 0.60-0.65. The standard Home Assistant slider (0-1) then becomes the only volume control. Do not add a separate volume number entity that also attenuates: stacking two attenuations leaves you with nearly inaudible audio or barely visible LEDs.

### 5. Touch strips use `publish_swipe_event`

The `pyramidtouch` component's `on_value` callback fires continuously during a touch, making it unreliable for discrete actions like volume steps. Use `publish_swipe_event: true` — it emits a single event per completed swipe gesture:

| Code | Gesture | Action |
|---|---|---|
| 1 | Left strip, swipe up | Volume + |
| 2 | Left strip, swipe down | Volume − |

Touch 3 and 4 (right strip, STM32 TSC-based) are **disabled** — they behave erratically due to the STM32's internal touch controller. Only the external PT2042AD4-based pads on the left strip are reliable.

Touch is suspended (`component.suspend`) during all active voice phases and resumed on idle to prevent accidental volume changes mid-conversation.

A short tap on a left pad is one volume step, holding it repeats, and a full swipe adds a bigger jump (taps that belong to a swipe are suppressed). In idle or muted state the LEDs show a volume bar (N of 28 white LEDs for 1.5 s).

### 6. Don't use `grove_bus` (GPIO1/GPIO2)

Defining a software I2C bus on GPIO1/GPIO2 causes a crash on boot. The ESP-IDF OTA rollback then reverts to the previous firmware silently — you'll see a successful upload followed by the old firmware date in the logs. Use the Pyramid's hardware I2C bus (`ext_bus`, GPIO38/GPIO39) for all Pyramid peripherals including the Grove port.

### 7. Don't compile with the HA ESPHome Builder add-on

This firmware includes TF Lite Micro for on-device wake word detection. The HA Builder runs on a Raspberry Pi with limited RAM — the compiler (`cc1plus`) gets OOM-killed mid-build. **Always compile on a PC** with the ESPHome CLI and upload via OTA. For the same reason, do not use the Builder's "Update" button on this device: if its copy of the YAML is stale, it recompiles that copy and overwrites the firmware you flashed from your PC.

---

## LED Animations

The Pyramid's STM32 exposes **one register per LED**, so the `pyramidrgb` light is a true **addressable light of 28 LEDs** (4 zones x 7). Logical ring order, seen from above with the tip as the front: `0-6` back-left | `7-13` front-left | `14-20` front-right | `21-27` back-right. The effects are ported from the Voice PE and adapted from 12 to 28 LEDs:

| State | Effect | Look |
|---|---|---|
| Idle | Idle Sparkle | Cool blue with a soft per-LED sparkle (starry sky) |
| Idle for 2 min | Fade-out | 3 s fade, then off |
| Wake word detected | White sweep | From the back to the front tip, both sides meeting at the tip |
| Listening | Listening | Cyan breathing, about 1.6 s per cycle |
| Thinking | Thinking | Amber comet spinning around the ring |
| Replying | Solid | Green |
| Error | Error Pulse | Red breathing |
| Muted | Solid, dim | Dark red |
| Timer counting | Timer Tick | Blue arc, the lit fraction of the ring is the time left |
| Timer ringing | Timer Ring | Orange pulse |
| WiFi or HA down | Offline Twinkle | Flickering red ember |

A central `update_leds` script picks the effect with a priority chain: offline > timer ringing > error > voice phases > timer counting > muted > idle. The `idle_sleep_timer` script (mode: restart) counts 2 minutes of idle inactivity and then fades the LEDs out; any wake word or voice activity calls `wake_from_sleep`, which cancels the timer and restores the state.

The "RGB Master Brightness" number (0-100) sets the brightness in hardware (STM32 registers 0x10/0x11), so it never double-attenuates the effect colors.

---

## `components/pyramidrgb/` - LED driver

The upstream `pyramidrgb` component had a timing problem. ESPHome's `rgb` light platform calls `write_state()` separately for R, G, and B on each frame, and the upstream component issued I2C writes immediately on every call. Each color update produced intermediate wrong colors and up to 84+ I2C transactions per animation tick, which caused visible flicker and added latency to the voice pipeline by starving the I2C bus.

- **Dirty flags** (from [malonestar/echo-pyramid](https://github.com/malonestar/echo-pyramid)): `write_state()` updates an in-memory buffer and the flush happens once per tick, after R, G, and B have settled.
- **Addressable light** (`light/`, since v0.5): `PyramidRGBLight` is an `AddressableLight` with 28 individual LEDs, so effects can address each LED. It keeps a cache of the last frame per zone and sends over I2C only the zones that changed.
- **Write mode:** by default one zone is written per transaction (29 bytes). If your STM32 does not auto-increment registers on such a write (symptom: pink corruption on specific LEDs), set `per_led_write: true` for 7 transactions of 5 bytes per zone. Zones 0 and 1 have an inverted hardware index so that the visual flow around the ring is continuous.

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

- [ESPHome](https://esphome.io/) 2026.6 or newer, CLI installed on a PC (not the HA add-on — see above)
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
echos3r-satellite.yaml      <- Main firmware (v0.5.2)
components/pyramidrgb/      <- Local component: output (dirty flags) and addressable light
secrets.yaml.example        <- Credentials template
```

---

## Credits

- [malonestar/echo-pyramid](https://github.com/malonestar/echo-pyramid) — primary reference: `pyramidrgb` fix (dirty flags, per-LED writes, stepped animations), voice assistant timing patterns, and Pyramid hardware bring-up. This project would not exist without that work.
- [m5stack/esphome-yaml](https://github.com/m5stack/esphome-yaml) — original `pyramidrgb` component upstream
- [ESPHome](https://esphome.io/) — firmware framework
- [Home Assistant](https://www.home-assistant.io/) — voice pipeline

---

## License

MIT — see [LICENSE](LICENSE).
