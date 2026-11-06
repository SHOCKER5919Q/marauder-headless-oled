#include "TftUI.h"

#ifdef HAS_TFT_UI

#include <esp_wifi.h>
#include <Preferences.h>
#include "WiFiScan.h"
#include "CommandLine.h"
#include "SDInterface.h"
#include <SD.h>
#include <FS.h>

// ---- firmware globals we drive / read ----
extern CommandLine cli_obj;
extern WiFiScan wifi_scan_obj;
extern SDInterface sd_obj;
extern LinkedList<AccessPoint>* access_points;
extern LinkedList<Station>* stations;

// ---- packet counters (owned here; Buffer::append() increments them) ----
volatile uint32_t oled_frame_count = 0;
volatile uint32_t ac_mgmt = 0, ac_data = 0, ac_ctrl = 0, ac_beacon = 0, ac_probe = 0, ac_deauth = 0;

// ---- colors (RGB565) ----
#define RGB(r,g,b) ((uint16_t)(((r&0xF8)<<8)|((g&0xFC)<<3)|(b>>3)))
static const uint16_t C_BG    = RGB(0,0,0);
static const uint16_t C_FG    = RGB(255,255,255);
static const uint16_t C_MUT   = RGB(140,150,165);
static const uint16_t C_PANEL = RGB(22,26,38);
static const uint16_t C_LINE  = RGB(40,48,70);
static const uint16_t C_RED   = RGB(255,59,59);
static const uint16_t C_CYAN  = RGB(0,224,255);
static const uint16_t C_GREEN = RGB(40,210,110);
static const uint16_t C_AMBER = RGB(255,176,32);

static Preferences tui_prefs;

// ---- keyboard layouts ----
static const char* KB_ABC[3] = {"qwertyuiop","asdfghjkl","zxcvbnm"};
static const char* KB_UPR[3] = {"QWERTYUIOP","ASDFGHJKL","ZXCVBNM"};
static const char* KB_NUM[3] = {"1234567890","@#$%^&*()","-_=+[]:;"};
static const char* KB_SYM[3] = {"~`|\\/<>?!",".,\"'{}()","+-=*%&"};
static const char* KB_HEX[2] = {"0123456789","ABCDEF"};

// ---- built-in manual (SD /marauder/manual/<file>.md overrides when present) ----
struct Manual { const char* title; const char* file; const char* body; };
static const Manual MANUALS[] = {
  {"Getting Started","home",
    "MARAUDER is a 2.4GHz Wi-Fi analysis and security research tool.\n\nUse it only on networks and devices you own or are explicitly authorized to test.\n\nNavigate with the 6 buttons. ENTER selects, BACK returns, hold BACK for Home."},
  {"Button Navigation","navigation",
    "UP / DOWN  move selection\nLEFT / RIGHT  change value / page\nENTER  select / confirm\nBACK  back / delete\nHold BACK  jump Home\n\nHold UP/DOWN to repeat."},
  {"Wi-Fi Scanner","wifi_scanner",
    "Lists nearby 2.4GHz networks: SSID, signal, channel, lock=secured.\n\nNOTE: this radio is 2.4GHz only - 5GHz networks cannot be seen.\n\nENTER opens details (BSSID, channel, security, vendor). From there you can Connect or run an authorized test."},
  {"Device Discovery","device_discovery",
    "Shows client stations seen during a scan: MAC, packet count, last-seen.\n\nStart a Wi-Fi scan first so devices are observed. Encrypted payloads are never inspected - only frame headers."},
  {"Packet Monitor","packet_monitor",
    "Live frame counters while sniffing: management, data, control, totals, plus channel and capture time.\n\nCounts header categories only; it does not decrypt traffic."},
  {"MAC Configuration","mac_configuration",
    "For authorized lab/privacy testing.\n\nRandomize sets a locally-administered random MAC. Set Manual enters a MAC by hand. Restore returns the original interface MAC.\n\nChanging MAC needs no password; a secured network still needs its own password to join."},
  {"Storage (SD)","sd_card",
    "Optional microSD holds logs, configs and these manuals.\n\nSD shares the SPI bus with the display (separate chip-selects). If no card is present the device runs fine and uses the built-in manual."},
  {"Power & Battery","power",
    "Powered via a boost converter from a single cell.\n\nBattery % needs a sense divider wired to an ADC pin; until then the UI honestly shows external/unknown rather than a fake percentage."},
  {"Troubleshooting","troubleshooting",
    "Blank screen: check TFT CS/DC/RST wiring and try a different init tab (BLACK/GREEN/RED) in configs.h.\nWrong colors/offset: change the tab.\nNo SD: reseat card, check SD_CS=5 and shared SPI pins.\nButton dead: verify pin and GND."},
  {"Safety & Authorized Use","safety",
    "Attacks (deauth, beacon, evil-twin) are for networks you own or are authorized to test. Unauthorized use may be illegal. You are responsible for compliance."},
};
static const int N_MANUAL = sizeof(MANUALS)/sizeof(MANUALS[0]);

// ---- main menu tiles ----
static const char* MENU_LABELS[] = {"Wi-Fi","Devices","Packets","MAC","Manual","Settings","Tools","Storage"};
static const int N_MENU = sizeof(MENU_LABELS)/sizeof(MENU_LABELS[0]);

static const char* SETTINGS_LABELS[] = {"Display & Theme","Button Guide","Storage (SD)","About Device","Reset Preferences"};
static const int N_SETTINGS = sizeof(SETTINGS_LABELS)/sizeof(SETTINGS_LABELS[0]);

static const char* TOOLS_LABELS[] = {"Deauth (test)","Beacon flood","Probe flood","Evil AP (lab)"};
static const int N_TOOLS = sizeof(TOOLS_LABELS)/sizeof(TOOLS_LABELS[0]);

