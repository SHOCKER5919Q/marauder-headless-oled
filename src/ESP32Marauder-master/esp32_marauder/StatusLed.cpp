#include "StatusLed.h"

#ifdef HAS_STATUS_LED

#include "WiFiScan.h"

extern WiFiScan wifi_scan_obj;   // we only read scanning()

static const uint32_t LED_BLINK_MS = 150;   // heartbeat period while active (~3 Hz)

static inline void led_write(bool on) {
  #ifdef STATUS_LED_ACTIVE_HIGH
    digitalWrite(STATUS_LED_PIN, on ? HIGH : LOW);
  #else
    digitalWrite(STATUS_LED_PIN, on ? LOW : HIGH);
  #endif
}

void StatusLed::begin() {
  pinMode(STATUS_LED_PIN, OUTPUT);
  led_write(true);               // idle default: solid ON
}

void StatusLed::update(uint32_t now) {
  if (wifi_scan_obj.scanning()) {
    // Activity (scan/sniff/attack): heartbeat blink
    if ((now - this->last_toggle) >= LED_BLINK_MS) {
      this->last_toggle = now;
      this->blink_on = !this->blink_on;
      led_write(this->blink_on);
    }
  } else {
    // Idle: solid ON
    led_write(true);
    this->blink_on = false;
  }
}

#endif // HAS_STATUS_LED
