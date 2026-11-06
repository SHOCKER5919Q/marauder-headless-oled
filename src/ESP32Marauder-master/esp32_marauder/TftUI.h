// TftUI - modern 1.8" SPI TFT (ST7735 128x160) + 6-button UI for the headless
// Marauder build. Independent of Marauder's HAS_SCREEN menu (that targets
// 240x320). Renders flicker-free through a full-frame GFXcanvas16 and drives the
// real firmware via cli_obj.runCommand() + direct reads of wifi_scan_obj.
#pragma once
#include "configs.h"

#ifdef HAS_TFT_UI

#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>

class TftUI {
  public:
    void begin();
    void update(uint32_t now);

  private:
    Adafruit_ST7735* tft = nullptr;
    GFXcanvas16*     cv  = nullptr;   // 128x160x2 = 40KB off-screen framebuffer
    bool ok = false;

    // ---- screens ----
    enum Screen {
      SPLASH, HWCHECK, HOME, MENU,
      WIFI_SCAN, WIFI_DETAIL, DEVICES, DEV_DETAIL,
      PACKETS, MACCFG, KEYBOARD,
      SETTINGS, DISPLAY_SET, MANUAL_LIST, MANUAL_VIEW,
      STORAGE, TOOLS
    };
    Screen screen = SPLASH;
    Screen kb_return = HOME;          // where the keyboard returns to on OK/cancel
    uint32_t splash_until = 0;
    uint32_t last_draw = 0;
    uint8_t  beat = 0;

    // ---- navigation state ----
    int menu_idx = 0;                 // main grid selection
    int list_idx = 0, list_top = 0;   // generic scroll list (wifi/devices/manual/settings)
    int detail_idx = 0;               // selected row in a detail/action screen
    int sel_ap = -1, sel_sta = -1;    // chosen network / device index
    int manual_idx = 0, manual_scroll = 0;
    int settings_idx = 0;
    int tools_idx = 0;
    uint32_t pkt_t0 = 0;              // packet-monitor start time

    // ---- keyboard ----
    char   kb_buf[64] = {0};
    int    kb_len = 0;
    int    kb_row = 0, kb_col = 0;
    int    kb_page = 0;               // 0=abc 1=ABC 2=123 3=sym  (4=HEX for MAC)
    bool   kb_mask = true;            // password masking
    bool   kb_hex = false;           // MAC entry mode
    const char* kb_title = "Enter Text";

    // ---- settings (persisted) ----
    uint8_t set_brightness = 80;      // 0..100 (only applies if TFT_UI_BL wired)
    uint8_t set_accent = 0;           // 0=red 1=cyan 2=green
    uint16_t COL_ACCENT = 0;

    // ---- MAC config ----
    uint8_t mac_orig[6] = {0};
    uint8_t mac_shown[6] = {0};

    // ---- toast / notification ----
    char toast[40] = {0};
    uint32_t toast_until = 0;
    uint8_t  toast_kind = 0;          // 0 info 1 ok 2 warn 3 err

    // ---- buttons ----
    struct Btn { uint8_t pin; bool down; bool prev; uint32_t t_edge; uint32_t t_rep; };
    Btn b_up, b_dn, b_lt, b_rt, b_en, b_bk;
    bool evUp=false, evDn=false, evLt=false, evRt=false, evEn=false, evBk=false, evBkLong=false;

    // ---- helpers ----
    void loadPrefs();
    void savePrefs();
    void applyAccent();
    void pollButtons(uint32_t now);
    void handleInput(uint32_t now);
    void showToast(const char* msg, uint8_t kind);
    void push();                      // blit canvas -> panel

    // chrome
    void header(const char* title, bool back);
    void footer(const char* hint);
    void drawBattery(int x, int y);
    void scrollbar(int top, int count, int visible);

    // screens
    void drawSplash();
    void drawHwCheck();
    void drawHome();
    void drawMenu();
    void drawWifiScan();
    void drawWifiDetail();
    void drawDevices();
    void drawDevDetail();
    void drawPackets();
    void drawMacCfg();
    void drawKeyboard();
    void drawSettings();
    void drawDisplaySet();
    void drawManualList();
    void drawManualView();
    void drawStorage();
    void drawTools();
    void drawToast();

    // actions
    void openKeyboard(const char* title, bool mask, bool hex, Screen ret);
    void kbInsertCurrent();
    void applyPassword();             // join selected AP with typed password
    void applyManualMac();            // set STA MAC from typed hex
    void randomizeMac();
    void restoreMac();
    void readStaMac(uint8_t* out);

    // packet counters (owned here; Buffer::append increments them)
    const char* secLabel(uint8_t sec);
    const char* modeLabel(uint8_t mode);
    bool sdReady();
};

#endif // HAS_TFT_UI