// ================= lifecycle =================
void TftUI::begin() {
  tft = new Adafruit_ST7735(TFT_UI_CS, TFT_UI_DC, TFT_UI_RST);
  tft->initR(TFT_UI_TAB);
  tft->setRotation(TFT_UI_ROT);
  tft->fillScreen(C_BG);
  cv = new GFXcanvas16(tft->width(), tft->height());
  if (!cv) { Serial.println(F("[TFT] canvas alloc failed")); ok=false; return; }

  #ifdef TFT_UI_BL
    pinMode(TFT_UI_BL, OUTPUT);
    ledcAttach(TFT_UI_BL, 5000, 8);
  #endif

  b_up={TUI_UP,0,0,0,0}; b_dn={TUI_DOWN,0,0,0,0}; b_lt={TUI_LEFT,0,0,0,0};
  b_rt={TUI_RIGHT,0,0,0,0}; b_en={TUI_ENTER,0,0,0,0}; b_bk={TUI_BACK,0,0,0,0};
  pinMode(TUI_UP,INPUT_PULLUP);  pinMode(TUI_DOWN,INPUT_PULLUP);
  pinMode(TUI_LEFT,INPUT_PULLUP);pinMode(TUI_RIGHT,INPUT_PULLUP);
  pinMode(TUI_ENTER,INPUT_PULLUP);pinMode(TUI_BACK,INPUT_PULLUP);

  readStaMac(mac_orig);
  memcpy(mac_shown, mac_orig, 6);

  loadPrefs();
  applyAccent();
  #ifdef TFT_UI_BL
    ledcWrite(TFT_UI_BL, map(set_brightness,0,100,10,255));
  #endif

  ok = true;
  screen = SPLASH;
  splash_until = millis() + 1400;
  Serial.println(F("[TFT] ST7735 UI ready"));
}

void TftUI::loadPrefs(){
  tui_prefs.begin("tftui", true);
  set_brightness = tui_prefs.getUChar("bri", 80);
  set_accent     = tui_prefs.getUChar("acc", 0);
  tui_prefs.end();
}
void TftUI::savePrefs(){
  tui_prefs.begin("tftui", false);
  tui_prefs.putUChar("bri", set_brightness);
  tui_prefs.putUChar("acc", set_accent);
  tui_prefs.end();
}
void TftUI::applyAccent(){
  COL_ACCENT = (set_accent==1)?C_CYAN:(set_accent==2)?C_GREEN:C_RED;
}

void TftUI::readStaMac(uint8_t* out){
  if (esp_wifi_get_mac(WIFI_IF_STA, out) != ESP_OK) memset(out,0,6);
}

// ================= buttons =================
void TftUI::pollButtons(uint32_t now){
  evUp=evDn=evLt=evRt=evEn=evBk=evBkLong=false;
  Btn* bs[6] = {&b_up,&b_dn,&b_lt,&b_rt,&b_en,&b_bk};
  bool* ev[6] = {&evUp,&evDn,&evLt,&evRt,&evEn,&evBk};
  bool rep[6] = {true,true,true,true,false,false};   // up/dn/lt/rt auto-repeat
  for (int i=0;i<6;i++){
    bool raw = (digitalRead(bs[i]->pin)==LOW);
    if (raw != bs[i]->down && (now - bs[i]->t_edge) > 15){  // debounce
      bs[i]->down = raw; bs[i]->t_edge = now;
      if (raw){ *ev[i]=true; bs[i]->t_rep = now + 400; }     // press edge
    }
    if (raw && rep[i] && now >= bs[i]->t_rep){ *ev[i]=true; bs[i]->t_rep = now + 120; }
  }
  // long-press BACK -> Home
  static uint32_t bk_since=0;
  if (b_bk.down){ if(!bk_since) bk_since=now; else if(now-bk_since>600){ evBkLong=true; bk_since=now+100000; } }
  else bk_since=0;
}

// ================= toast =================
void TftUI::showToast(const char* msg, uint8_t kind){
  strncpy(toast,msg,sizeof(toast)-1); toast[sizeof(toast)-1]=0;
  toast_kind=kind; toast_until=millis()+1600;
}

// ================= update loop =================
void TftUI::update(uint32_t now){
  if (!ok) return;
  if (screen==SPLASH && now>=splash_until) screen=HWCHECK, splash_until=now+1000;
  else if (screen==HWCHECK && now>=splash_until) screen=HOME;

  pollButtons(now);
  if (screen!=SPLASH && screen!=HWCHECK) handleInput(now);

  if ((now - last_draw) < 90) return;   // ~11 Hz
  last_draw = now; beat++;

  switch(screen){
    case SPLASH:      drawSplash(); break;
    case HWCHECK:     drawHwCheck(); break;
    case HOME:        drawHome(); break;
    case MENU:        drawMenu(); break;
    case WIFI_SCAN:   drawWifiScan(); break;
    case WIFI_DETAIL: drawWifiDetail(); break;
    case DEVICES:     drawDevices(); break;
    case DEV_DETAIL:  drawDevDetail(); break;
    case PACKETS:     drawPackets(); break;
    case MACCFG:      drawMacCfg(); break;
    case KEYBOARD:    drawKeyboard(); break;
    case SETTINGS:    drawSettings(); break;
    case DISPLAY_SET: drawDisplaySet(); break;
    case MANUAL_LIST: drawManualList(); break;
    case MANUAL_VIEW: drawManualView(); break;
    case STORAGE:     drawStorage(); break;
    case TOOLS:       drawTools(); break;
  }
  if (now < toast_until) drawToast();
  push();
}

void TftUI::push(){ tft->drawRGBBitmap(0,0, cv->getBuffer(), cv->width(), cv->height()); }

// ================= chrome =================
void TftUI::header(const char* title, bool back){
  int W=cv->width();
  cv->fillRect(0,0,W,14,C_PANEL);
  cv->drawFastHLine(0,14,W,C_LINE);
  cv->setTextSize(1); cv->setTextColor(C_FG);
  cv->setCursor(back?12:4,4);
  if(back){ cv->setTextColor(COL_ACCENT); cv->setCursor(3,4); cv->print("<"); cv->setTextColor(C_FG); cv->setCursor(12,4); }
  cv->print(title);
  drawBattery(W-18,3);
}
void TftUI::footer(const char* hint){
  int H=cv->height(), W=cv->width();
  cv->drawFastHLine(0,H-11,W,C_LINE);
  cv->setTextSize(1); cv->setTextColor(C_MUT);
  cv->setCursor(3,H-9); cv->print(hint);
}
void TftUI::drawBattery(int x,int y){
  // Honest: no sense line wired -> show a plug glyph, not a fake %.
  cv->drawRect(x,y,13,8,C_MUT); cv->fillRect(x+13,y+2,2,4,C_MUT);
  cv->drawLine(x+4,y+2,x+7,y+4,COL_ACCENT); cv->drawLine(x+7,y+4,x+4,y+6,COL_ACCENT); // bolt = ext pwr
}
void TftUI::scrollbar(int top,int count,int visible){
  if(count<=visible) return;
  int H=cv->height(), x=cv->width()-3, y0=16, h=H-28;
  cv->drawFastVLine(x,y0,h,C_LINE);
  int bh=h*visible/count; if(bh<6)bh=6;
  int by=y0+(h-bh)*top/(count-visible);
  cv->fillRect(x-1,by,3,bh,COL_ACCENT);
}

