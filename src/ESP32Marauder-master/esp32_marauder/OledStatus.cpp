#include "OledStatus.h"

#ifdef HAS_OLED_STATUS

#include <Wire.h>
#include "WiFiScan.h"

// Global owned by the sketch; we only read it.
extern WiFiScan wifi_scan_obj;

// Owned by this module: incremented once per captured frame from
// Buffer::append() (the universal capture chokepoint), so Pkts moves in every
// scan/sniff mode. Reset at each scan start (see update()).
volatile uint32_t oled_frame_count = 0;

static const uint32_t OLED_REFRESH_MS = 250;   // ~4 Hz
static const char HEARTBEAT[4] = {'|', '/', '-', '\\'};

void OledStatus::begin() {
  Wire.begin(OLED_SDA, OLED_SCL);

  // Probe the bus so the real address is confirmed in the boot log, rather
  // than assumed. Prefer the configured address; fall back to the other common
  // one (0x3D). Controller type (SSD1306 vs SH1106) can't be told apart over
  // I2C - it's a compile-time choice (OLED_SH1106); a wrong guess shows as a
  // ~2px horizontal shift, not a failure to find the device.
  uint8_t candidates[2] = { OLED_ADDR, (uint8_t)(OLED_ADDR == 0x3C ? 0x3D : 0x3C) };
  bool found = false;
  for (uint8_t i = 0; i < 2 && !found; i++) {
    Wire.beginTransmission(candidates[i]);
    if (Wire.endTransmission() == 0) { this->addr = candidates[i]; found = true; }
  }
  if (!found) {
    Serial.println(F("[OLED] no I2C device at 0x3C/0x3D (SDA=21 SCL=22) - status display disabled"));
    this->ok = false;
    return;
  }
  Serial.print(F("[OLED] device found at 0x"));
  Serial.print(this->addr, HEX);
  #ifdef OLED_SH1106
    Serial.println(F(" (driver: SH1106)"));
  #else
    Serial.println(F(" (driver: SSD1306; set OLED_SH1106 if image is shifted)"));
  #endif

  this->u8g2.setI2CAddress(this->addr << 1);   // U8g2 wants the 8-bit address
  // 100kHz rather than the 400kHz default: the status display updates reliably
  // while the WiFi radio is active (sendBuffer completes on the MCU either way -
  // verified by trace - but 400kHz I2C can go marginal under radio EMI/load,
  // leaving the glass stuck on the last frame). 100kHz is the robust choice for
  // a low-rate status readout.
  this->u8g2.setBusClock(100000);
  this->u8g2.begin();
  this->u8g2.setFont(u8g2_font_6x10_tf);       // ~6px wide, 10px tall -> 6 lines fit
  this->ok = true;

  // brief splash so you can see it come alive before the first scan state
  this->u8g2.clearBuffer();
  this->u8g2.drawStr(0, 10, "ESP32 Marauder");
  this->u8g2.drawStr(0, 22, "status oled: ok");
  this->u8g2.sendBuffer();
  this->last_draw = millis();
}

