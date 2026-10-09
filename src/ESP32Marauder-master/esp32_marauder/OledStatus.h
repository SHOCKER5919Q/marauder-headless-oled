// OledStatus - minimal, standalone I2C OLED status readout for the headless
// (no-HAS_SCREEN) build. It only *displays* state derived from the existing
// WiFiScan object; it reads no buttons and touches neither the SD (5/18/19/23)
// nor button (25/26/27) pins. Entirely independent of the TFT_eSPI code path,
// which stays compiled out.
#pragma once
#include "configs.h"

#ifdef HAS_OLED_STATUS

#include <U8g2lib.h>

class OledStatus {
  public:
    void begin();                 // call once from setup()
    void update(uint32_t now);    // call every loop(); self-throttles

  private:
    #ifdef OLED_SH1106
      U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2{U8G2_R0, U8X8_PIN_NONE};
    #else
      U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2{U8G2_R0, U8X8_PIN_NONE};
    #endif

    bool     ok = false;          // display init succeeded
    uint8_t  addr = OLED_ADDR;    // confirmed at begin()
    uint32_t last_draw = 0;
    uint8_t  beat = 0;            // heartbeat frame
    bool     prev_running = false; // for the scan-start reset edge

    const char* modeLabel(uint8_t mode);
    void draw();
};

#endif // HAS_OLED_STATUS