// ================= labels =================
const char* TftUI::secLabel(uint8_t sec){
  String s = wifi_scan_obj.security_int_to_string(sec);
  s.replace("[",""); s.replace("]","");
  static char buf[20]; strncpy(buf,s.c_str(),sizeof(buf)-1); buf[sizeof(buf)-1]=0; return buf;
}
const char* TftUI::modeLabel(uint8_t mode){
  if(mode==WIFI_SCAN_OFF) return "Idle";
  if(wifi_scan_obj.scanning()) return "Active";
  return "Idle";
}
bool TftUI::sdReady(){ return sd_obj.supported; }

// ================= screens: splash / hwcheck =================
void TftUI::drawSplash(){
  cv->fillScreen(C_BG);
  int W=cv->width();
  // dotted "M" mark
  cv->setTextSize(3); cv->setTextColor(COL_ACCENT);
  cv->setCursor(W/2-26, 44); cv->print("M");
  cv->setTextSize(2); cv->setTextColor(C_FG);
  cv->setCursor(W/2-45, 78); cv->print("MARAUDER");
  cv->setTextSize(1); cv->setTextColor(C_MUT);
  cv->setCursor(W/2-30, 100); cv->print(MARAUDER_VERSION);
  // progress
  uint32_t t = 1400-(splash_until>millis()?splash_until-millis():0);
  int w=map((int)min(t,(uint32_t)1400),0,1400,0,W-40);
  cv->drawRect(20,120,W-40,6,C_LINE); cv->fillRect(20,120,w,6,COL_ACCENT);
  push();
}
void TftUI::drawHwCheck(){
  cv->fillScreen(C_BG);
  header("System Init", false);
  const char* items[4]={"Display (TFT)","SD Card","Wi-Fi (2.4GHz)","Modules"};
  bool oks[4]={ true, sdReady(), true, true };
  cv->setTextSize(1);
  for(int i=0;i<4;i++){
    int y=26+i*18;
    cv->setTextColor(oks[i]?C_GREEN:C_AMBER);
    cv->setCursor(8,y); cv->print(oks[i]?"[OK]":"[--]");
    cv->setTextColor(C_FG); cv->setCursor(42,y); cv->print(items[i]);
  }
  cv->setTextColor(C_MUT); cv->setCursor(8,120); cv->print("Starting...");
  push();
}

// ================= HOME =================
void TftUI::drawHome(){
  cv->fillScreen(C_BG);
  int W=cv->width();
  header("MARAUDER", false);
  // big status: uptime (no RTC -> honest uptime, not a fake clock)
  uint32_t up=millis()/1000;
  char t[16]; snprintf(t,sizeof(t),"%lu:%02lu:%02lu",(unsigned long)(up/3600),(unsigned long)((up/60)%60),(unsigned long)(up%60));
  cv->setTextSize(2); cv->setTextColor(C_FG);
  cv->setCursor((W-strlen(t)*12)/2, 30); cv->print(t);
  cv->setTextSize(1); cv->setTextColor(C_MUT);
  cv->setCursor((W-8*6)/2, 50); cv->print("uptime");
  // info rows
  cv->drawFastHLine(8,64,W-16,C_LINE);
  char l[24];
  cv->setTextColor(C_MUT); cv->setCursor(8,72); cv->print("Channel");
  cv->setTextColor(C_FG);  cv->setCursor(80,72); snprintf(l,sizeof(l),"CH %u",wifi_scan_obj.set_channel); cv->print(l);
  cv->setTextColor(C_MUT); cv->setCursor(8,84); cv->print("Mode");
  cv->setTextColor(wifi_scan_obj.scanning()?C_GREEN:C_MUT); cv->setCursor(80,84); cv->print(wifi_scan_obj.scanning()?"ACTIVE":"IDLE");
  cv->setTextColor(C_MUT); cv->setCursor(8,96); cv->print("Storage");
  cv->setTextColor(sdReady()?C_FG:C_AMBER); cv->setCursor(80,96); cv->print(sdReady()?"SD OK":"No SD");
  cv->setTextColor(C_MUT); cv->setCursor(8,108); cv->print("Networks");
  cv->setTextColor(C_FG);  cv->setCursor(80,108); snprintf(l,sizeof(l),"%d",access_points?access_points->size():0); cv->print(l);
  footer("ENTER menu");
}

// ================= MENU grid =================
void TftUI::drawMenu(){
  cv->fillScreen(C_BG);
  header("Main Menu", true);
  int W=cv->width();
  int cols=2, cw=W/2, ch=30, y0=18;
  for(int i=0;i<N_MENU;i++){
    int cx=(i%cols)*cw, cy=y0+(i/cols)*ch;
    bool sel=(i==menu_idx);
    cv->drawRect(cx+4,cy+2,cw-8,ch-4, sel?COL_ACCENT:C_LINE);
    if(sel) cv->drawRect(cx+5,cy+3,cw-10,ch-6,COL_ACCENT);
    cv->setTextSize(1); cv->setTextColor(sel?C_FG:C_MUT);
    cv->setCursor(cx+10,cy+ch/2-3); cv->print(MENU_LABELS[i]);
  }
  footer("ENTER select  BACK home");
}

