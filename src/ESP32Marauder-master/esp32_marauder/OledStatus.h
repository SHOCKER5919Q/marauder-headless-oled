// OledStatus - 3-button OLED menu UI for the headless build.
// Splash -> Dashboard -> Category menu -> Feature menu, with confirm (TX),
// running-status, scanned-AP list (target picker) and a channel picker.
// Buttons: UP=GPIO25, SELECT=GPIO26 (short=enter/confirm, hold=back), DOWN=GPIO27.
// Every feature fires the real CLI via cli_obj.runCommand(); no scan logic is
// duplicated. Independent of the TFT menu (HAS_SCREEN stays off).
#pragma once
#include "configs.h"

#ifdef HAS_OLED_STATUS

#include <U8g2lib.h>

class OledStatus {
  public:
    void begin();
    void update(uint32_t now);

  private:
    #ifdef OLED_SH1106
      U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2{U8G2_R0, U8X8_PIN_NONE};
    #else
      U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2{U8G2_R0, U8X8_PIN_NONE};
    #endif

    bool     ok = false;
    uint8_t  addr = OLED_ADDR;
    uint32_t last_draw = 0;
    uint8_t  beat = 0;

    // --- UI state ---
    uint8_t  screen = 0;       // Screen enum (see .cpp)
    uint32_t splash_until = 0;
    int      cat_idx = 0;      // selected category (main menu)
    int      cat_top = 0;      // scroll offset (main menu)
    int      feat_idx = 0;     // selected feature (feature menu)
    int      feat_top = 0;
    int      list_idx = 0;     // selected AP (list view)
    int      list_top = 0;
    int      chan_val = 1;     // channel picker value
    const void* pend_item = 0; // pending Item* awaiting confirm / target
    int      pend_target = -1; // chosen AP index

    // pkts/sec sampling
    uint32_t last_pkt_sample = 0;
    uint32_t last_pkt_count = 0;
    uint32_t pkt_rate = 0;

    // transient toast
    uint32_t toast_until = 0;
    char     toast[22] = {0};

    void drawSplash();
    void drawDash();
    void drawMenu(const char* title, int idx, int top, int count,
                  const char* (*label_of)(int));
    void drawConfirm();
    void drawRunning();
    void drawList();
    void drawChan();
    void drawToast();
    void showToast(const char* msg);

    void handleButtons(uint32_t now);
    void runItem(const void* item);
    void samplePkts(uint32_t now);
    const char* modeLabel(uint8_t mode);
};

#endif // HAS_OLED_STATUS
