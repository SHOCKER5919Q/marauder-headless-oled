// StatusLed - onboard activity LED for the headless build. Solid ON when idle;
// heartbeat-blink while a scan/sniff/attack is running. Standalone and
// read-only w.r.t. scan state (polls wifi_scan_obj.scanning()); independent of
// the OLED and of the stock HAS_ACT_LED path.
#pragma once
#include "configs.h"

#ifdef HAS_STATUS_LED

#include <Arduino.h>

class StatusLed {
  public:
    void begin();                 // call once from setup()
    void update(uint32_t now);    // call every loop(); self-throttles

  private:
    uint32_t last_toggle = 0;
    bool     blink_on = false;
};

#endif // HAS_STATUS_LED