// ================= WIFI SCAN =================
void TftUI::drawWifiScan(){
  cv->fillScreen(C_BG);
  int count=access_points?access_points->size():0;
  char h[22]; snprintf(h,sizeof(h),"Wi-Fi (%d)",count);
  header(h, true);
  cv->setTextSize(1);
  if(count==0){
    cv->setTextColor(C_MUT); cv->setCursor(8,40);
    cv->print(wifi_scan_obj.scanning()?"Scanning CH 1-14...":"No networks yet");
    cv->setCursor(8,54); cv->print("2.4GHz only");
  }
  int rows=7;
  for(int r=0;r<rows && (list_top+r)<count;r++){
    int i=list_top+r, y=18+r*14; bool sel=(i==list_idx);
    AccessPoint ap=access_points->get(i);
    if(sel){ cv->fillRect(0,y-1,cv->width()-3,13,C_PANEL); cv->drawRect(0,y-1,cv->width()-3,13,COL_ACCENT); }
    cv->setTextColor(sel?C_FG:C_MUT); cv->setCursor(4,y+2);
    String e=ap.essid; if(e.length()==0)e="(hidden)"; if(e.length()>12)e=e.substring(0,12);
    cv->print(e);
    if(ap.sec!=0){ cv->setTextColor(C_AMBER); cv->setCursor(78,y+2); cv->print("L"); }
    cv->setTextColor(sel?C_FG:C_MUT); cv->setCursor(90,y+2);
    char rs[8]; snprintf(rs,sizeof(rs),"%d",ap.rssi); cv->print(rs);
  }
  scrollbar(list_top,count,rows);
  footer("ENTER details  BACK stop");
}
void TftUI::drawWifiDetail(){
  cv->fillScreen(C_BG);
  header("Network", true);
  if(sel_ap<0 || !access_points || sel_ap>=access_points->size()){ footer("BACK"); return; }
  AccessPoint ap=access_points->get(sel_ap);
  cv->setTextSize(1); int y=20; char l[40];
  String e=ap.essid; if(e.length()==0)e="(hidden)";
  cv->setTextColor(C_FG); cv->setCursor(4,y); cv->print(e); y+=12;
  cv->setTextColor(C_MUT); cv->setCursor(4,y); cv->print("BSSID"); cv->setTextColor(C_FG);
  snprintf(l,sizeof(l),"%02X:%02X:%02X:%02X:%02X:%02X",ap.bssid[0],ap.bssid[1],ap.bssid[2],ap.bssid[3],ap.bssid[4],ap.bssid[5]);
  cv->setCursor(4,y+9); cv->print(l); y+=20;
  int freq=2407+ap.channel*5; if(ap.channel==14)freq=2484;
  cv->setTextColor(C_MUT);cv->setCursor(4,y);cv->print("Chan"); cv->setTextColor(C_FG);cv->setCursor(48,y);
  snprintf(l,sizeof(l),"%u (%d MHz)",ap.channel,freq); cv->print(l); y+=11;
  cv->setTextColor(C_MUT);cv->setCursor(4,y);cv->print("RSSI"); cv->setTextColor(C_FG);cv->setCursor(48,y);
  snprintf(l,sizeof(l),"%d dBm",ap.rssi); cv->print(l); y+=11;
  cv->setTextColor(C_MUT);cv->setCursor(4,y);cv->print("Sec"); cv->setTextColor(ap.sec?C_AMBER:C_GREEN);cv->setCursor(48,y);
  cv->print(secLabel(ap.sec)); y+=11;
  cv->setTextColor(C_MUT);cv->setCursor(4,y);cv->print("Vendor"); cv->setTextColor(C_FG);cv->setCursor(48,y);
  { String m=ap.man; if(m.length()==0)m="unknown"; if(m.length()>12)m=m.substring(0,12); cv->print(m); } y+=14;
  // actions
  const char* acts[2]={"Connect","Attack / Test"};
  for(int i=0;i<2;i++){
    bool sel=(detail_idx==i); int ay=y+i*14;
    cv->drawRect(4,ay,cv->width()-10,12, sel?COL_ACCENT:C_LINE);
    cv->setTextColor(sel?C_FG:C_MUT); cv->setCursor(10,ay+3); cv->print(acts[i]);
  }
  footer("ENTER  BACK");
}

// ================= DEVICES =================
void TftUI::drawDevices(){
  cv->fillScreen(C_BG);
  int count=stations?stations->size():0;
  char h[22]; snprintf(h,sizeof(h),"Devices (%d)",count); header(h,true);
  cv->setTextSize(1);
  if(count==0){ cv->setTextColor(C_MUT); cv->setCursor(8,40); cv->print("Scan Wi-Fi first"); }
  int rows=7;
  for(int r=0;r<rows && (list_top+r)<count;r++){
    int i=list_top+r,y=18+r*14; bool sel=(i==list_idx);
    Station st=stations->get(i);
    if(sel){ cv->fillRect(0,y-1,cv->width()-3,13,C_PANEL); cv->drawRect(0,y-1,cv->width()-3,13,COL_ACCENT); }
    cv->setTextColor(sel?C_FG:C_MUT); cv->setCursor(4,y+2);
    char l[24]; snprintf(l,sizeof(l),"%02X:%02X:%02X:%02X",st.mac[2],st.mac[3],st.mac[4],st.mac[5]); cv->print(l);
    cv->setCursor(96,y+2); snprintf(l,sizeof(l),"%u",st.packets); cv->print(l);
  }
  scrollbar(list_top,count,rows);
  footer("ENTER details  BACK");
}
void TftUI::drawDevDetail(){
  cv->fillScreen(C_BG); header("Device", true);
  if(sel_sta<0||!stations||sel_sta>=stations->size()){footer("BACK");return;}
  Station st=stations->get(sel_sta);
  cv->setTextSize(1); char l[40]; int y=24;
  cv->setTextColor(C_MUT);cv->setCursor(4,y);cv->print("MAC"); cv->setTextColor(C_FG);cv->setCursor(4,y+10);
  snprintf(l,sizeof(l),"%02X:%02X:%02X:%02X:%02X:%02X",st.mac[0],st.mac[1],st.mac[2],st.mac[3],st.mac[4],st.mac[5]);
  cv->print(l); y+=24;
  cv->setTextColor(C_MUT);cv->setCursor(4,y);cv->print("OUI"); cv->setTextColor(C_FG);cv->setCursor(48,y);
  snprintf(l,sizeof(l),"%02X:%02X:%02X",st.mac[0],st.mac[1],st.mac[2]); cv->print(l); y+=12;
  cv->setTextColor(C_MUT);cv->setCursor(4,y);cv->print("Packets");cv->setTextColor(C_FG);cv->setCursor(64,y);
  snprintf(l,sizeof(l),"%u",st.packets); cv->print(l); y+=12;
  cv->setTextColor(C_MUT);cv->setCursor(4,y);cv->print("Seen");cv->setTextColor(C_FG);cv->setCursor(64,y);
  snprintf(l,sizeof(l),"%lus ago",(unsigned long)((millis()-st.last_seen_ms)/1000)); cv->print(l);
  footer("BACK");
}

