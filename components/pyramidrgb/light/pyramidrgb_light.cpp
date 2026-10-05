#include "pyramidrgb_light.h"

#include <cstring>

namespace esphome {
namespace pyramidrgb {

static const char *const TAG = "pyramidrgb.light";

// Register bases per logical zone (BL, FL, FR, BR). Note: FR uses the base of
// STM32 channel 4 and BR that of channel 3, the same remapping as the v0.4 driver.
static const uint8_t ZONE_BASE_REG[4] = {0x20, 0x3C, 0x7C, 0x60};
static const bool ZONE_INVERTED[4] = {true, true, false, false};
static const uint8_t BRIGHT_REG[2] = {0x10, 0x11};

void PyramidRGBLight::setup() {
  ESP_LOGI(TAG, "PyramidRGB addressable light (STM32 @0x%02X, %u LEDs)", this->address_, NUM_LEDS);
  this->set_strip_brightness(1, this->initial_brightness_);
  this->set_strip_brightness(2, this->initial_brightness_);
}

void PyramidRGBLight::dump_config() {
  ESP_LOGCONFIG(TAG, "PyramidRGB Light: %u LEDs, %u zones, per_led_write=%s", NUM_LEDS, NUM_ZONES,
                this->per_led_write_ ? "true" : "false");
  LOG_I2C_DEVICE(this);
}

bool PyramidRGBLight::set_strip_brightness(uint8_t strip, uint8_t pct) {
  if (strip < 1 || strip > 2)
    return false;
  if (pct > 100)
    pct = 100;
  uint8_t b = (uint8_t) ((pct * 255) / 100);
  return this->write_byte(BRIGHT_REG[strip - 1], b);
}

light::ESPColorView PyramidRGBLight::get_view_internal(int32_t index) const {
  auto *buf = const_cast<uint8_t *>(this->buf_);
  auto *effect = const_cast<uint8_t *>(this->effect_data_);
  return {buf + index * 3 + 0, buf + index * 3 + 1, buf + index * 3 + 2,
          nullptr, effect + index, &this->correction_};
}

void PyramidRGBLight::write_state(light::LightState *state) {
  for (uint8_t z = 0; z < NUM_ZONES; z++)
    this->flush_zone_(z);
  this->mark_shown_();
}

void PyramidRGBLight::flush_zone_(uint8_t zone) {
  uint8_t payload[LEDS_PER_ZONE * 4];
  for (uint8_t hw = 0; hw < LEDS_PER_ZONE; hw++) {
    uint8_t logical = ZONE_INVERTED[zone] ? (LEDS_PER_ZONE - 1 - hw) : hw;
    const uint8_t *rgb = this->buf_ + (zone * LEDS_PER_ZONE + logical) * 3;
    payload[hw * 4 + 0] = rgb[2];  // B
    payload[hw * 4 + 1] = rgb[1];  // G
    payload[hw * 4 + 2] = rgb[0];  // R
    payload[hw * 4 + 3] = 0x00;
  }
  if (this->zone_ever_sent_[zone] && memcmp(payload, this->last_sent_[zone], sizeof(payload)) == 0)
    return;

  bool ok;
  if (this->per_led_write_) {
    ok = true;
    for (uint8_t hw = 0; hw < LEDS_PER_ZONE; hw++) {
      uint8_t frame[5];
      frame[0] = ZONE_BASE_REG[zone] + hw * 4;
      memcpy(frame + 1, payload + hw * 4, 4);
      ok = (this->write(frame, sizeof(frame)) == i2c::ERROR_OK) && ok;
    }
  } else {
    uint8_t frame[1 + LEDS_PER_ZONE * 4];
    frame[0] = ZONE_BASE_REG[zone];
    memcpy(frame + 1, payload, sizeof(payload));
    ok = this->write(frame, sizeof(frame)) == i2c::ERROR_OK;
  }

  if (ok) {
    memcpy(this->last_sent_[zone], payload, sizeof(payload));
    this->zone_ever_sent_[zone] = true;
  } else {
    ESP_LOGW(TAG, "I2C write failed for zone %u", zone);
  }
}

}  // namespace pyramidrgb
}  // namespace esphome