const char* OledStatus::modeLabel(uint8_t mode) {
  // Map the WiFiScan mode enum to short labels. Covers the modes reachable from
  // this headless build's CLI; anything else shows the generic "Active".
  switch (mode) {
    case WIFI_SCAN_OFF:            return "Idle";
    case WIFI_SCAN_AP:             return "Beacon Sniff";
    case WIFI_SCAN_PROBE:          return "Probe Sniff";
    case WIFI_SCAN_DEAUTH:         return "Deauth Detect";
    case WIFI_SCAN_PWN:            return "Pwn Detect";
    case WIFI_SCAN_EAPOL:          return "PMKID/EAPOL";
    case WIFI_SCAN_RAW_CAPTURE:    return "Raw Sniff";
    case WIFI_SCAN_AP_STA:         return "Scan All";
    case WIFI_SCAN_ALL:            return "Scan All";
    case WIFI_SCAN_STATION:        return "Station Scan";
    case WIFI_SCAN_TARGET_AP:      return "Target Sniff";
    case WIFI_SCAN_TARGET_AP_FULL: return "Target Sniff";
    case WIFI_SCAN_SIG_STREN:      return "Foxhunt";
    case WIFI_SCAN_EVIL_PORTAL:    return "Evil Portal";
    case WIFI_SCAN_PINESCAN:       return "Pineapple Det";
    case WIFI_PACKET_MONITOR:      return "Pkt Monitor";
    case WIFI_ATTACK_BEACON_SPAM:  return "Beacon Spam";
    case WIFI_ATTACK_BEACON_LIST:  return "Beacon Spam";
    case WIFI_ATTACK_RICK_ROLL:    return "Rickroll";
    case WIFI_ATTACK_DEAUTH:
    case WIFI_ATTACK_DEAUTH_MANUAL:
    case WIFI_ATTACK_DEAUTH_TARGETED: return "Deauth Attack";
    case WIFI_ATTACK_AP_SPAM:      return "AP Spam";
    case WIFI_ATTACK_AUTH:         return "Auth Attack";
    case WIFI_ATTACK_MIMIC:        return "Mimic Attack";
    case BT_SCAN_ALL:              return "BLE Sniff";
    case BT_SCAN_SKIMMERS:         return "Skimmer Det";
    case BT_SCAN_AIRTAG:           return "AirTag Sniff";
    case BT_SCAN_FLIPPER:          return "Flipper Sniff";
    case BT_ATTACK_SOUR_APPLE:
    case BT_ATTACK_SWIFTPAIR_SPAM:
    case BT_ATTACK_SPAM_ALL:
    case BT_ATTACK_SAMSUNG_SPAM:
    case BT_ATTACK_GOOGLE_SPAM:
    case BT_ATTACK_FLIPPER_SPAM:   return "BLE Spam";
    default:                       return "Active";
  }
}

void OledStatus::draw() {
  uint8_t  mode    = wifi_scan_obj.currentScanMode;
  bool     running = wifi_scan_obj.scanning();
  uint8_t  ch      = wifi_scan_obj.set_channel;
  uint32_t secs    = millis() / 1000;
  uint32_t pkts    = oled_frame_count;   // frames captured this scan (all modes)

  char l1[24], l4[24], l5[24];
  snprintf(l1, sizeof(l1), "ESP32 Marauder %c", HEARTBEAT[this->beat & 3]);
  snprintf(l4, sizeof(l4), "Pkts: %lu", (unsigned long)pkts);
  snprintf(l5, sizeof(l5), "Ch:%u  Up:%lum%02lus",
           (unsigned)ch, (unsigned long)(secs / 60), (unsigned long)(secs % 60));

  this->u8g2.clearBuffer();
  this->u8g2.setFont(u8g2_font_6x10_tf);
  this->u8g2.drawStr(0, 10, l1);                               // line 1: title + heartbeat
  this->u8g2.drawStr(0, 22, this->modeLabel(mode));            // line 2: current mode
  this->u8g2.drawStr(0, 34, running ? "RUNNING" : "STOPPED");  // line 3: run status
  this->u8g2.drawStr(0, 46, l4);                               // line 4: AP counter
  this->u8g2.drawStr(0, 58, l5);                               // line 5: channel + uptime
  this->u8g2.sendBuffer();
}

void OledStatus::update(uint32_t now) {
  if (!this->ok) return;
  if ((now - this->last_draw) < OLED_REFRESH_MS) return;
  this->last_draw = now;
  this->beat++;
  // Reset the frame counter at each scan start (STOPPED -> RUNNING edge), so
  // Pkts reflects the current scan, matching how the firmware zeroes its own
  // per-scan counters.
  bool running = wifi_scan_obj.scanning();
  if (running && !this->prev_running) oled_frame_count = 0;
  this->prev_running = running;
  this->draw();
}

#endif // HAS_OLED_STATUS