// ================= PACKETS =================
void TftUI::drawPackets(){
  cv->fillScreen(C_BG); header("Packet Monitor", true);
  cv->setTextSize(2); cv->setTextColor(C_FG);
  char l[20]; snprintf(l,sizeof(l),"%lu",(unsigned long)oled_frame_count);
  cv->setCursor(6,20); cv->print(l);
  cv->setTextSize(1); cv->setTextColor(C_MUT); cv->setCursor(8,40); cv->print("frames");
  struct{const char*n;volatile uint32_t*v;uint16_t c;} rows[3]={
    {"Mgmt",&ac_mgmt,C_CYAN},{"Data",&ac_data,C_GREEN},{"Ctrl",&ac_ctrl,C_AMBER}};
  for(int i=0;i<3;i++){ int y=58+i*13;
    cv->setTextColor(rows[i].c); cv->setCursor(8,y); cv->print(rows[i].n);
    cv->setTextColor(C_FG); cv->setCursor(64,y); snprintf(l,sizeof(l),"%lu",(unsigned long)*rows[i].v); cv->print(l);
  }
  // mini bar graph of totals mod
  int base=112, W=cv->width();
  cv->drawFastHLine(4,base,W-8,C_LINE);
  for(int x=0;x<W-10;x+=3){ int bh=(oled_frame_count*7+x*5)%18; cv->drawFastVLine(5+x,base-bh,bh,COL_ACCENT); }
  cv->setTextColor(C_MUT); cv->setCursor(4,base+4);
  snprintf(l,sizeof(l),"CH%u  %lus",wifi_scan_obj.set_channel,(unsigned long)((millis()-pkt_t0)/1000)); cv->print(l);
  footer("BACK stop");
}

// ================= MAC CONFIG =================
void TftUI::drawMacCfg(){
  cv->fillScreen(C_BG); header("MAC Config", true);
  cv->setTextSize(1); cv->setTextColor(C_MUT); cv->setCursor(6,20); cv->print("Current STA MAC");
  char l[20]; snprintf(l,sizeof(l),"%02X:%02X:%02X:%02X:%02X:%02X",mac_shown[0],mac_shown[1],mac_shown[2],mac_shown[3],mac_shown[4],mac_shown[5]);
  cv->drawRect(6,30,cv->width()-12,14,C_LINE);
  cv->setTextColor(C_FG); cv->setCursor(12,34); cv->print(l);
  const char* acts[3]={"Randomize","Set Manual","Restore Original"};
  for(int i=0;i<3;i++){ bool sel=(detail_idx==i); int y=52+i*16;
    cv->drawRect(6,y,cv->width()-12,14, sel?COL_ACCENT:C_LINE);
    cv->setTextColor(sel?C_FG:C_MUT); cv->setCursor(12,y+3); cv->print(acts[i]);
  }
  footer("ENTER  BACK");
}

// ================= KEYBOARD =================
void TftUI::openKeyboard(const char* title,bool mask,bool hex,Screen ret){
  kb_title=title; kb_mask=mask; kb_hex=hex; kb_return=ret;
  kb_len=0; kb_buf[0]=0; kb_row=0; kb_col=0; kb_page=hex?0:0;
  screen=KEYBOARD;
}
static const char** kbRows(bool hex,int page,int* nrows){
  if(hex){ *nrows=2; return KB_HEX; }
  *nrows=3;
  switch(page){case 1:return KB_UPR;case 2:return KB_NUM;case 3:return KB_SYM;default:return KB_ABC;}
}
void TftUI::drawKeyboard(){
  cv->fillScreen(C_BG);
  header(kb_title, true);
  // text field
  cv->drawRect(4,16,cv->width()-8,16,C_LINE);
  cv->setTextSize(1); cv->setTextColor(C_FG); cv->setCursor(8,20);
  char disp[40]; int n=min(kb_len,(int)sizeof(disp)-1);
  for(int i=0;i<n;i++) disp[i]= (kb_mask && !kb_hex)?'*':kb_buf[i];
  disp[n]=0; cv->print(disp);
  if((beat/4)&1){ cv->print("_"); }
  int nrows; const char** rows=kbRows(kb_hex,kb_page,&nrows);
  int y0=40, kh=16;
  // char rows
  for(int r=0;r<nrows;r++){
    int len=strlen(rows[r]); int kw=cv->width()/max(len,1);
    for(int c=0;c<len;c++){
      bool sel=(kb_row==r && kb_col==c);
      int x=c*kw, y=y0+r*kh;
      if(sel) cv->fillRect(x,y,kw-1,kh-1,COL_ACCENT);
      cv->setTextColor(sel?C_BG:C_FG); cv->setCursor(x+kw/2-3,y+4); cv->print(rows[r][c]);
    }
  }
  // control row
  int cry=nrows; int yC=y0+cry*kh;
  const char* ctl[5]; int nctl;
  if(kb_hex){ const char* c2[3]={"DEL","OK","X"}; nctl=3; for(int i=0;i<3;i++)ctl[i]=c2[i]; }
  else { const char* c5[5]={kb_page==0?"ABC":kb_page==1?"123":kb_page==2?"#$%":"abc","SPC","DEL","OK","X"}; nctl=5; for(int i=0;i<5;i++)ctl[i]=c5[i]; }
  int cw=cv->width()/nctl;
  for(int i=0;i<nctl;i++){ bool sel=(kb_row==cry && kb_col==i); int x=i*cw;
    cv->drawRect(x,yC,cw-1,14, sel?COL_ACCENT:C_LINE);
    if(sel) cv->fillRect(x+1,yC+1,cw-3,12,C_PANEL);
    cv->setTextColor(sel?COL_ACCENT:C_MUT); cv->setCursor(x+3,yC+3); cv->print(ctl[i]);
  }
  footer("ENTER key  BACK del");
}
void TftUI::kbInsertCurrent(){
  int nrows; const char** rows=kbRows(kb_hex,kb_page,&nrows);
  if(kb_row<nrows){
    int len=strlen(rows[kb_row]); if(kb_col<len && kb_len<(int)sizeof(kb_buf)-1){
      kb_buf[kb_len++]=rows[kb_row][kb_col]; kb_buf[kb_len]=0;
    }
    return;
  }
  // control row
  if(kb_hex){
    if(kb_col==0){ if(kb_len>0)kb_buf[--kb_len]=0; }           // DEL
    else if(kb_col==1){ applyManualMac(); }                    // OK
    else { screen=kb_return; }                                 // X
  } else {
    if(kb_col==0){ kb_page=(kb_page+1)%4; kb_row=0; kb_col=0; }  // page cycle
    else if(kb_col==1){ if(kb_len<(int)sizeof(kb_buf)-1){kb_buf[kb_len++]=' ';kb_buf[kb_len]=0;} } // SPACE
    else if(kb_col==2){ if(kb_len>0)kb_buf[--kb_len]=0; }       // DEL
    else if(kb_col==3){ applyPassword(); }                     // OK
    else { screen=kb_return; }                                 // X cancel
  }
}

