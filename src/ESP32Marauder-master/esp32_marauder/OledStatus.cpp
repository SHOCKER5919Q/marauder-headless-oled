#include "OledStatus.h"

#ifdef HAS_OLED_STATUS

#include <Wire.h>
#include "WiFiScan.h"
#include "CommandLine.h"
#include "Switches.h"
#include "EvilPortal.h"   // AccessPoint struct

// Globals owned by the sketch; we drive/read them.
extern CommandLine cli_obj;
extern WiFiScan wifi_scan_obj;
extern LinkedList<AccessPoint>* access_points;
extern LinkedList<Station>* stations;
extern LinkedList<BleDevice>* ble_devices;
volatile uint32_t oled_frame_count = 0;  // defined here; Buffer::append() increments it
extern Switches u_btn;
extern Switches c_btn;
extern Switches d_btn;

// ---- screens ----
enum { SPLASH=0, DASH, MAIN, FEAT, CONFIRMS, RUNNINGS, LISTS, CHANS };

// ---- item flags ----
#define F_SCAN     0x01  // after run, show RUNNING screen
#define F_TX       0x02  // confirm before running (offensive / identity / risky)
#define F_TARGET   0x04  // pick AP index, append " <idx>" to cmd
#define F_SELTGT   0x08  // pick AP index, run "select -a <idx>" then cmd
#define F_CHAN     0x10  // channel picker, runs "channel -s <n>"
#define F_TEXT     0x20  // needs typed text -> not possible on 3 buttons
#define F_LISTV    0x40  // open the scanned-AP list view (no command)

struct Item { const char* label; const char* cmd; uint8_t flags; };

// ===== Feature tables (CORRECT commands/syntax) =====
static const Item I_SCAN[] = {
  {"scanall",      "scanall",       F_SCAN},
  {"recon wifi",   "recon wifi",    F_SCAN},
  {"recon ble",    "recon ble",     F_SCAN},
  {"AP list view", "",              F_LISTV},
  {"select AP",    "select -a",     F_SELTGT},   // select -a <idx>
  {"info AP",      "info -a",       F_TARGET},
  {"mactrack",     "mactrack",      F_SCAN},
  {"packetcount",  "packetcount",   F_SCAN},
  {"set channel",  "channel -s",    F_CHAN},
  {"foxhunt AP",   "foxhunt -w",    F_TARGET},
};
static const Item I_DEF[] = {
  {"sniffdeauth",    "sniffdeauth",    F_SCAN},
  {"sniffpwn",       "sniffpwn",       F_SCAN},
  {"sniffpinescan",  "sniffpinescan",  F_SCAN},
  {"sniffmultissid", "sniffmultissid", F_SCAN},
  {"sniffskim",      "sniffskim",      F_SCAN},
  {"remoteid scan",  "remoteid scan",  F_SCAN},
};
static const Item I_PASS[] = {
  {"sniffbeacon", "sniffbeacon", F_SCAN},
  {"sniffprobe",  "sniffprobe",  F_SCAN},
  {"sniffraw",    "sniffraw",    F_SCAN},
  {"sniffsae",    "sniffsae",    F_SCAN},
  {"sniffpmkid",  "sniffpmkid",  F_SCAN},
};
static const Item I_ATK[] = {
  {"deauth (target)", "attack -t deauth",   F_TX|F_SELTGT|F_SCAN},
  {"beacon list(ssid)","attack -t beacon -l",F_TX|F_SCAN},  // needs ssid list (use 'ssid x5' first)
  {"beacon spam rand","attack -t beacon -r",F_TX|F_SCAN},
  {"probe (target)",  "attack -t probe",    F_TX|F_SELTGT|F_SCAN},  // needs selected AP
  {"rickroll",        "attack -t rickroll", F_TX|F_SCAN},
  {"evil portal(SD)", "evilportal -c start",F_TX|F_SCAN},   // needs portal html on SD
  {"karma (target)",  "karma -p",           F_TX|F_TARGET|F_SCAN},
};
static const Item I_TWIN[] = {
  // beacon twin: broadcastCustomBeacon() injects the target's real SSID+BSSID+channel
  {"beacon twin",    "attack -t beacon -a", F_TX|F_SELTGT|F_SCAN},
  // portal twin: clone target BSSID onto our AP iface, then bring up the captive
  // soft-AP. "@twinportal" is a macro intercepted in runItem(). Needs portal html on SD.
  {"portal twin(SD)","@twinportal",         F_TX|F_SELTGT|F_SCAN},
};
static const Item I_BLE[] = {
  {"sniffbt airtag",  "sniffbt -t airtag",  F_SCAN},
  {"sniffbt flipper", "sniffbt -t flipper", F_SCAN},
  {"blespam all",     "blespam -t all",     F_TX|F_SCAN},
  {"findmy (tag)",    "findmy -t",          F_TARGET},
  {"spoofat (tag)",   "spoofat -t",         F_TX|F_TARGET|F_SCAN},
};
static const Item I_MAC[] = {
  {"randapmac",     "randapmac",    F_TX},
  {"randstamac",    "randstamac",   F_TX},
  {"cloneapmac",    "cloneapmac -a",F_TX|F_TARGET},
  {"clonestamac",   "",             F_TEXT},   // needs station idx (list -c) -> serial
};
static const Item I_SESS[] = {
  {"save APs",   "save -a",     0},
  {"load APs",   "load -a",     0},
  {"clear APs",  "clearlist -a",0},
  {"clear STAs", "clearlist -c",0},
  {"join saved", "join -s",     0},
  {"join (pw)",  "",            F_TEXT},   // needs password text -> serial
  {"ssid x5",    "ssid -a -g 5",0},
  {"ssid name",  "",            F_TEXT},   // needs text -> serial
};
static const Item I_NET[] = {
  {"pingscan",     "pingscan",        0},
  {"arpscan",      "arpscan -f",      0},
  {"portscan http","portscan -s http",0},
};
static const Item I_SD[] = {
  {"ls /",          "ls /",         0},
  {"backupspiffs",  "backupspiffs",  0},
  {"backupstatus",  "backupstatus",  0},
  {"restorespiffs", "restorespiffs", 0},
  {"update SD",     "update -s",     F_TX},
  {"upload wigle",  "upload -d wigle",0},
};
static const Item I_SYS[] = {
  {"stopscan",      "stopscan",                   0},
  {"reboot",        "reboot",                     F_TX},
  {"protocolinfo",  "protocolinfo",               0},
  {"SavePCAP on",   "settings -s SavePCAP enable",0},
  {"SavePCAP off",  "settings -s SavePCAP disable",0},
  {"settings reset","settings -r",                F_TX},
};

