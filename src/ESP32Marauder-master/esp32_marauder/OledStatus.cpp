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
  {"beacon spam list","attack -t beacon -l",F_TX|F_SCAN},
  {"beacon spam rand","attack -t beacon -r",F_TX|F_SCAN},
  {"probe flood",     "attack -t probe",    F_TX|F_SCAN},
  {"rickroll",        "attack -t rickroll", F_TX|F_SCAN},
  {"evil portal",     "evilportal -c start",F_TX|F_SCAN},
  {"karma (target)",  "karma -p",           F_TX|F_TARGET|F_SCAN},
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
  {"BLE Tools",         I_BLE,  NI(I_BLE)},
  {"MAC-Spoof / ID",    I_MAC,  NI(I_MAC)},
  {"Target / Session",  I_SESS, NI(I_SESS)},
  {"Network Recon",     I_NET,  NI(I_NET)},
  {"SD / Files",        I_SD,   NI(I_SD)},
  {"Settings / System", I_SYS,  NI(I_SYS)},
};
static const int N_CAT = NI(CATS);

static const uint32_t OLED_REFRESH_MS = 120;
static const char HEARTBEAT[4] = {'|','/','-','\\'};
static const int ROWS = 5;   // visible rows below the header

// label accessors for drawMenu
static int g_feat_cat = 0;
static const char* main_label(int i){ return CATS[i].name; }
static const char* feat_label(int i){ return CATS[g_feat_cat].items[i].label; }

// confirm selection (false=START, true=CANCEL)
static bool g_confirm_cancel = false;
// select-vs-hold tracking for the center button
static bool g_c_prev = false, g_c_held = false;
static uint32_t g_c_start = 0;
static const uint32_t HOLD_MS = 500;

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
  this->ok=true;
  this->screen=SPLASH;
  this->splash_until=millis()+1800;
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
    case WIFI_ATTACK_RICK_ROLL: return "Rickroll";
    case WIFI_ATTACK_AP_SPAM: return "AP Spam";
    case BT_SCAN_ALL: return "BLE Sniff";
    case BT_SCAN_SKIMMERS: return "Skimmer Det";
    case BT_SCAN_AIRTAG: return "AirTag Sniff";
    default: return "Active";
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
  strncpy(this->toast, msg, sizeof(this->toast)-1);
  this->toast[sizeof(this->toast)-1]=0;
  this->toast_until = millis()+1400;
}

// ---------- drawing ----------
void OledStatus::drawSplash(){
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_7x13B_tf);
  u8g2.drawStr(22,22,"MARAUDER");
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(10,38,"FIELD CONSOLE");
  u8g2.drawStr(16,50,"WiFi / BLE");
  u8g2.drawFrame(14,56,100,6);
  uint32_t left = (this->splash_until>millis())?(this->splash_until-millis()):0;
  int w = 98 - (int)(98L*left/1800);
  if(w>0) u8g2.drawBox(15,57,w,4);
  u8g2.sendBuffer();
}

static void header(U8G2& g, const char* t, char beat){
  g.drawBox(0,0,128,12);
  g.setDrawColor(0);
  g.setFont(u8g2_font_6x10_tf);
  g.drawStr(2,10,t);
  char b[2]={beat,0}; g.drawStr(120,10,b);
  g.setDrawColor(1);
}

void OledStatus::drawDash(){
  u8g2.clearBuffer();
  header(u8g2,"HOME", HEARTBEAT[beat&3]);
  char l[22];
  bool sc = wifi_scan_obj.scanning();
  snprintf(l,sizeof(l),"WiFi CH:%u  %s",(unsigned)wifi_scan_obj.set_channel, sc?"RUN":"IDLE");
  u8g2.drawStr(2,24,l);
  snprintf(l,sizeof(l),"APs:%d  STAs:%d",access_points?access_points->size():0, stations?stations->size():0);
  u8g2.drawStr(2,36,l);
  snprintf(l,sizeof(l),"Pkts/s:%lu",(unsigned long)pkt_rate);
  u8g2.drawStr(2,48,l);
  snprintf(l,sizeof(l),"BLE ON  DEV:%d", ble_devices?ble_devices->size():0);
  u8g2.drawStr(2,60,l);
  u8g2.sendBuffer();
}

void OledStatus::drawMenu(const char* title,int idx,int top,int count,const char*(*label_of)(int)){
  u8g2.clearBuffer();
  char t[22]; snprintf(t,sizeof(t),"%s %d/%d",title,idx+1,count);
  header(u8g2,t,HEARTBEAT[beat&3]);
  for(int r=0;r<ROWS && (top+r)<count;r++){
    int i=top+r; int y=22+r*10;
    if(i==idx){ u8g2.drawBox(0,y-9,128,10); u8g2.setDrawColor(0);}
    u8g2.drawStr(4,y,label_of(i));
    if(i==idx) u8g2.setDrawColor(1);
  }
  u8g2.sendBuffer();
}