// ================= SETTINGS =================
void TftUI::drawSettings(){
  cv->fillScreen(C_BG); header("Settings", true);
  cv->setTextSize(1);
  for(int i=0;i<N_SETTINGS;i++){ int y=20+i*16; bool sel=(settings_idx==i);
    if(sel){ cv->fillRect(0,y-2,cv->width()-3,15,C_PANEL); cv->drawRect(0,y-2,cv->width()-3,15,COL_ACCENT); }
    cv->setTextColor(sel?C_FG:C_MUT); cv->setCursor(8,y+1); cv->print(SETTINGS_LABELS[i]);
  }
  footer("ENTER  BACK");
}
void TftUI::drawDisplaySet(){
  cv->fillScreen(C_BG); header("Display & Theme", true);
  cv->setTextSize(1);
  // brightness
  cv->setTextColor(detail_idx==0?C_FG:C_MUT); cv->setCursor(8,24); cv->print("Brightness");
  cv->drawRect(8,36,100,8,C_LINE); cv->fillRect(8,36,set_brightness,8,COL_ACCENT);
  char l[8]; snprintf(l,sizeof(l),"%d%%",set_brightness); cv->setTextColor(C_FG); cv->setCursor(112,36); cv->print(l);
  #ifndef TFT_UI_BL
    cv->setTextColor(C_AMBER); cv->setCursor(8,48); cv->print("(backlight on 3V3: fixed)");
  #endif
  // accent
  cv->setTextColor(detail_idx==1?C_FG:C_MUT); cv->setCursor(8,66); cv->print("Accent");
  const char* an[3]={"Red","Cyan","Green"}; uint16_t ac[3]={C_RED,C_CYAN,C_GREEN};
  for(int i=0;i<3;i++){ int x=8+i*38,y=78; bool s=(set_accent==i);
    cv->fillRect(x,y,30,14,ac[i]); if(s){cv->drawRect(x-1,y-1,32,16,C_FG);}
    cv->setTextColor(C_BG); cv->setCursor(x+2,y+4); cv->print(an[i]);
  }
  cv->setTextColor(C_MUT); cv->setCursor(8,104); cv->print("UP/DN pick row");
  cv->setCursor(8,114); cv->print("LEFT/RIGHT change");
  footer("BACK save");
}

// ================= MANUAL =================
void TftUI::drawManualList(){
  cv->fillScreen(C_BG);
  char h[20]; snprintf(h,sizeof(h),"Manual %s", sdReady()?"(SD)":"");
  header(h, true); cv->setTextSize(1);
  int rows=7;
  for(int r=0;r<rows && (list_top+r)<N_MANUAL;r++){ int i=list_top+r,y=18+r*14; bool sel=(i==list_idx);
    if(sel){ cv->fillRect(0,y-1,cv->width()-3,13,C_PANEL); cv->drawRect(0,y-1,cv->width()-3,13,COL_ACCENT);}
    cv->setTextColor(sel?C_FG:C_MUT); cv->setCursor(6,y+2); cv->print(MANUALS[i].title);
  }
  scrollbar(list_top,N_MANUAL,rows);
  footer("ENTER read  BACK");
}
void TftUI::drawManualView(){
  cv->fillScreen(C_BG);
  header(MANUALS[manual_idx].title, true);
  cv->setTextSize(1); cv->setTextColor(C_FG);
  // Prefer SD override, else built-in body. Word-wrap to width, apply scroll.
  String body;
  char path[48]; snprintf(path,sizeof(path),"/marauder/manual/%s.md",MANUALS[manual_idx].file);
  if(sdReady() && SD.exists(path)){
    File f=SD.open(path); if(f){ while(f.available() && body.length()<1600) body+=(char)f.read(); f.close(); }
  }
  if(body.length()==0) body = MANUALS[manual_idx].body;
  int W=cv->width(), maxc=(W-8)/6, y=18-manual_scroll*10, lineh=10;
  int col=0; String word;
  cv->setCursor(4,y);
  for(uint16_t i=0;i<=body.length();i++){
    char c = i<body.length()? body[i] : '\n';
    if(c==' '||c=='\n'){
      if(col+(int)word.length()>maxc){ y+=lineh; col=0; cv->setCursor(4,y); }
      if(y>12 && y<cv->height()-12){ cv->setCursor(4+col*6,y); cv->print(word); }
      col+=word.length(); word="";
      if(c=='\n'){ y+=lineh; col=0; } else { col+=1; }
    } else word+=c;
  }
  footer("UP/DN scroll  BACK");
}