struct Cat { const char* name; const Item* items; uint8_t n; };
#define NI(a) (sizeof(a)/sizeof(a[0]))
static const Cat CATS[] = {
  {"Scan / Discover",   I_SCAN, NI(I_SCAN)},
  {"Defensive Detect",  I_DEF,  NI(I_DEF)},
  {"Passive Sniff",     I_PASS, NI(I_PASS)},
  {"Attack / Offensive",I_ATK,  NI(I_ATK)},
  {"Evil Twin",         I_TWIN, NI(I_TWIN)},
  {"BLE Tools",         I_BLE,  NI(I_BLE)},
  {"MAC-Spoof / ID",    I_MAC,  NI(I_MAC)},
  {"Target / Session",  I_SESS, NI(I_SESS)},
  {"Network Recon",     I_NET,  NI(I_NET)},
  {"SD / Files",        I_SD,   NI(I_SD)},
  {"Settings / System", I_SYS,  NI(I_SYS)},
};
static const int N_CAT = NI(CATS);

static const uint32_t OLED_REFRESH_MS = 100;
static const char HEARTBEAT[4] = {'|','/','-','\\'};
static const int ROWS = 4;   // visible rows below the 12px header

// ---- 8x8 category icons (XBM, LSB-first) ----
static const unsigned char IC_SCAN[8]  = {0x1e,0x21,0x21,0x21,0x1e,0x18,0x30,0x20};
static const unsigned char IC_SHIELD[8]= {0x7e,0x7e,0x42,0x42,0x42,0x24,0x18,0x00};
static const unsigned char IC_EYE[8]   = {0x00,0x3c,0x42,0x99,0x5a,0x3c,0x00,0x00};
static const unsigned char IC_WARN[8]  = {0x08,0x08,0x1c,0x14,0x36,0x2a,0x22,0x3e};
static const unsigned char IC_BT[8]    = {0x08,0x18,0x2a,0x1c,0x08,0x1c,0x2a,0x18};
static const unsigned char IC_MASK[8]  = {0x00,0x7e,0xa5,0x81,0xa5,0x42,0x3c,0x00};
static const unsigned char IC_LIST[8]  = {0x00,0x3d,0x00,0x3d,0x00,0x3d,0x00,0x00};
static const unsigned char IC_NET[8]   = {0x42,0x42,0x24,0x18,0x18,0x24,0x42,0x42};
static const unsigned char IC_SD[8]    = {0x3e,0x22,0x2a,0x22,0x22,0x22,0x3e,0x00};
static const unsigned char IC_GEAR[8]  = {0x08,0x2a,0x1c,0x63,0x1c,0x2a,0x08,0x00};
static const unsigned char IC_WIFI[8]  = {0x80,0x80,0xa0,0xa0,0xa8,0xa8,0xaa,0xaa};
static const unsigned char* const CAT_ICONS[] = {
  IC_SCAN, IC_SHIELD, IC_EYE, IC_WARN, IC_MASK, IC_BT, IC_MASK, IC_LIST, IC_NET, IC_SD, IC_GEAR
};

