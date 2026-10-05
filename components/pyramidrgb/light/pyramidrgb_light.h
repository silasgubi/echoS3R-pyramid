#pragma once

#include "esphome/core/component.h"
#include "esphome/components/i2c/i2c.h"
#include "esphome/components/light/addressable_light.h"
#include "esphome/core/log.h"

namespace esphome {
namespace pyramidrgb {

// Addressable light of 28 LEDs (4 zones x 7) on the Voice Pyramid's STM32 (0x1A).
// The STM32 exposes one register PER LED: zone_base + hw_index*4, bytes [B,G,R,0x00].
// Logical order of the "ring" (seen from above, tip = front):
//   0-6  BackLeft | 7-13 FrontLeft | 14-20 FrontRight | 21-27 BackRight
// Zones 0/1 have an inverted hardware index (same convention as the per-
// group driver of v0.4) so the visual flow is continuous.
// Writes are coalesced per zone with a cache of the last frame: only zones that changed
// go to I2C (a bus shared with the codecs/touch, so economy matters).
class PyramidRGBLight : public light::AddressableLight, public i2c::I2CDevice {
 public:
  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::HARDWARE; }

  int32_t size() const override { return NUM_LEDS; }
  light::LightTraits get_traits() override {
    auto traits = light::LightTraits();
    traits.set_supported_color_modes({light::ColorMode::RGB});
    return traits;
  }
  void write_state(light::LightState *state) override;
  void clear_effect_data() override {
    for (int i = 0; i < this->size(); i++)
      this->effect_data_[i] = 0;
  }

  void set_initial_brightness(uint8_t pct) { initial_brightness_ = pct; }
  // true = 7 transactions of 5 bytes per zone (fallback if the STM32 does not
  // auto-increment registers on a 29-byte write)
  void set_per_led_write(bool v) { per_led_write_ = v; }
  // Global brightness per strip in hardware (regs 0x10/0x11), 0-100%.
  // strip 1 = zones BL+FL, strip 2 = FR+BR.
  bool set_strip_brightness(uint8_t strip, uint8_t pct);

 protected:
  static const uint8_t NUM_LEDS = 28;
  static const uint8_t LEDS_PER_ZONE = 7;
  static const uint8_t NUM_ZONES = 4;

  light::ESPColorView get_view_internal(int32_t index) const override;
  void flush_zone_(uint8_t zone);

  uint8_t buf_[NUM_LEDS * 3] = {0};
  uint8_t effect_data_[NUM_LEDS] = {0};
  uint8_t last_sent_[NUM_ZONES][LEDS_PER_ZONE * 4];
  bool zone_ever_sent_[NUM_ZONES] = {false, false, false, false};
  uint8_t initial_brightness_{80};
  bool per_led_write_{false};
};

}  // namespace pyramidrgb
}  // namespace esphome