// ================= STORAGE =================
void TftUI::drawStorage(){
  cv->fillScreen(C_BG); header("Storage", true); cv->setTextSize(1);
  if(!sdReady()){
    cv->setTextColor(C_AMBER); cv->setCursor(8,40); cv->print("No SD card");
    cv->setTextColor(C_MUT); cv->setCursor(8,56); cv->print("Insert a card and");
    cv->setCursor(8,66); cv->print("wire SD_CS=5 (shared");
    cv->setCursor(8,76); cv->print("SPI). Device runs");
    cv->setCursor(8,86); cv->print("fine without it.");
    footer("BACK"); return;
  }
  char l[28];
  cv->setTextColor(C_MUT);cv->setCursor(8,24);cv->print("Card size");
  cv->setTextColor(C_FG);cv->setCursor(80,24); snprintf(l,sizeof(l),"%lu MB",(unsigned long)sd_obj.cardSizeMB); cv->print(l);
  uint64_t used=SD.usedBytes()/(1024*1024), tot=SD.totalBytes()/(1024*1024);
  cv->setTextColor(C_MUT);cv->setCursor(8,38);cv->print("Used");
  cv->setTextColor(C_FG);cv->setCursor(80,38); snprintf(l,sizeof(l),"%lu MB",(unsigned long)used); cv->print(l);
  cv->setTextColor(C_MUT);cv->setCursor(8,52);cv->print("Free");
  cv->setTextColor(C_FG);cv->setCursor(80,52); snprintf(l,sizeof(l),"%lu MB",(unsigned long)(tot>used?tot-used:0)); cv->print(l);
  cv->drawFastHLine(8,64,cv->width()-16,C_LINE);
  cv->setTextColor(C_MUT); cv->setCursor(8,70); cv->print("Files /:");
  File root=SD.open("/"); int yy=82,cnt=0;
  if(root){ File fe=root.openNextFile();
    while(fe && cnt<5){ cv->setTextColor(C_FG); cv->setCursor(12,yy); String n=fe.name(); if(n.length()>18)n=n.substring(0,18); cv->print(n); yy+=11; cnt++; fe=root.openNextFile(); }
    root.close();
  }
  footer("BACK");
}

// ================= TOOLS =================
void TftUI::drawTools(){
  cv->fillScreen(C_BG); header("Tools (lab)", true); cv->setTextSize(1);
  for(int i=0;i<N_TOOLS;i++){ int y=22+i*18; bool sel=(tools_idx==i);
    if(sel){ cv->fillRect(0,y-2,cv->width()-3,16,C_PANEL); cv->drawRect(0,y-2,cv->width()-3,16,COL_ACCENT);}
    cv->setTextColor(sel?C_FG:C_MUT); cv->setCursor(8,y+2); cv->print(TOOLS_LABELS[i]);
  }
  cv->setTextColor(C_MUT); cv->setCursor(6,106); cv->print("Authorized testing");
  cv->setCursor(6,116); cv->print("only.");
  footer("ENTER run  BACK");
}

// ================= toast =================
void TftUI::drawToast(){
  int W=cv->width(), h=18, y=cv->height()/2-9;
  uint16_t c = toast_kind==1?C_GREEN:toast_kind==2?C_AMBER:toast_kind==3?C_RED:C_CYAN;
  cv->fillRect(6,y,W-12,h,C_PANEL); cv->drawRect(6,y,W-12,h,c);
  cv->setTextSize(1); cv->setTextColor(C_FG); cv->setCursor(12,y+6); cv->print(toast);
}

// ================= actions =================
void TftUI::applyPassword(){
  if(sel_ap<0){ showToast("no network",2); screen=kb_return; return; }
  cli_obj.runCommand(String("join -a ")+sel_ap+" -p "+kb_buf);
  showToast("joining...",0);
  memset(kb_buf,0,sizeof(kb_buf)); kb_len=0;   // don't keep the password around
  screen=WIFI_DETAIL;
}
static uint8_t hexNibble(char c){
  if(c>='0'&&c<='9')return c-'0';
  if(c>='a'&&c<='f')return c-'a'+10;
  if(c>='A'&&c<='F')return c-'A'+10;
  return 0;
}
void TftUI::applyManualMac(){
  // parse up to 12 hex chars from kb_buf -> 6 bytes (ignores ':' and spaces)
  uint8_t m[6]={0}; int b=0, hi=0; uint8_t cur=0;
  for(int i=0;i<kb_len && b<6;i++){ char c=kb_buf[i]; if(!isxdigit((int)c))continue;
    cur=(cur<<4)|hexNibble(c); hi++;
    if(hi==2){ m[b++]=cur; cur=0; hi=0; } }
  if(b<6){ showToast("need 12 hex",2); return; }
  m[0]=(m[0]&0xFE)|0x02;   // force locally-administered, unicast
  esp_err_t e=esp_wifi_set_mac(WIFI_IF_STA,m);
  if(e==ESP_OK){ memcpy(mac_shown,m,6); showToast("MAC set",1); }
  else showToast("set failed",3);
  screen=MACCFG;
}
void TftUI::randomizeMac(){
  uint8_t m[6]; for(int i=0;i<6;i++)m[i]=esp_random()&0xFF;
  m[0]=(m[0]&0xFE)|0x02;
  esp_err_t e=esp_wifi_set_mac(WIFI_IF_STA,m);
  if(e==ESP_OK){ memcpy(mac_shown,m,6); showToast("randomized",1); }
  else showToast("set failed",3);
}
void TftUI::restoreMac(){
  esp_err_t e=esp_wifi_set_mac(WIFI_IF_STA,mac_orig);
  if(e==ESP_OK){ memcpy(mac_shown,mac_orig,6); showToast("restored",1); }
  else showToast("restore failed",3);
}