static const unsigned char BAT_BMP[] = {
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08,
  0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x9c, 0x03, 0x00, 0x00, 0x00,
  0x30, 0x00, 0x00, 0xfe, 0xfc, 0x01, 0x00, 0x0c, 0x78, 0x00, 0x00, 0xff,
  0x7f, 0x0e, 0x00, 0x1e, 0xfc, 0x78, 0xc0, 0xff, 0xff, 0x07, 0x3c, 0x3f,
  0xfe, 0xfd, 0xe0, 0xff, 0xff, 0x3f, 0xfc, 0xfe, 0xff, 0xff, 0xff, 0xff,
  0xff, 0xff, 0xff, 0xfd, 0xfe, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x3e,
  0xfc, 0xff, 0xff, 0xff, 0xff, 0xff, 0xbf, 0x07, 0xf0, 0xff, 0xff, 0xff,
  0xff, 0xff, 0xe7, 0x00, 0xc0, 0xff, 0xff, 0xff, 0xff, 0x1f, 0x06, 0x00,
  0x00, 0xfe, 0x3f, 0xff, 0x7f, 0x30, 0x00, 0x00, 0x00, 0xf0, 0x0f, 0xfc,
  0x87, 0x01, 0x00, 0x00, 0x00, 0x80, 0x07, 0xf0, 0x31, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x04, 0xc0, 0x0d, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc0,
  0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00,
};

static int g_feat_cat = 0;
static const char* main_label(int i){ return CATS[i].name; }
static const char* feat_label(int i){ return CATS[g_feat_cat].items[i].label; }

static bool g_confirm_cancel = false;
static bool g_c_prev = false, g_c_held = false;
static uint32_t g_c_start = 0;
static const uint32_t HOLD_MS = 500;
static uint32_t g_last_fine = 0;   // packet count at last graph sample

void OledStatus::begin() {
  Wire.begin(OLED_SDA, OLED_SCL);
  uint8_t cand[2] = { OLED_ADDR, (uint8_t)(OLED_ADDR==0x3C?0x3D:0x3C) };
  bool found=false;
  for (uint8_t i=0;i<2&&!found;i++){ Wire.beginTransmission(cand[i]); if(Wire.endTransmission()==0){this->addr=cand[i];found=true;} }
  if(!found){ Serial.println(F("[OLED] no I2C device at 0x3C/0x3D - UI disabled")); this->ok=false; return; }
  Serial.print(F("[OLED] device found at 0x")); Serial.print(this->addr,HEX);
  #ifdef OLED_SH1106
    Serial.println(F(" (SH1106)"));
  #else
    Serial.println(F(" (SSD1306; set OLED_SH1106 if shifted)"));
  #endif
  this->u8g2.setI2CAddress(this->addr<<1);
  this->u8g2.setBusClock(100000);
  this->u8g2.begin();
  this->u8g2.setFont(u8g2_font_6x10_tf);
  #if defined(HAS_BATTERY_SENSE) && defined(BATT_VBUS_PIN)
    pinMode(BATT_VBUS_PIN, INPUT);
  #endif
  #if defined(HAS_BATTERY_SENSE) && defined(BATT_CHRG_PIN)
    pinMode(BATT_CHRG_PIN, INPUT_PULLUP);
  #endif
  this->ok=true;
  this->screen=SPLASH;
  this->splash_until=millis()+2600;
}

const char* OledStatus::modeLabel(uint8_t mode){
  switch(mode){
    case WIFI_SCAN_OFF: return "Idle";
    case WIFI_SCAN_AP: return "Beacon Sniff";
    case WIFI_SCAN_PROBE: return "Probe Sniff";
    case WIFI_SCAN_DEAUTH: return "Deauth Detect";
    case WIFI_SCAN_PWN: return "Pwn Detect";
    case WIFI_SCAN_RAW_CAPTURE: return "Raw Sniff";
    case WIFI_SCAN_AP_STA: return "Scan All";
    case WIFI_SCAN_STATION: return "Station Scan";
    case WIFI_SCAN_SIG_STREN: return "Foxhunt";
    case WIFI_SCAN_EVIL_PORTAL: return "Evil Portal";
    case WIFI_SCAN_PINESCAN: return "Pineapple Det";
    case WIFI_SCAN_MULTISSID: return "MultiSSID Det";
    case WIFI_PACKET_MONITOR: return "Pkt Monitor";
    case WIFI_ATTACK_BEACON_SPAM: return "Beacon Spam";
    case WIFI_ATTACK_BEACON_LIST: return "Beacon Spam";
    case WIFI_ATTACK_RICK_ROLL: return "Rickroll";
    case WIFI_ATTACK_AP_SPAM: return "AP Spam";
    case BT_SCAN_ALL: return "BLE Sniff";
    case BT_SCAN_SKIMMERS: return "Skimmer Det";
    case BT_SCAN_AIRTAG: return "AirTag Sniff";
    default: return "Active";
  }
}