void OledStatus::drawConfirm(){
  const Item* it=(const Item*)pend_item;
  u8g2.clearBuffer();
  header(u8g2,"CONFIRM (TX)",HEARTBEAT[beat&3]);
  u8g2.drawStr(2,24,it?it->label:"");
  u8g2.drawStr(2,36,"Transmits/changes ID.");
  // START / CANCEL
  if(!g_confirm_cancel){u8g2.drawBox(6,44,54,14);u8g2.setDrawColor(0);}
  u8g2.drawStr(16,54,"START"); u8g2.setDrawColor(1);
  if(g_confirm_cancel){u8g2.drawBox(68,44,54,14);u8g2.setDrawColor(0);}
  u8g2.drawStr(78,54,"CANCEL"); u8g2.setDrawColor(1);
  u8g2.sendBuffer();
}

void OledStatus::drawRunning(){
  u8g2.clearBuffer();
  header(u8g2,"RUNNING",HEARTBEAT[beat&3]);
  u8g2.drawStr(2,24,modeLabel(wifi_scan_obj.currentScanMode));
  char l[22];
  snprintf(l,sizeof(l),"Pkts:%lu /s:%lu",(unsigned long)oled_frame_count,(unsigned long)pkt_rate);
  u8g2.drawStr(2,36,l);
  snprintf(l,sizeof(l),"CH:%u",(unsigned)wifi_scan_obj.set_channel);
  u8g2.drawStr(2,48,l);
  u8g2.drawStr(2,60,"SELECT = STOP");
  u8g2.sendBuffer();
}

void OledStatus::drawList(){
  u8g2.clearBuffer();
  int count = access_points?access_points->size():0;
  char t[22]; snprintf(t,sizeof(t),"AP LIST %d",count);
  header(u8g2,t,HEARTBEAT[beat&3]);
  if(count==0){ u8g2.drawStr(2,30,"empty - Scan first"); u8g2.sendBuffer(); return; }
  for(int r=0;r<ROWS && (list_top+r)<count;r++){
    int i=list_top+r; int y=22+r*10;
    AccessPoint ap=access_points->get(i);
    char row[26];
    String e=ap.essid; if(e.length()==0) e="(hidden)";
    snprintf(row,sizeof(row),"%d %.14s %d",i,e.c_str(),(int)ap.rssi);
    if(i==list_idx){u8g2.drawBox(0,y-9,128,10);u8g2.setDrawColor(0);}
    u8g2.drawStr(2,y,row);
    if(i==list_idx)u8g2.setDrawColor(1);
  }
  u8g2.sendBuffer();
}

void OledStatus::drawChan(){
  u8g2.clearBuffer();
  header(u8g2,"SET CHANNEL",HEARTBEAT[beat&3]);
  char l[22]; snprintf(l,sizeof(l),"Channel: %d",chan_val);
  u8g2.setFont(u8g2_font_7x13B_tf); u8g2.drawStr(20,38,l); u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(2,60,"UP/DN  SELECT=set");
  u8g2.sendBuffer();
}

void OledStatus::drawToast(){
  // overlay a small box with the toast text
  int w=120, x=4, y=26, h=16;
  u8g2.setDrawColor(1); u8g2.drawBox(x,y,w,h);
  u8g2.setDrawColor(0); u8g2.drawStr(x+4,y+11,toast); u8g2.setDrawColor(1);
}

// ---------- run a feature item ----------
void OledStatus::runItem(const void* item){
  const Item* it=(const Item*)item;
  // build the command, applying a chosen target index if needed
  String cmd = it->cmd;
  if((it->flags & (F_TARGET|F_SELTGT)) && pend_target>=0){
    if(it->flags & F_SELTGT){
      cli_obj.runCommand(String("select -a ")+pend_target);
      // cmd stays as-is (e.g. "attack -t deauth")
    } else {
      cmd = cmd + " " + pend_target;   // e.g. "info -a 3", "karma -p 2"
    }
  }
  if(cmd.length()) cli_obj.runCommand(cmd);
  if(it->flags & F_SCAN){ this->screen=RUNNINGS; }
  else { showToast("sent"); this->screen=FEAT; }
  this->pend_target=-1; this->pend_item=0;
}

// ---------- buttons ----------
void OledStatus::handleButtons(uint32_t now){
  // UP/DOWN: single-step on the press edge (justPressed is the only method we
  // call on these, so its internal state stays consistent).
  bool up=u_btn.justPressed();
  bool dn=d_btn.justPressed();
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
  samplePkts(now);

  if((now - this->last_draw) < OLED_REFRESH_MS) return;
  this->last_draw=now;
  this->beat++;

  switch(screen){
    case SPLASH: drawSplash(); return;
    case DASH: drawDash(); break;
    case MAIN: drawMenu("MENU",cat_idx,cat_top,N_CAT,main_label); break;
    case FEAT: drawMenu(CATS[g_feat_cat].name,feat_idx,feat_top,CATS[g_feat_cat].n,feat_label); break;
    case CONFIRMS: drawConfirm(); break;
    case RUNNINGS: drawRunning(); break;
    case LISTS: drawList(); break;
    case CHANS: drawChan(); break;
  }
  if(now < toast_until){ drawToast(); u8g2.sendBuffer(); }
}

#endif // HAS_OLED_STATUS