// ================= input routing =================
void TftUI::handleInput(uint32_t now){
  if(evBkLong && screen!=KEYBOARD){ screen=HOME; return; }
  switch(screen){
    case HOME:
      if(evEn) screen=MENU;
      break;
    case MENU:
      if(evLt&&menu_idx>0)menu_idx--;
      if(evRt&&menu_idx<N_MENU-1)menu_idx++;
      if(evUp&&menu_idx>=2)menu_idx-=2;
      if(evDn&&menu_idx+2<N_MENU)menu_idx+=2;
      if(evBk)screen=HOME;
      if(evEn){ list_idx=0;list_top=0;detail_idx=0;
        switch(menu_idx){
          case 0: screen=WIFI_SCAN; if(!wifi_scan_obj.scanning())cli_obj.runCommand("scanall"); break;
          case 1: screen=DEVICES; if(!wifi_scan_obj.scanning())cli_obj.runCommand("scanall"); break;
          case 2: screen=PACKETS; pkt_t0=now; oled_frame_count=0; ac_mgmt=ac_data=ac_ctrl=ac_beacon=ac_probe=ac_deauth=0; cli_obj.runCommand("sniffraw"); break;
          case 3: readStaMac(mac_shown); screen=MACCFG; break;
          case 4: screen=MANUAL_LIST; break;
          case 5: settings_idx=0; screen=SETTINGS; break;
          case 6: screen=TOOLS; break;
          case 7: if(!sdReady())sd_obj.initSD(); screen=STORAGE; break;
        }
      }
      break;
    case WIFI_SCAN:{
      int count=access_points?access_points->size():0;
      if(evUp&&list_idx>0){list_idx--; if(list_idx<list_top)list_top=list_idx;}
      if(evDn&&list_idx<count-1){list_idx++; if(list_idx>=list_top+7)list_top=list_idx-6;}
      if(evBk){ cli_obj.runCommand("stopscan"); screen=MENU; }
      if(evEn&&count>0){ sel_ap=list_idx; detail_idx=0; screen=WIFI_DETAIL; }
      break; }
    case WIFI_DETAIL:
      if(evUp&&detail_idx>0)detail_idx--;
      if(evDn&&detail_idx<1)detail_idx++;
      if(evBk)screen=WIFI_SCAN;
      if(evEn){ if(detail_idx==0) openKeyboard("Enter Password",true,false,WIFI_DETAIL);
                else { cli_obj.runCommand(String("select -a ")+sel_ap); screen=TOOLS; showToast("target set",0);} }
      break;
    case DEVICES:{
      int count=stations?stations->size():0;
      if(evUp&&list_idx>0){list_idx--;if(list_idx<list_top)list_top=list_idx;}
      if(evDn&&list_idx<count-1){list_idx++;if(list_idx>=list_top+7)list_top=list_idx-6;}
      if(evBk){ cli_obj.runCommand("stopscan"); screen=MENU; }
      if(evEn&&count>0){ sel_sta=list_idx; screen=DEV_DETAIL; }
      break; }
    case DEV_DETAIL: if(evBk)screen=DEVICES; break;
    case PACKETS: if(evBk){ cli_obj.runCommand("stopscan"); screen=MENU; } break;
    case MACCFG:
      if(evUp&&detail_idx>0)detail_idx--;
      if(evDn&&detail_idx<2)detail_idx++;
      if(evBk)screen=MENU;
      if(evEn){ if(detail_idx==0)randomizeMac();
                else if(detail_idx==1)openKeyboard("Set MAC (hex)",false,true,MACCFG);
                else restoreMac(); }
      break;
    case KEYBOARD:
      { int nrows; kbRows(kb_hex,kb_page,&nrows); int totrows=nrows+1;
        if(evUp&&kb_row>0)kb_row--;
        if(evDn&&kb_row<totrows-1)kb_row++;
        if(evLt&&kb_col>0)kb_col--;
        if(evRt)kb_col++;
        // clamp col to row length
        int rowlen; if(kb_row<nrows){const char**rr=kbRows(kb_hex,kb_page,&nrows); rowlen=strlen(rr[kb_row]);}
        else rowlen = kb_hex?3:5;
        if(kb_col>=rowlen)kb_col=rowlen-1; if(kb_col<0)kb_col=0;
        if(evEn)kbInsertCurrent();
        if(evBk){ if(kb_len>0){kb_buf[--kb_len]=0;} else screen=kb_return; }
      }
      break;
    case SETTINGS:
      if(evUp&&settings_idx>0)settings_idx--;
      if(evDn&&settings_idx<N_SETTINGS-1)settings_idx++;
      if(evBk)screen=MENU;
      if(evEn){ switch(settings_idx){
        case 0: detail_idx=0; screen=DISPLAY_SET; break;
        case 1: showToast("UP/DN ENT BACK",0); break;
        case 2: if(!sdReady())sd_obj.initSD(); screen=STORAGE; break;
        case 3: { char m[40]; snprintf(m,sizeof(m),"%s heap %luk",MARAUDER_VERSION,(unsigned long)(ESP.getFreeHeap()/1024)); showToast(m,0);} break;
        case 4: set_brightness=80;set_accent=0;applyAccent();savePrefs();showToast("prefs reset",1); break;
      } }
      break;
    case DISPLAY_SET:
      if(evUp&&detail_idx>0)detail_idx--;
      if(evDn&&detail_idx<1)detail_idx++;
      if(detail_idx==0){ if(evLt&&set_brightness>0)set_brightness-=10; if(evRt&&set_brightness<100)set_brightness+=10;
        #ifdef TFT_UI_BL
          ledcWrite(TFT_UI_BL, map(set_brightness,0,100,10,255));
        #endif
      } else { if(evLt&&set_accent>0)set_accent--; if(evRt&&set_accent<2)set_accent++; applyAccent(); }
      if(evBk){ savePrefs(); screen=SETTINGS; }
      break;
    case MANUAL_LIST:
      if(evUp&&list_idx>0){list_idx--;if(list_idx<list_top)list_top=list_idx;}
      if(evDn&&list_idx<N_MANUAL-1){list_idx++;if(list_idx>=list_top+7)list_top=list_idx-6;}
      if(evBk)screen=MENU;
      if(evEn){ manual_idx=list_idx; manual_scroll=0; screen=MANUAL_VIEW; }
      break;
    case MANUAL_VIEW:
      if(evUp&&manual_scroll>0)manual_scroll--;
      if(evDn)manual_scroll++;
      if(evBk)screen=MANUAL_LIST;
      break;
    case STORAGE: if(evBk)screen=MENU; break;
    case TOOLS:
      if(evUp&&tools_idx>0)tools_idx--;
      if(evDn&&tools_idx<N_TOOLS-1)tools_idx++;
      if(evBk)screen=MENU;
      if(evEn){ switch(tools_idx){
        case 0: if(!wifi_scan_obj.filterActive())showToast("pick network 1st",2); else {cli_obj.runCommand("attack -t deauth");showToast("deauth running",3);} break;
        case 1: cli_obj.runCommand("attack -t beacon -r"); showToast("beacon flood",3); break;
        case 2: if(!wifi_scan_obj.filterActive())showToast("pick network 1st",2); else {cli_obj.runCommand("attack -t probe");showToast("probe flood",3);} break;
        case 3: showToast("needs SD portal",2); break;
      } }
      break;
    default: if(evBk)screen=HOME; break;
  }
}

#endif // HAS_TFT_UI