// ---- power / battery indicator ----
enum { PWR_EXTERNAL=0, PWR_CHARGING=1, PWR_BATTERY=2 };

int OledStatus::batteryPct(){
#if defined(BATT_UI_DEMO)
  int v=(int)((millis()/120)%200); return v>100?200-v:v;     // fake ramp 0..100..0
#elif defined(HAS_BATTERY_SENSE)
  long mv=(long)analogReadMilliVolts(BATT_ADC_PIN)*BATT_DIV_MUL; // divider -> battery mV
  long pct=(mv-3300)*100/(4200-3300);                            // 3.3V=0%  4.2V=100%
  if(pct<0)pct=0; if(pct>100)pct=100; return (int)pct;
#else
  return 0;
#endif
}

int OledStatus::powerState(){
#if defined(BATT_UI_DEMO)
  switch((millis()/5000)%3){ case 1: return PWR_CHARGING; case 2: return PWR_EXTERNAL; default: return PWR_BATTERY; }
#elif defined(HAS_BATTERY_SENSE)
  bool ext=false, chg=false;
  #ifdef BATT_VBUS_PIN
    ext = digitalRead(BATT_VBUS_PIN);          // HIGH = USB 5V present
  #endif
  #ifdef BATT_CHRG_PIN
    chg = (digitalRead(BATT_CHRG_PIN)==LOW);   // TP4056 CHRG active-low
  #endif
  if(chg) return PWR_CHARGING;
  if(ext) return PWR_EXTERNAL;
  return PWR_BATTERY;
#else
  return PWR_EXTERNAL;   // nothing wired -> honest default: externally powered
#endif
}

// draws a ~13x7 glyph at (x,y) in the current draw color
void OledStatus::drawPower(int x,int y){
  int st=powerState();
  if(st==PWR_EXTERNAL){                 // plug
    u8g2.drawBox(x+7,y+1,6,5);          // body
    u8g2.drawHLine(x+4,y+2,3); u8g2.drawHLine(x+4,y+4,3);   // leads
    u8g2.drawBox(x+1,y+1,2,2); u8g2.drawBox(x+1,y+4,2,2);   // prong tips
    return;
  }
  // battery shell + nub
  u8g2.drawFrame(x,y,12,7); u8g2.drawBox(x+12,y+2,1,3);
  if(st==PWR_CHARGING){                 // lightning bolt inside
    u8g2.drawLine(x+7,y+1,x+4,y+4); u8g2.drawLine(x+4,y+4,x+7,y+4); u8g2.drawLine(x+7,y+4,x+5,y+6);
  } else {                              // fill by percent
    int fw=batteryPct()*10/100; if(fw<0)fw=0; if(fw>10)fw=10;
    if(fw>0) u8g2.drawBox(x+1,y+1,fw,5);
  }
}

void OledStatus::samplePkts(uint32_t now){
  if(now - this->last_pkt_sample >= 1000){
    uint32_t c = oled_frame_count;
    this->pkt_rate = c - this->last_pkt_count;
    this->last_pkt_count = c;
    this->last_pkt_sample = now;
  }
}

void OledStatus::showToast(const char* msg){
  strncpy(this->toast,msg,sizeof(this->toast)-1);
  this->toast[sizeof(this->toast)-1]=0;
  this->toast_until = millis()+1400;
}

// ---- helpers ----
void OledStatus::drawScrollbar(int top,int count,int visible){
  if(count<=visible) return;
  int trackY=13, trackH=50;
  u8g2.drawVLine(126,trackY,trackH);
  int barH = trackH*visible/count; if(barH<5)barH=5;
  int barY = trackY + (trackH-barH)*top/(count-visible);
  u8g2.drawBox(124,barY,4,barH);
}
void OledStatus::drawGraph(int x,int y,int w,int h){
  u8g2.drawFrame(x,y,w,h);
  int n=w-2; int maxh=h-2;
  for(int i=0;i<n;i++){
    int idx=(int)((rate_head - 1 - i + sizeof(rate_hist)) % sizeof(rate_hist));
    int dh = rate_hist[idx]*maxh/24;                 // real activity
    int ph = ((i*5 + beat*2) & 31); int tw = ph<16?ph:(31-ph);  // scrolling triangle wave 0..15
    int fh = 1 + tw*(maxh/3)/15;                      // animated idle floor (<=1/3)
    int bh = dh>fh?dh:fh; if(bh<1)bh=1; if(bh>maxh)bh=maxh;
    u8g2.drawVLine(x+w-2-i, y+h-1-bh, bh);
  }
}
void OledStatus::drawSignal(int x,int y,int rssi){
  int bars=(rssi+95)/13; if(bars<0)bars=0; if(bars>4)bars=4;
  for(int b=0;b<4;b++){ int bh=2+b*2;
    if(b<bars) u8g2.drawBox(x+b*3,y-bh,2,bh); else u8g2.drawFrame(x+b*3,y-bh,2,bh);
  }
}
void OledStatus::drawRowText(int x,int y,int maxw,const char* text,bool selected){
  int len=(int)strlen(text); int px=len*6;
  if(selected && px>maxw){
    int over=px-maxw+6;
    int off=(int)(marquee % (over+16)); if(off>over) off=over;
    u8g2.setClipWindow(x,y-10,x+maxw,y+2);
    u8g2.drawStr(x-off,y,text);
    u8g2.setMaxClipWindow();
  } else {
    char buf[26]; int m=maxw/6; if(m>25)m=25;
    strncpy(buf,text,m); buf[m]=0; u8g2.drawStr(x,y,buf);
  }
}

