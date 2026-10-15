// leds.h - three WS2812B strips: front warm white, rear red, amber indicators.
#pragma once
#include <Adafruit_NeoPixel.h>
#include "config.h"

class Leds {
 public:
  Leds()
      : front_(NUM_LEDS_FRONT, PIN_LED_FRONT, NEO_GRB + NEO_KHZ800),
        rear_(NUM_LEDS_REAR, PIN_LED_REAR, NEO_GRB + NEO_KHZ800),
        amber_(NUM_LEDS_AMBER, PIN_LED_AMBER, NEO_GRB + NEO_KHZ800) {}

  void begin() {
    front_.begin(); rear_.begin(); amber_.begin();
    front_.setBrightness(LED_BRIGHTNESS);
    rear_.setBrightness(LED_BRIGHTNESS);
    amber_.setBrightness(LED_BRIGHTNESS);
    apply(false, false, false, true);
  }

  void setNight(bool on) { night_ = on; }                 // front + rear lights
  void setAmberFlashing(bool on) { amberFlash_ = on; }    // amber strip (used during crash countdown)

  // Call every LED_PERIOD_MS. Only touches the strips when something changed.
  void update(uint32_t now) {
    const bool amberOn = amberFlash_ && ((now / AMBER_FLASH_HALF_PERIOD_MS) % 2 == 0);
    apply(night_, night_, amberOn, false);
  }

 private:
  void apply(bool frontOn, bool rearOn, bool amberOn, bool force) {
    if (force || frontOn != frontOn_) { fill(front_, frontOn, front_.Color(255, 147, 41)); frontOn_ = frontOn; }  // warm white
    if (force || rearOn != rearOn_)   { fill(rear_,  rearOn,  rear_.Color(255, 0, 0));     rearOn_ = rearOn; }    // red
    if (force || amberOn != amberOn_) { fill(amber_, amberOn, amber_.Color(255, 110, 0));  amberOn_ = amberOn; }  // amber
  }
  static void fill(Adafruit_NeoPixel& strip, bool on, uint32_t color) {
    strip.fill(on ? color : 0);
    strip.show();
  }

  Adafruit_NeoPixel front_, rear_, amber_;
  bool night_ = false, amberFlash_ = false;
  bool frontOn_ = false, rearOn_ = false, amberOn_ = false;
};
