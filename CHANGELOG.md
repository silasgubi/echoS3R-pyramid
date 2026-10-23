# Changelog

Versions follow [Semantic Versioning](https://semver.org/). Versions before 0.6.0 are described in the git history.

## 0.6.0 - 2026-10-23

Protection release. Our Voice Pyramid base failed (I2C bus stuck, chips silent, base hot on its own), and v0.5.2 had
no protection at all. Updating is strongly recommended.

### Added

- Thermal protection in the firmware: the ESP32-S3 chip temperature limits the volume to 0.5 at 70 °C and switches
  the amp off at 80 °C, with hysteresis on the way down. New `Thermal Protection` sensor.
- Base fault watch: every 30 s the base chips are probed on I2C; two failed checks in a row switch the amp off and
  turn on the new `Base Fault` problem sensor.
- Safe mode after 3 short boots in a row (ESPHome default was 10), switching the base amp and LEDs off with direct
  I2C writes, because a software reboot does not reset the base chips.
- Amp lock: while a protection is active, nothing can switch the amp back on.
- Diagnostics: `Chip Temperature`, `Reset Reason` and `Uptime`.
- README: safety warning, protection kit section and suggested Home Assistant alerts.

### Fixed

- Music from Home Assistant / Music Assistant showed "playing" with no sound (`Parent bus is busy`): the mic now
  releases the shared I2S bus while music plays and takes it back when the music ends.
- README: address 0x18 is the ES8311 DAC, not an unknown device.