void OledStatus::drawSplash(){
  const uint32_t total=2600;
  uint32_t left=(splash_until>millis())?(splash_until-millis()):0;
  uint32_t t=total-left;                         // elapsed ms
  u8g2.clearBuffer();
  // bat logo wipes in left-to-right
  int reveal=(t<700)?(int)(t*128/700):128;
  u8g2.setClipWindow(0,0,reveal,24);
  u8g2.drawXBMP(32,1,64,20,BAT_BMP);
  u8g2.setMaxClipWindow();
  // sonar-ping wifi arc above the head once revealed
  if(t>600){
    int pr=2+(int)(((t-600)%800)*12/800);
    u8g2.drawCircle(64,7,pr,U8G2_DRAW_UPPER_LEFT|U8G2_DRAW_UPPER_RIGHT);
  }
  u8g2.setFont(u8g2_font_7x13B_tf); u8g2.drawStr(30,36,"MARAUDER");
  u8g2.setFont(u8g2_font_5x8_tf);   u8g2.drawStr(26,45,"FIELD CONSOLE");
  // progress bar
  u8g2.drawFrame(14,49,100,7);
  int w=(int)(96L*t/total); if(w>96)w=96; if(w>0) u8g2.drawBox(16,51,w,3);
  // BOOTING with cycling dots
  int dots=(int)((t/300)%4); char bd[12]="BOOTING";
  for(int i=0;i<dots&&i<3;i++) strcat(bd,".");
  u8g2.drawStr(42,63,bd);
  u8g2.sendBuffer();
}

static void header(U8G2& g,const char* t,char beat){
  g.drawBox(0,0,128,12);
  g.setDrawColor(0); g.setFont(u8g2_font_6x10_tf);
  g.drawStr(2,10,t);
  char b[2]={beat,0}; g.drawStr(120,10,b);
  g.setDrawColor(1);
}

void OledStatus::drawDash(){
  u8g2.clearBuffer();
  // --- top status bar ---
  u8g2.drawBox(0,0,128,11); u8g2.setDrawColor(0);
  u8g2.drawXBMP(2,2,8,8,IC_WIFI);
  u8g2.setFont(u8g2_font_5x8_tf);
  uint32_t up=millis()/1000; char cl[12];
  snprintf(cl,sizeof(cl),"UP %lu:%02lu",(unsigned long)(up/60),(unsigned long)(up%60));
  u8g2.drawStr(42,8,cl);
  char hb[2]={HEARTBEAT[beat&3],0}; u8g2.drawStr(100,8,hb);
  #ifdef HAS_BATTERY_UI
    if(powerState()==PWR_BATTERY){ char pb[5]; snprintf(pb,sizeof(pb),"%d",batteryPct()); u8g2.drawStr(94,8,pb); drawPower(113,2); }
    else drawPower(113,2);
  #endif
  u8g2.setDrawColor(1);
  // --- line 1: channel + MON/IDLE badge + BLE count ---
  u8g2.setFont(u8g2_font_6x10_tf);
  bool sc=wifi_scan_obj.scanning();
  char l[24]; snprintf(l,sizeof(l),"CH:%u",(unsigned)wifi_scan_obj.set_channel);
  u8g2.drawStr(2,21,l);
  const char* badge = sc?"MON":"IDLE"; int bw=(int)strlen(badge)*6+5;
  u8g2.drawFrame(36,13,bw,10); u8g2.drawStr(39,21,badge);
  u8g2.drawXBMP(88,13,8,8,IC_BT);
  snprintf(l,sizeof(l),"%d",ble_devices?ble_devices->size():0); u8g2.drawStr(98,21,l);
  // --- line 2: AP / STA / pkts-per-sec ---
  snprintf(l,sizeof(l),"AP:%d STA:%d P/s:%lu",access_points?access_points->size():0,
           stations?stations->size():0,(unsigned long)pkt_rate);
  u8g2.drawStr(2,31,l);
  // --- spectrum graph ---
  drawGraph(2,34,124,21);
  u8g2.setFont(u8g2_font_5x8_tf); u8g2.drawStr(30,63,"SELECT for Menu");
  u8g2.sendBuffer();
}

void OledStatus::drawMenu(const char* title,int idx,int top,int count,const char*(*label_of)(int),bool is_main){
  u8g2.clearBuffer();
  char t[22]; snprintf(t,sizeof(t),"%s %d/%d",title,idx+1,count);
  header(u8g2,t,HEARTBEAT[beat&3]);
  for(int r=0;r<ROWS && (top+r)<count;r++){
    int i=top+r; int y=24+r*12; bool seld=(i==idx);
    if(seld){ u8g2.drawBox(0,y-10,124,12); u8g2.setDrawColor(0); }
    if(is_main) u8g2.drawXBMP(2,y-9,8,8,CAT_ICONS[i]);
    else { bool tx=CATS[g_feat_cat].items[i].flags & F_TX;
           if(tx) u8g2.drawXBMP(2,y-9,8,8,IC_WARN); else u8g2.drawDisc(5,y-5,1); }
    drawRowText(13,y,108,label_of(i),seld);
    if(seld) u8g2.setDrawColor(1);
  }
  drawScrollbar(top,count,ROWS);
  u8g2.sendBuffer();
}

void OledStatus::drawConfirm(){
  const Item* it=(const Item*)pend_item;
  u8g2.clearBuffer();
  header(u8g2,"CONFIRM (TX)",HEARTBEAT[beat&3]);
  u8g2.drawXBMP(2,15,8,8,IC_WARN);
  u8g2.setFont(u8g2_font_6x10_tf); u8g2.drawStr(13,23,it?it->label:"");
  u8g2.setFont(u8g2_font_5x8_tf);
  u8g2.drawStr(2,35,"Transmits / changes ID.");
  u8g2.drawStr(2,44,"Use only where legal.");
  u8g2.setFont(u8g2_font_6x10_tf);
  if(!g_confirm_cancel){u8g2.drawBox(6,50,52,13);u8g2.setDrawColor(0);} else u8g2.drawFrame(6,50,52,13);
  u8g2.drawStr(16,60,"START"); u8g2.setDrawColor(1);
  if(g_confirm_cancel){u8g2.drawBox(68,50,54,13);u8g2.setDrawColor(0);} else u8g2.drawFrame(68,50,54,13);
  u8g2.drawStr(76,60,"CANCEL"); u8g2.setDrawColor(1);
  u8g2.sendBuffer();
}

void OledStatus::drawRunning(){
  u8g2.clearBuffer();
  // title bar + blinking RUN badge
  u8g2.drawBox(0,0,128,11); u8g2.setDrawColor(0);
  u8g2.setFont(u8g2_font_6x10_tf);
  char t[22]; snprintf(t,sizeof(t),"[*] %.13s",modeLabel(wifi_scan_obj.currentScanMode));
  u8g2.drawStr(2,9,t);
  // honest state: blink RUN only while the radio is actually in a scan/attack
  // mode; otherwise say OFF (the command returned without starting).
  if(wifi_scan_obj.scanning()){ if((beat/3)&1) u8g2.drawStr(104,9,"RUN"); }
  else u8g2.drawStr(104,9,"OFF");
  u8g2.setDrawColor(1);
  char l[24]; uint32_t el=(run_start?(millis()-run_start):0)/1000;
  snprintf(l,sizeof(l),"CH:%u  Pkts:%lu",(unsigned)wifi_scan_obj.set_channel,(unsigned long)oled_frame_count);
  u8g2.drawStr(2,21,l);
  snprintf(l,sizeof(l),"Rate:%lu/s  %02lu:%02lu",(unsigned long)pkt_rate,(unsigned long)(el/60),(unsigned long)(el%60));
  u8g2.drawStr(2,31,l);
  drawGraph(2,34,124,20);
  u8g2.setFont(u8g2_font_5x8_tf); u8g2.drawStr(22,63,"SELECT to STOP");
  u8g2.sendBuffer();
}

void OledStatus::drawList(){
  u8g2.clearBuffer();
  int count=access_points?access_points->size():0;
  char t[22]; snprintf(t,sizeof(t),"AP LIST %d",count);
  header(u8g2,t,HEARTBEAT[beat&3]);
  if(count==0){ u8g2.setFont(u8g2_font_6x10_tf); u8g2.drawStr(2,34,"empty - Scan first"); u8g2.sendBuffer(); return; }
  u8g2.setFont(u8g2_font_6x10_tf);
  for(int r=0;r<ROWS && (list_top+r)<count;r++){
    int i=list_top+r; int y=24+r*12; bool seld=(i==list_idx);
    AccessPoint ap=access_points->get(i);
    if(seld){ u8g2.drawBox(0,y-10,124,12); u8g2.setDrawColor(0); }
    char row[26]; String e=ap.essid; if(e.length()==0)e="(hidden)";
    snprintf(row,sizeof(row),"%d %.12s",i,e.c_str());
    drawRowText(2,y,92,row,seld);
    char rs[8]; snprintf(rs,sizeof(rs),"%d",(int)ap.rssi); u8g2.drawStr(98,y,rs);
    if(seld) u8g2.setDrawColor(1);
  }
  drawScrollbar(list_top,count,ROWS);
  u8g2.sendBuffer();
}

void OledStatus::drawChan(){
  u8g2.clearBuffer();
  header(u8g2,"SET CHANNEL",HEARTBEAT[beat&3]);
  u8g2.setFont(u8g2_font_6x10_tf); u8g2.drawStr(34,24,"Channel");
  char cv[4]; snprintf(cv,sizeof(cv),"%d",chan_val);
  u8g2.setFont(u8g2_font_logisoso16_tn);
  u8g2.drawStr(chan_val<10?60:54,44,cv);
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawFrame(10,48,108,7);
  int sx=10+(chan_val-1)*103/13; u8g2.drawBox(sx,47,4,9);
  u8g2.drawStr(2,62,"UP/DN  SELECT=set");
  u8g2.sendBuffer();
}

void OledStatus::drawToast(){
  int w=118,x=5,y=24,h=18;
  u8g2.setDrawColor(1); u8g2.drawRBox(x,y,w,h,3);
  u8g2.setDrawColor(0); u8g2.setFont(u8g2_font_6x10_tf); u8g2.drawStr(x+6,y+12,toast); u8g2.setDrawColor(1);
}

// ---------- run a feature item ----------
void OledStatus::runItem(const void* item){
  const Item* it=(const Item*)item;
  // build the command, applying a chosen target index if needed
  String cmd = it->cmd;
  // Evil-twin macro: clone the picked AP's BSSID onto our AP iface, then bring
  // up the captive soft-AP. Two CLI calls, so it can't be a plain single cmd.
  if(cmd == "@twinportal"){
    if(pend_target>=0){
      cli_obj.runCommand(String("cloneapmac -a ")+pend_target);
      cli_obj.runCommand("evilportal -c start");
      this->run_start=millis(); this->screen=RUNNINGS;
    } else { showToast("pick a target"); this->screen=FEAT; }
    this->pend_target=-1; this->pend_item=0; return;
  }
  if((it->flags & (F_TARGET|F_SELTGT)) && pend_target>=0){
    if(it->flags & F_SELTGT){
      cli_obj.runCommand(String("select -a ")+pend_target);
      // cmd stays as-is (e.g. "attack -t deauth")
    } else {
      cmd = cmd + " " + pend_target;   // e.g. "info -a 3", "karma -p 2"
    }
  }
  if(cmd.length()) cli_obj.runCommand(cmd);
  if(it->flags & F_SCAN){ this->run_start=millis(); this->screen=RUNNINGS; }
  else { showToast("sent"); this->screen=FEAT; }
  this->pend_target=-1; this->pend_item=0;
}

// ---------- buttons ----------
void OledStatus::handleButtons(uint32_t now){
  // UP/DOWN: single-step on the press edge (justPressed is the only method we
  // call on these, so its internal state stays consistent).
  bool up=u_btn.justPressed();
  bool dn=d_btn.justPressed();
  if(up||dn) this->marquee=0;
  // CENTER: driven purely from isPressedNow() + our own timer, because the
  // Switches justPressed()/justReleased() pair share one 'pressed' flag and
  // would consume each other's edge. short press = SELECT, hold = BACK.
  bool c_now=c_btn.isPressedNow();
  bool sel=false, back=false;
  if(c_now && !g_c_prev){ g_c_start=now; g_c_held=false; }            // press edge
  if(c_now && !g_c_held && (now-g_c_start)>=HOLD_MS){ g_c_held=true; back=true; } // long press
  if(!c_now && g_c_prev && !g_c_held) sel=true;                        // release = short press
  g_c_prev=c_now;
  if(back){
    switch(screen){
      case FEAT: screen=MAIN; break;
      case MAIN: screen=DASH; break;
      case LISTS: screen=FEAT; pend_item=0; pend_target=-1; break;
      case CONFIRMS: screen=FEAT; pend_item=0; break;
      case CHANS: screen=FEAT; break;
      case RUNNINGS: cli_obj.runCommand("stopscan"); screen=FEAT; break;
      default: break;
    }
    return;   // consume the back press; don't also treat as select this loop
  }

  const Cat& cat = CATS[cat_idx];

  switch(screen){
    case DASH:
      if(sel){ screen=MAIN; }
      break;
    case MAIN:
      if(up && cat_idx>0){ cat_idx--; if(cat_idx<cat_top)cat_top=cat_idx; }
      if(dn && cat_idx<N_CAT-1){ cat_idx++; if(cat_idx>=cat_top+ROWS)cat_top=cat_idx-ROWS+1; }
      if(sel){ g_feat_cat=cat_idx; feat_idx=0; feat_top=0; screen=FEAT; }
      break;
    case FEAT: {
      if(up && feat_idx>0){ feat_idx--; if(feat_idx<feat_top)feat_top=feat_idx; }
      if(dn && feat_idx<cat.n-1){ feat_idx++; if(feat_idx>=feat_top+ROWS)feat_top=feat_idx-ROWS+1; }
      if(sel){
        const Item* it=&cat.items[feat_idx];
        if(it->flags & F_TEXT){ showToast("use serial for text"); break; }
        if(it->flags & F_LISTV){ list_idx=0; list_top=0; screen=LISTS; break; }
        if(it->flags & F_CHAN){ chan_val=wifi_scan_obj.set_channel; screen=CHANS; break; }
        if(it->flags & (F_TARGET|F_SELTGT)){
          if(!access_points || access_points->size()==0){ showToast("scan first"); break; }
          pend_item=it; list_idx=0; list_top=0; screen=LISTS; break;
        }
        if(it->flags & F_TX){ pend_item=it; g_confirm_cancel=false; screen=CONFIRMS; break; }
        pend_item=it; runItem(it);
      }
      break; }
    case LISTS: {
      int count=access_points?access_points->size():0;
      if(up && list_idx>0){ list_idx--; if(list_idx<list_top)list_top=list_idx; }
      if(dn && list_idx<count-1){ list_idx++; if(list_idx>=list_top+ROWS)list_top=list_idx-ROWS+1; }
      if(sel){
        if(pend_item){ // target picker for a feature
          const Item* it=(const Item*)pend_item;
          pend_target=list_idx;
          if(it->flags & F_TX){ g_confirm_cancel=false; screen=CONFIRMS; }
          else runItem(it);
        } else { // plain AP list view -> show info over serial
          cli_obj.runCommand(String("info -a ")+list_idx);
          showToast("info -> serial");
        }
      }
      break; }
    case CONFIRMS:
      if(up||dn) g_confirm_cancel=!g_confirm_cancel;
      if(sel){
        if(g_confirm_cancel){ screen=FEAT; pend_item=0; pend_target=-1; }
        else runItem(pend_item);
      }
      break;
    case CHANS:
      if(up && chan_val<14) chan_val++;
      if(dn && chan_val>1) chan_val--;
      if(sel){ cli_obj.runCommand(String("channel -s ")+chan_val); showToast("channel set"); screen=FEAT; }
      break;
    case RUNNINGS:
      if(sel){ cli_obj.runCommand("stopscan"); screen=FEAT; }
      break;
    default: break;
  }
}

// ---------- main update ----------
void OledStatus::update(uint32_t now){
  if(!this->ok) return;
  // splash timeout
  if(screen==SPLASH && now>=splash_until) screen=DASH;

  if(screen!=SPLASH) handleButtons(now);

  // Honesty guard: if we jumped to the RUNNING screen but the radio never
  // entered a scan/attack mode within a short grace window, the command didn't
  // start (missing targets, empty list, unsupported path). Don't fake "RUN" -
  // tell the user and drop back to the feature menu.
  if(screen==RUNNINGS && run_start && (now-run_start)>900 && !wifi_scan_obj.scanning()){
    showToast("didn't start"); screen=FEAT; run_start=0;
  }

  samplePkts(now);

  if((now - this->last_draw) < OLED_REFRESH_MS) return;
  this->last_draw=now;
  this->beat++;
  this->marquee++;
  // fine-grained activity sample (per refresh ~10 Hz) for a lively spectrum
  { uint32_t c=oled_frame_count; uint32_t d=c-g_last_fine; g_last_fine=c;
    this->rate_hist[this->rate_head] = (uint8_t)(d>24?24:d);
    this->rate_head = (uint8_t)((this->rate_head+1) % sizeof(this->rate_hist)); }

  switch(screen){
    case SPLASH: drawSplash(); return;
    case DASH: drawDash(); break;
    case MAIN: drawMenu("MENU",cat_idx,cat_top,N_CAT,main_label,true); break;
    case FEAT: drawMenu(CATS[g_feat_cat].name,feat_idx,feat_top,CATS[g_feat_cat].n,feat_label,false); break;
    case CONFIRMS: drawConfirm(); break;
    case RUNNINGS: drawRunning(); break;
    case LISTS: drawList(); break;
    case CHANS: drawChan(); break;
  }
  if(now < toast_until){ drawToast(); u8g2.sendBuffer(); }
}

#endif // HAS_OLED_STATUS
