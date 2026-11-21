/*
  Stove monitor for M5Stack Core2 (also Core Basic/Gray and CoreS3)

  - Two K-type thermocouples (stove top + flue) via MAX31855 or MAX6675 amplifiers
  - Screen wakes when the stove gets warm; colour-coded temperatures
  - Loud over-temperature alarm, gentle "add fuel" reminder when the fire dies down
  - CSV logging to microSD (temperatures, internal temperature, battery)
  - Web interface: live view, 6-hour graph, log viewer, settings
  - Joins home Wi-Fi; falls back to its own open hotspot when home Wi-Fi is unavailable
  - Clock from NTP when online, kept by the RTC through power cuts, or set by hand

  See README.md for wiring, libraries and setup.
*/

#include <M5Unified.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <SPI.h>
#include <SD.h>
#include <ArduinoJson.h>   // ArduinoJson 7.x
#include <time.h>
#include <sys/time.h>
#include "esp_sntp.h"
#include "StoveTypes.h"
#include "webpage.h"

// ======================= User options =======================
// You can leave these as placeholders and set Wi-Fi from the web page instead.
#define DEFAULT_WIFI_SSID  "SETME"
#define DEFAULT_WIFI_PASS  "SETME"

// Thermocouple hardware:
//   TC_DUALKMETER - M5Stack Module13.2 Dual Kmeter (stacks under the Core, I2C)
//   TC_MAX31855   - two separate MAX31855 breakout boards (wired to Port B/C)
//   TC_MAX6675    - two separate MAX6675 breakout boards (wired to Port B/C)
#define TC_MAX31855   1
#define TC_MAX6675    2
#define TC_DUALKMETER 3
#define TC_CHIP       TC_DUALKMETER

// Dual Kmeter module settings (only used with TC_DUALKMETER)
#define DKM_ADDR          0x11   // set by the module's DIP switches (0x11-0x20)
#define DKM_STOVE_CHANNEL 0      // 0 = probe socket 1, 1 = probe socket 2
#define DKM_REG_TEMP      0x00   // thermocouple temperature, int32 little-endian, 0.01 C
#define DKM_REG_CHANNEL   0x20   // channel select
#define DKM_REG_STATUS    0x30   // 0 = valid reading, 1 = no valid reading
#define DKM_I2C_FREQ      100000
// The module switches between probes with a relay, which clicks. To keep it quiet it
// stays on the stove-top probe and only switches to the flue now and then
// (two clicks: there and back).
#define FLUE_CHECK_SECONDS      60   // normal flue check interval
#define FLUE_CHECK_FAST_SECONDS 15   // when the flue was last in the red; set to 60 to disable

// ======================= Constants =======================
static const uint32_t CONFIG_VERSION = 1;
static const int      HIST_N         = 360;     // 6 h at one point per minute
static const uint32_t HIST_PERIOD_MS = 60000;
static const int      TREND_SAMPLES  = 5;       // trend over 5 minutes
static const int      SD_CS          = 4;

// ======================= Configuration =======================
Config cfg;
Preferences prefs;

void setTemperatureDefaults(Config &c) {
  c.wakeTemp = 40;
  // Stove top (cast-iron multi-fuel stove, surface probe)
  c.stove = {120, 260, 315, 340};
  // Flue (single-wall pipe surface, ~45 cm above the stove)
  c.flue  = {120, 245, 300, 330};
  c.intWarn = 50;
  c.intAlarm = 60;
  c.alarmEnabled = true;
  c.alarmVolume = 200;
  c.snoozeMin = 10;
  c.refuelEnabled = true;
  c.refuelTemp = 160;
  c.refuelVolume = 40;
  c.refuelRepeatMin = 15;
  c.logInterval = 30;
  c.ignoreShortFaults = false;
}

void setDefaults() {
  memset(&cfg, 0, sizeof(cfg));
  cfg.version = CONFIG_VERSION;
  strlcpy(cfg.ssid, DEFAULT_WIFI_SSID, sizeof(cfg.ssid));
  strlcpy(cfg.pass, DEFAULT_WIFI_PASS, sizeof(cfg.pass));
  strlcpy(cfg.apName, "StoveMonitor", sizeof(cfg.apName));
  strlcpy(cfg.hostname, "stove", sizeof(cfg.hostname));
  strlcpy(cfg.tz, "GMT0BST,M3.5.0/1,M10.5.0", sizeof(cfg.tz));   // UK time
  strlcpy(cfg.ntp, "uk.pool.ntp.org", sizeof(cfg.ntp));
  setTemperatureDefaults(cfg);
}

void loadConfig() {
  bool ok = false;
  if (prefs.begin("stove", true)) {
    if (prefs.getBytesLength("cfg") == sizeof(Config)) {
      prefs.getBytes("cfg", &cfg, sizeof(Config));
      ok = (cfg.version == CONFIG_VERSION);
    }
    prefs.end();
  }
  if (!ok) setDefaults();
}

void saveConfig() {
  prefs.begin("stove", false);
  prefs.putBytes("cfg", &cfg, sizeof(Config));
  prefs.end();
}

// ======================= State =======================
const char *levelName(Level l) {
  switch (l) {
    case L_COLD: return "cold";
    case L_GOOD: return "good";
    case L_HIGH: return "high";
    case L_HOT:  return "hot";
    default:     return "unknown";
  }
}
Level levelFor(float t, const ProbeLimits &p) {
  if (isnan(t)) return L_UNKNOWN;
  if (t < p.cool) return L_COLD;
  if (t < p.warn) return L_GOOD;
  if (t < p.hot)  return L_HIGH;
  return L_HOT;
}
Level internalLevel(float t) {
  if (isnan(t)) return L_UNKNOWN;
  if (t < cfg.intWarn) return L_GOOD;
  if (t < cfg.intAlarm) return L_HIGH;
  return L_HOT;
}

struct Readings {
  float stove = NAN, flue = NAN, internal = NAN;
  int battery = -1;
  int batteryMv = 0;
  bool onMains = true;
  bool charging = false;
} cur;

const char *probeFault[2] = {"", ""};
uint8_t probeBadCount[2] = {0, 0};
float probeLast[2] = {NAN, NAN};

bool hiStove = false, hiFlue = false, hiInt = false;
uint32_t snoozeUntil = 0;
bool refuelArmed = false, refuelDue = false;
uint32_t lastChirp = 0;
float trend = NAN;

bool sdOk = false;
bool screenOn = true;
uint32_t manualWakeUntil = 0;

const char *timeSource = "Not set";
volatile bool ntpJustSynced = false;

uint32_t pendingWifiRestart = 0, pendingReboot = 0;

// History ring buffer for the 6-hour graph
struct HistPt { uint32_t up; int16_t s, f, i; };
HistPt hist[HIST_N];
int histHead = 0, histCount = 0;

// ======================= Pins =======================
struct TcPins { int sck, so, cs1, cs2; } tc;

void selectPins() {
  switch (M5.getBoard()) {
    case m5::board_t::board_M5StackCore2:
    case m5::board_t::board_M5Tough:
      tc = {26, 36, 13, 14};       // Port B (G26, G36) + Port C (G13, G14)
      SPI.begin(18, 38, 23, SD_CS);
      break;
    case m5::board_t::board_M5StackCoreS3:
      tc = {9, 8, 18, 17};         // Port B (G9, G8) + Port C (G18, G17)
      SPI.begin(36, 35, 37, SD_CS);
      break;
    default:                        // M5Stack Basic / Gray (Fire: change CS to 2 and 5)
      tc = {26, 36, 16, 17};       // Port B (G26, G36) + Port C (G16, G17)
      SPI.begin(18, 19, 23, SD_CS);
      break;
  }
#if TC_CHIP != TC_DUALKMETER
  pinMode(tc.sck, OUTPUT); digitalWrite(tc.sck, LOW);
  pinMode(tc.so, INPUT);
  pinMode(tc.cs1, OUTPUT); digitalWrite(tc.cs1, HIGH);
  pinMode(tc.cs2, OUTPUT); digitalWrite(tc.cs2, HIGH);
#endif
}

// ======================= Thermocouples =======================
uint32_t shiftInBits(int cs, int bits) {
  uint32_t v = 0;
  digitalWrite(cs, LOW);
  delayMicroseconds(5);
  for (int i = 0; i < bits; i++) {
    digitalWrite(tc.sck, LOW);
    delayMicroseconds(2);
    v = (v << 1) | (digitalRead(tc.so) ? 1 : 0);
    digitalWrite(tc.sck, HIGH);
    delayMicroseconds(2);
  }
  digitalWrite(tc.sck, LOW);
  digitalWrite(cs, HIGH);
  return v;
}

// Dual Kmeter: the module measures one socket at a time (it switches with a relay),
// so we select a channel, wait until the next 1-second tick, then read it.
bool readDualKmeter(float &out, const char *&fault) {
  uint8_t status = 0xFF;
  if (!M5.In_I2C.readRegister(DKM_ADDR, DKM_REG_STATUS, &status, 1, DKM_I2C_FREQ)) {
    fault = "Kmeter module not found";
    return false;
  }
  if (status != 0) {            // 0 = valid reading; 1 = no valid reading (e.g. probe unplugged)
    fault = "Probe not connected";
    return false;
  }
  uint8_t b[4];
  if (!M5.In_I2C.readRegister(DKM_ADDR, DKM_REG_TEMP, b, 4, DKM_I2C_FREQ)) {
    fault = "Kmeter module not found";
    return false;
  }
  int32_t raw = (int32_t)((uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24));
  float t = raw / 100.0f;
  if (t < -200.0f || t > 1370.0f) { fault = "Probe disconnected"; return false; }
  out = t;
  fault = "";
  return true;
}

bool readThermocouple(int idx, float &out, const char *&fault) {
  int cs = (idx == 0) ? tc.cs1 : tc.cs2;
#if TC_CHIP == TC_MAX31855
  uint32_t v = shiftInBits(cs, 32);
  if (v == 0 || v == 0xFFFFFFFF || (v & 0x00020008)) { fault = "Amplifier not found"; return false; }
  if (v & 0x00010000) {
    if (v & 0x1) { fault = "Probe disconnected"; return false; }
    if (!cfg.ignoreShortFaults) {
      fault = (v & 0x2) ? "Probe shorted to ground" : "Probe shorted to supply";
      return false;
    }
  }
  int32_t raw = (v >> 18) & 0x3FFF;
  if (raw & 0x2000) raw -= 0x4000;
  out = raw * 0.25f;
#else
  uint32_t v = shiftInBits(cs, 16);
  if (v == 0xFFFF || (v & 0x8002)) { fault = "Amplifier not found"; return false; }
  if (v & 0x4) { fault = "Probe disconnected"; return false; }
  out = ((v >> 3) & 0xFFF) * 0.25f;
#endif
  fault = "";
  return true;
}

float readInternalTemp() {
  float t;
  if (M5.Imu.isEnabled() && M5.Imu.getTemp(&t) && t > -30 && t < 120) return t;
  if (M5.Power.getType() == m5::Power_Class::pmic_t::pmic_axp192) {
    return M5.Power.Axp192.getInternalTemperature();
  }
  return temperatureRead();  // ESP32 die sensor - rough only
}

void updateProbe(int i, bool ok, float t, const char *f, uint8_t failLimit = 3) {
  if (ok) {
    probeLast[i] = t;
    probeBadCount[i] = 0;
    probeFault[i] = "";
  } else {
    if (probeBadCount[i] < 255) probeBadCount[i]++;
    if (probeBadCount[i] >= failLimit) { probeLast[i] = NAN; probeFault[i] = f; }
  }
}

#if TC_CHIP == TC_DUALKMETER
// Called once a second. Normally reads the stove top; once a minute it switches
// to the flue, reads it on the next tick, switches back, and lets the stove
// channel settle for one tick before reading it again.
void readDualKmeterProbes() {
  enum { ON_STOVE, ON_FLUE, SETTLING } ;
  static int state = -1;
  static uint32_t lastFlueCheck = 0;
  const uint8_t stoveCh = DKM_STOVE_CHANNEL, flueCh = 1 - DKM_STOVE_CHANNEL;
  float t; const char *f;

  if (state < 0) {                                   // first call: read the flue first
    M5.In_I2C.writeRegister8(DKM_ADDR, DKM_REG_CHANNEL, flueCh, DKM_I2C_FREQ);
    state = ON_FLUE;
    return;
  }
  switch (state) {
    case ON_FLUE: {
      bool ok = readDualKmeter(t, f);
      updateProbe(1, ok, t, f, 2);                   // two missed checks = fault
      lastFlueCheck = millis();
      M5.In_I2C.writeRegister8(DKM_ADDR, DKM_REG_CHANNEL, stoveCh, DKM_I2C_FREQ);
      state = SETTLING;
      break;
    }
    case SETTLING:
      state = ON_STOVE;
      break;
    default: {                                       // ON_STOVE
      bool ok = readDualKmeter(t, f);
      updateProbe(0, ok, t, f);
      bool flueHot = !isnan(probeLast[1]) && probeLast[1] >= cfg.flue.hot;
      uint32_t interval = (flueHot ? FLUE_CHECK_FAST_SECONDS : FLUE_CHECK_SECONDS) * 1000UL;
      if (millis() - lastFlueCheck >= interval) {
        M5.In_I2C.writeRegister8(DKM_ADDR, DKM_REG_CHANNEL, flueCh, DKM_I2C_FREQ);
        state = ON_FLUE;
      }
      break;
    }
  }
}
#endif

void readSensors() {
#if TC_CHIP == TC_DUALKMETER
  readDualKmeterProbes();
#else
  float t; const char *f;
  for (int i = 0; i < 2; i++) {
    bool ok = readThermocouple(i, t, f);
    updateProbe(i, ok, t, f);
  }
#endif
  cur.stove = probeLast[0];
  cur.flue  = probeLast[1];

  float it = readInternalTemp();
  if (!isnan(it)) cur.internal = isnan(cur.internal) ? it : cur.internal * 0.8f + it * 0.2f;

  cur.battery   = M5.Power.getBatteryLevel();
  cur.batteryMv = M5.Power.getBatteryVoltage();
  auto chg = M5.Power.isCharging();
  cur.charging = (chg == m5::Power_Class::is_charging);
  int vbus = M5.Power.getVBUSVoltage();
  if (vbus >= 0) cur.onMains = vbus > 4000;
  else cur.onMains = (chg != m5::Power_Class::is_discharging);
}

// ======================= Time =======================
bool timeValid() { return time(nullptr) > 1704067200; }   // after 1 Jan 2024

void applyTimezone() { setenv("TZ", cfg.tz, 1); tzset(); }

void startNtp() { configTzTime(cfg.tz, cfg.ntp, "pool.ntp.org", "time.cloudflare.com"); }

void onNtpSync(struct timeval *) { ntpJustSynced = true; }

int64_t daysFromCivil(int y, int m, int d) {
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const int yoe = y - era * 400;
  const int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return (int64_t)era * 146097 + doe - 719468;
}

void loadTimeFromRtc() {
  if (!M5.Rtc.isEnabled()) return;
  auto dt = M5.Rtc.getDateTime();
  if (dt.date.year < 2024 || dt.date.year > 2099) return;
  time_t t = daysFromCivil(dt.date.year, dt.date.month, dt.date.date) * 86400LL
           + dt.time.hours * 3600 + dt.time.minutes * 60 + dt.time.seconds;   // RTC keeps UTC
  struct timeval tv = { t, 0 };
  settimeofday(&tv, nullptr);
  timeSource = "Real-time clock";
}

void saveTimeToRtc() {
  if (!M5.Rtc.isEnabled() || !timeValid()) return;
  time_t now = time(nullptr);
  struct tm u;
  gmtime_r(&now, &u);
  m5::rtc_datetime_t dt;
  dt.date.year = u.tm_year + 1900;
  dt.date.month = u.tm_mon + 1;
  dt.date.date = u.tm_mday;
  dt.date.weekDay = u.tm_wday;
  dt.time.hours = u.tm_hour;
  dt.time.minutes = u.tm_min;
  dt.time.seconds = u.tm_sec;
  M5.Rtc.setDateTime(dt);
}

String localTimeStr(const char *fmt) {
  if (!timeValid()) return String("");
  time_t now = time(nullptr);
  struct tm l;
  localtime_r(&now, &l);
  char buf[48];
  strftime(buf, sizeof(buf), fmt, &l);
  return String(buf);
}

// ======================= SD card =======================
bool mountSD() {
  SD.end();
  if (!SD.begin(SD_CS, SPI, 25000000)) { sdOk = false; return false; }
  if (!SD.exists("/logs")) SD.mkdir("/logs");
  sdOk = true;
  return true;
}

void fmt1(char *buf, size_t n, float v) {
  if (isnan(v)) buf[0] = 0; else snprintf(buf, n, "%.1f", v);
}

bool alarmActive() { return hiStove || hiFlue || hiInt; }

void logSample() {
  if (!sdOk) return;
  char path[40];
  if (timeValid()) {
    String d = localTimeStr("/logs/%Y-%m-%d.csv");
    strlcpy(path, d.c_str(), sizeof(path));
  } else {
    strlcpy(path, "/logs/no-clock.csv", sizeof(path));
  }
  bool isNew = !SD.exists(path);
  File f = SD.open(path, FILE_APPEND);
  if (!f) { sdOk = false; return; }
  if (isNew) f.println("datetime,epoch,uptime_s,stove_c,flue_c,internal_c,battery_pct,battery_mv,power,alarm,refuel_due");
  char s[12], fl[12], in[12];
  fmt1(s, sizeof(s), cur.stove);
  fmt1(fl, sizeof(fl), cur.flue);
  fmt1(in, sizeof(in), cur.internal);
  String dts = localTimeStr("%Y-%m-%d %H:%M:%S");
  f.printf("%s,%ld,%lu,%s,%s,%s,%d,%d,%s,%d,%d\n",
           dts.c_str(), timeValid() ? (long)time(nullptr) : 0L, (unsigned long)(millis() / 1000),
           s, fl, in, cur.battery, cur.batteryMv, cur.onMains ? "mains" : "battery",
           alarmActive() ? 1 : 0, refuelDue ? 1 : 0);
  f.close();
}

// ======================= History, trend and alerts =======================
int16_t enc(float v) { return isnan(v) ? INT16_MIN : (int16_t)lroundf(v * 10); }
float dec(int16_t v) { return v == INT16_MIN ? NAN : v / 10.0f; }

void addHistory() {
  HistPt &p = hist[histHead];
  p.up = millis() / 1000;
  p.s = enc(cur.stove);
  p.f = enc(cur.flue);
  p.i = enc(cur.internal);
  histHead = (histHead + 1) % HIST_N;
  if (histCount < HIST_N) histCount++;

  trend = NAN;
  if (histCount > TREND_SAMPLES) {
    float now = dec(hist[(histHead - 1 + HIST_N) % HIST_N].s);
    float then = dec(hist[(histHead - 1 - TREND_SAMPLES + 2 * HIST_N) % HIST_N].s);
    if (!isnan(now) && !isnan(then)) trend = now - then;
  }
}

void chirp() {
  M5.Speaker.setVolume(cfg.refuelVolume);
  M5.Speaker.tone(880, 90);
  delay(170);
  M5.Speaker.tone(660, 140);
}

void checkRefuel() {
  float t = cur.stove;
  if (isnan(t)) { refuelDue = false; return; }
  if (t >= cfg.refuelTemp + 20) { refuelArmed = true; lastChirp = 0; }   // burning well / refuelled
  if (t < cfg.wakeTemp) refuelArmed = false;                              // fire is out
  bool falling = !isnan(trend) && trend <= -2.0f;
  refuelDue = refuelArmed && t < cfg.refuelTemp && t >= cfg.wakeTemp;
  if (refuelDue && falling && cfg.refuelEnabled && !alarmActive()) {
    if (lastChirp == 0 || millis() - lastChirp >= cfg.refuelRepeatMin * 60000UL) {
      chirp();
      lastChirp = millis();
    }
  }
}

void hysteresis(bool &state, float t, float limit, float band) {
  if (isnan(t)) return;
  if (!state && t >= limit) state = true;
  else if (state && t < limit - band) state = false;
}

void updateAlarms() {
  hysteresis(hiStove, cur.stove, cfg.stove.alarm, 10);
  hysteresis(hiFlue, cur.flue, cfg.flue.alarm, 10);
  hysteresis(hiInt, cur.internal, cfg.intAlarm, 3);
  if (!alarmActive()) { snoozeUntil = 0; return; }
  if (!cfg.alarmEnabled || millis() < snoozeUntil) return;
  static bool alt = false;
  alt = !alt;
  M5.Speaker.setVolume(cfg.alarmVolume);
  M5.Speaker.tone(alt ? 2800 : 2100, 350);
}

bool alarmMuted() { return alarmActive() && snoozeUntil != 0 && millis() < snoozeUntil; }

void muteAlarm() {
  if (alarmActive()) snoozeUntil = millis() + cfg.snoozeMin * 60000UL;
}

// ======================= Wi-Fi =======================
enum NetMode { NET_CONNECTING, NET_STA, NET_AP };
NetMode netMode = NET_AP;
bool apUp = false, scanning = false;
uint32_t netTimer = 0, lastScan = 0, staLostAt = 0;
DNSServer dns;
const IPAddress apIP(192, 168, 4, 1);

void startAP() {
  WiFi.setAutoReconnect(false);
  WiFi.mode(WIFI_AP_STA);            // STA side is only used to scan for home Wi-Fi
  WiFi.disconnect(false);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(cfg.apName);           // open network, as requested
  dns.start(53, "*", apIP);          // captive portal: any name -> this device
  apUp = true;
  netMode = NET_AP;
  lastScan = millis();
}

void startSTA(bool keepAP) {
  if (strlen(cfg.ssid) == 0) { if (!apUp) startAP(); return; }
  WiFi.mode(keepAP ? WIFI_AP_STA : WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(cfg.ssid, cfg.pass);
  netMode = NET_CONNECTING;
  netTimer = millis();
}

void onStaConnected() {
  netMode = NET_STA;
  staLostAt = 0;
  if (apUp) { dns.stop(); WiFi.softAPdisconnect(true); apUp = false; }
  MDNS.end();
  if (MDNS.begin(cfg.hostname)) MDNS.addService("http", "tcp", 80);
  startNtp();
}

void restartWifi() {
  if (apUp) { dns.stop(); WiFi.softAPdisconnect(true); apUp = false; }
  WiFi.disconnect(true);
  delay(200);
  WiFi.setHostname(cfg.hostname);
  startSTA(false);
}

void netLoop() {
  if (apUp) dns.processNextRequest();
  switch (netMode) {
    case NET_CONNECTING:
      if (WiFi.status() == WL_CONNECTED) onStaConnected();
      else if (millis() - netTimer > 20000) {
        if (!apUp) startAP();
        else { WiFi.disconnect(false); netMode = NET_AP; lastScan = millis(); }
      }
      break;

    case NET_STA:
      if (WiFi.status() != WL_CONNECTED) {
        if (!staLostAt) staLostAt = millis();
        else if (millis() - staLostAt > 60000) startAP();   // give auto-reconnect a minute first
      } else {
        staLostAt = 0;
      }
      break;

    case NET_AP:
      // Every 2 minutes (when nobody is using the hotspot) look for home Wi-Fi
      if (!scanning && strlen(cfg.ssid) && millis() - lastScan > 120000 &&
          WiFi.softAPgetStationNum() == 0) {
        WiFi.scanNetworks(true);
        scanning = true;
        lastScan = millis();
      }
      if (scanning) {
        int n = WiFi.scanComplete();
        if (n >= 0) {
          bool found = false;
          for (int i = 0; i < n; i++) if (WiFi.SSID(i) == cfg.ssid) found = true;
          WiFi.scanDelete();
          scanning = false;
          if (found) startSTA(true);
        } else if (n == WIFI_SCAN_FAILED) {
          scanning = false;
        }
      }
      break;
  }
}

String ipString() {
  if (netMode == NET_STA && WiFi.status() == WL_CONNECTED) return WiFi.localIP().toString();
  if (apUp) return WiFi.softAPIP().toString();
  return String("");
}

// ======================= Display =======================
M5Canvas canvas(&M5.Display);

const uint16_t COL_COLD = 0x001F;   // blue
const uint16_t COL_GOOD = 0x0540;   // green
const uint16_t COL_HIGH = 0xFFE0;   // yellow
const uint16_t COL_HOT  = 0xF800;   // red
const uint16_t COL_NONE = 0x7BEF;   // grey

uint16_t bgFor(Level l) {
  switch (l) {
    case L_COLD: return COL_COLD;
    case L_GOOD: return COL_GOOD;
    case L_HIGH: return COL_HIGH;
    case L_HOT:  return COL_HOT;
    default:     return COL_NONE;
  }
}

const char *levelLabel(Level l) {
  switch (l) {
    case L_COLD: return "Cold";
    case L_GOOD: return "Good";
    case L_HIGH: return "Getting high";
    case L_HOT:  return "Too hot";
    default:     return "No reading";
  }
}

void drawProbePanel(int x, int y, int w, int h, const char *name, float t,
                    Level l, bool alarm, const char *fault, bool refuel) {
  uint16_t bg = bgFor(l);
  if (alarm && (millis() / 1000) % 2) bg = (uint16_t)0xFFFF;   // flash white/red
  uint16_t fg = (bg == COL_HIGH || bg == 0xFFFF) ? (uint16_t)0x0000 : (uint16_t)0xFFFF;
  canvas.fillRoundRect(x + 3, y, w - 6, h, 10, bg);
  canvas.setTextColor(fg);
  canvas.setTextDatum(top_center);
  canvas.setTextSize(1);
  canvas.setFont(&fonts::FreeSansBold12pt7b);
  canvas.drawString(name, x + w / 2, y + 10);

  int cx = x + w / 2, ny = y + h / 2 + 4;
  canvas.setFont(&fonts::FreeSansBold24pt7b);
  if (isnan(t)) {
    canvas.setTextDatum(middle_center);
    canvas.drawString("--", cx, ny);
  } else {
    char num[8];
    snprintf(num, sizeof(num), "%d", (int)lroundf(t));
    canvas.setTextSize(t >= 1000 ? 1.0f : 1.4f);
    canvas.setTextDatum(middle_center);
    canvas.drawString(num, cx - 6, ny);
    int tw = canvas.textWidth(num);
    int dx = cx - 6 + tw / 2 + 9, dy = ny - 22;
    canvas.drawCircle(dx, dy, 5, fg);
    canvas.drawCircle(dx, dy, 4, fg);
    canvas.setTextSize(1);
  }

  canvas.setTextDatum(bottom_center);
  const char *state = levelLabel(l);
  if (fault && fault[0]) { canvas.setFont(&fonts::FreeSans9pt7b); state = fault; }
  else canvas.setFont(&fonts::FreeSansBold12pt7b);
  if (refuel && !isnan(t)) state = "Add fuel?";
  canvas.drawString(state, cx, y + h - 8);
}

void drawScreen() {
  const int W = canvas.width(), H = canvas.height();
  const int top = 28, bottom = 44;
  canvas.fillScreen((uint16_t)0x0000);

  // Top bar: time and IP address
  canvas.setFont(&fonts::FreeSansBold12pt7b);
  canvas.setTextSize(1);
  canvas.setTextColor((uint16_t)0xFFFF);
  canvas.setTextDatum(middle_left);
  String t = timeValid() ? localTimeStr("%H:%M:%S") : String("--:--:--");
  canvas.drawString(t, 4, top / 2);
  canvas.setFont(&fonts::FreeSans9pt7b);
  canvas.setTextDatum(middle_right);
  String ip = ipString();
  canvas.drawString(ip.length() ? ip : String("No network"), W - 4, top / 2);
  if (alarmMuted()) {
    canvas.setTextDatum(middle_center);
    canvas.setTextColor(COL_HIGH);
    canvas.drawString("Muted", W / 2 + 10, top / 2);
  }

  // Probe panels
  int ph = H - top - bottom - 4;
  drawProbePanel(0, top, W / 2, ph, "Stove", cur.stove,
                 levelFor(cur.stove, cfg.stove), hiStove, probeFault[0], refuelDue);
  drawProbePanel(W / 2, top, W / 2, ph, "Flue", cur.flue,
                 levelFor(cur.flue, cfg.flue), hiFlue, probeFault[1], false);

  // Bottom status lines
  int by = H - bottom;
  Level il = internalLevel(cur.internal);
  if (il == L_HIGH || il == L_HOT) canvas.fillRect(0, by, W, 21, il == L_HOT ? COL_HOT : COL_HIGH);
  canvas.setTextColor(il == L_HIGH ? (uint16_t)0x0000 : (uint16_t)0xFFFF);
  canvas.setFont(&fonts::FreeSans9pt7b);
  char line[64];
  if (isnan(cur.internal)) snprintf(line, sizeof(line), "Inside --");
  else snprintf(line, sizeof(line), "Inside %dC", (int)lroundf(cur.internal));
  canvas.setTextDatum(middle_left);
  canvas.drawString(line, 4, by + 10);
  if (cur.battery >= 0) snprintf(line, sizeof(line), "Battery %d%%  %s", cur.battery, cur.onMains ? "Mains" : "ON BATTERY");
  else snprintf(line, sizeof(line), "%s", cur.onMains ? "Mains" : "ON BATTERY");
  canvas.setTextDatum(middle_right);
  canvas.drawString(line, W - 4, by + 10);

  canvas.setTextColor((uint16_t)0xFFFF);
  String net;
  if (netMode == NET_STA) net = String("Wi-Fi: ") + cfg.ssid;
  else if (apUp) net = String("Hotspot: ") + cfg.apName;
  else net = "Connecting...";
  canvas.setTextDatum(middle_left);
  canvas.drawString(net, 4, by + 32);
  canvas.setTextDatum(middle_right);
  canvas.drawString(sdOk ? "Logging" : "No SD card", W - 4, by + 32);

  canvas.pushSprite(0, 0);
}

void setScreen(bool on) {
  if (on == screenOn) return;
  screenOn = on;
  if (on) { M5.Display.wakeup(); }
  else { M5.Display.setBrightness(0); M5.Display.sleep(); }
}

void updateScreen() {
  float wakeProbe = !isnan(cur.stove) ? cur.stove : cur.flue;
  bool warm = !isnan(wakeProbe) &&
              (wakeProbe >= cfg.wakeTemp || (screenOn && wakeProbe > cfg.wakeTemp - 3));
  bool want = warm || millis() < manualWakeUntil || alarmActive() || hiInt;
  setScreen(want);
  if (screenOn) {
    M5.Display.setBrightness(cur.onMains ? 180 : 90);
    drawScreen();
  }
}

void handleButtons() {
  bool pressed = M5.BtnA.wasPressed() || M5.BtnB.wasPressed() || M5.BtnC.wasPressed();
  if (M5.Touch.isEnabled()) {
    auto d = M5.Touch.getDetail();
    if (d.wasPressed()) pressed = true;
  }
  if (pressed) {
    manualWakeUntil = millis() + 60000;
    muteAlarm();
    updateScreen();
  }
}

// ======================= Web server =======================
WebServer server(80);

float round1(float v) { return roundf(v * 10) / 10; }

void sendJson(JsonDocument &d, int code = 200) {
  String s;
  serializeJson(d, s);
  server.send(code, "application/json", s);
}

void putTemp(JsonVariant v, float t) {
  if (isnan(t)) v.set(nullptr); else v.set(round1(t));
}

void handleStatus() {
  JsonDocument d;
  d["time"] = timeValid() ? localTimeStr("%a %d %b %Y, %H:%M:%S") : String("Clock not set");
  d["timeValid"] = timeValid();
  d["timeSource"] = timeSource;
  d["uptime"] = millis() / 1000;

  putTemp(d["stove"]["t"].to<JsonVariant>(), cur.stove);
  d["stove"]["level"] = levelName(levelFor(cur.stove, cfg.stove));
  d["stove"]["fault"] = probeFault[0];
  d["stove"]["alarm"] = hiStove;

  putTemp(d["flue"]["t"].to<JsonVariant>(), cur.flue);
  d["flue"]["level"] = levelName(levelFor(cur.flue, cfg.flue));
  d["flue"]["fault"] = probeFault[1];
  d["flue"]["alarm"] = hiFlue;

  putTemp(d["internal"]["t"].to<JsonVariant>(), cur.internal);
  d["internal"]["level"] = levelName(internalLevel(cur.internal));
  d["internal"]["alarm"] = hiInt;

  if (cur.battery >= 0) d["battery"] = cur.battery; else d["battery"] = nullptr;
  d["batteryMv"] = cur.batteryMv;
  d["power"] = cur.onMains ? "mains" : "battery";
  d["charging"] = cur.charging;
  d["alarm"] = alarmActive();
  d["alarmMuted"] = alarmMuted();
  d["refuelDue"] = refuelDue;
  putTemp(d["trend"].to<JsonVariant>(), trend);
  d["sd"] = sdOk;
  d["screen"] = screenOn;
  d["net"]["mode"] = netMode == NET_STA ? "sta" : (apUp ? "ap" : "connecting");
  d["net"]["ssid"] = cfg.ssid;
  d["net"]["ap"] = cfg.apName;
  d["net"]["ip"] = ipString();
  d["net"]["host"] = String(cfg.hostname) + ".local";
  if (netMode == NET_STA) d["net"]["rssi"] = WiFi.RSSI();
  sendJson(d);
}

void handleHistory() {
  JsonDocument d;
  JsonArray ages = d["ages"].to<JsonArray>();
  JsonArray s = d["stove"].to<JsonArray>();
  JsonArray f = d["flue"].to<JsonArray>();
  JsonArray in = d["internal"].to<JsonArray>();
  uint32_t nowUp = millis() / 1000;
  int start = (histHead - histCount + HIST_N) % HIST_N;
  for (int k = 0; k < histCount; k++) {
    const HistPt &p = hist[(start + k) % HIST_N];
    ages.add(nowUp - p.up);
    putTemp(s.add<JsonVariant>(), dec(p.s));
    putTemp(f.add<JsonVariant>(), dec(p.f));
    putTemp(in.add<JsonVariant>(), dec(p.i));
  }
  sendJson(d);
}

void configToJson(JsonDocument &d) {
  d["ssid"] = cfg.ssid;
  d["passSet"] = strlen(cfg.pass) > 0;
  d["apName"] = cfg.apName;
  d["hostname"] = cfg.hostname;
  d["tz"] = cfg.tz;
  d["ntp"] = cfg.ntp;
  d["wakeTemp"] = cfg.wakeTemp;
  d["stoveCool"] = cfg.stove.cool;  d["stoveWarn"] = cfg.stove.warn;
  d["stoveHot"] = cfg.stove.hot;    d["stoveAlarm"] = cfg.stove.alarm;
  d["flueCool"] = cfg.flue.cool;    d["flueWarn"] = cfg.flue.warn;
  d["flueHot"] = cfg.flue.hot;      d["flueAlarm"] = cfg.flue.alarm;
  d["intWarn"] = cfg.intWarn;
  d["intAlarm"] = cfg.intAlarm;
  d["alarmEnabled"] = cfg.alarmEnabled;
  d["alarmVolume"] = cfg.alarmVolume;
  d["snoozeMin"] = cfg.snoozeMin;
  d["refuelEnabled"] = cfg.refuelEnabled;
  d["refuelTemp"] = cfg.refuelTemp;
  d["refuelVolume"] = cfg.refuelVolume;
  d["refuelRepeatMin"] = cfg.refuelRepeatMin;
  d["logInterval"] = cfg.logInterval;
  d["ignoreShortFaults"] = cfg.ignoreShortFaults;
}

void handleConfigGet() {
  JsonDocument d;
  configToJson(d);
  sendJson(d);
}

const char *checkProbe(const ProbeLimits &p, const char *name) {
  static char msg[96];
  if (!(p.cool < p.warn && p.warn < p.hot && p.hot <= p.alarm)) {
    snprintf(msg, sizeof(msg), "%s: blue < yellow < red <= alarm must hold.", name);
    return msg;
  }
  if (p.cool < -20 || p.alarm > 1200) {
    snprintf(msg, sizeof(msg), "%s: values must be between -20 and 1200 C.", name);
    return msg;
  }
  return nullptr;
}

bool validHostname(const char *h) {
  size_t n = strlen(h);
  if (n == 0 || n > 32) return false;
  for (size_t i = 0; i < n; i++) if (!isalnum((unsigned char)h[i]) && h[i] != '-') return false;
  return true;
}

void handleConfigPost() {
  JsonDocument d;
  if (deserializeJson(d, server.arg("plain"))) { server.send(400, "text/plain", "Settings were not valid JSON."); return; }
  Config c = cfg;
  const char *err = nullptr;

  auto setStr = [&](const char *k, char *dst, size_t n) {
    if (!d[k].is<const char *>()) return;
    const char *v = d[k].as<const char *>();
    if (strlen(v) >= n) { err = "A text setting is too long."; return; }
    strlcpy(dst, v, n);
  };
  auto setFloat = [&](const char *k, float &v) { if (!d[k].isNull()) v = d[k].as<float>(); };
  auto setBool = [&](const char *k, bool &v) { if (!d[k].isNull()) v = d[k].as<bool>(); };
  auto setU8 = [&](const char *k, uint8_t &v) { if (!d[k].isNull()) v = (uint8_t)constrain(d[k].as<int>(), 0, 255); };
  auto setU16 = [&](const char *k, uint16_t &v) { if (!d[k].isNull()) v = (uint16_t)constrain(d[k].as<int>(), 0, 65535); };

  setStr("ssid", c.ssid, sizeof(c.ssid));
  if (d["pass"].is<const char *>() && strlen(d["pass"].as<const char *>()) > 0) setStr("pass", c.pass, sizeof(c.pass));
  setStr("apName", c.apName, sizeof(c.apName));
  setStr("hostname", c.hostname, sizeof(c.hostname));
  setStr("tz", c.tz, sizeof(c.tz));
  setStr("ntp", c.ntp, sizeof(c.ntp));
  setFloat("wakeTemp", c.wakeTemp);
  setFloat("stoveCool", c.stove.cool); setFloat("stoveWarn", c.stove.warn); setFloat("stoveHot", c.stove.hot); setFloat("stoveAlarm", c.stove.alarm);
  setFloat("flueCool", c.flue.cool);   setFloat("flueWarn", c.flue.warn);   setFloat("flueHot", c.flue.hot);   setFloat("flueAlarm", c.flue.alarm);
  setFloat("intWarn", c.intWarn); setFloat("intAlarm", c.intAlarm);
  setBool("alarmEnabled", c.alarmEnabled); setU8("alarmVolume", c.alarmVolume); setU16("snoozeMin", c.snoozeMin);
  setBool("refuelEnabled", c.refuelEnabled); setFloat("refuelTemp", c.refuelTemp);
  setU8("refuelVolume", c.refuelVolume); setU16("refuelRepeatMin", c.refuelRepeatMin);
  setU16("logInterval", c.logInterval);
  setBool("ignoreShortFaults", c.ignoreShortFaults);

  if (!err) err = checkProbe(c.stove, "Stove top");
  if (!err) err = checkProbe(c.flue, "Flue");
  if (!err && !(c.intWarn < c.intAlarm && c.intWarn >= 20 && c.intAlarm <= 90)) err = "Inside temperatures: warn must be below alarm, between 20 and 90 C.";
  if (!err && (c.wakeTemp < 0 || c.wakeTemp > 300)) err = "Screen-on temperature must be between 0 and 300 C.";
  if (!err && (c.logInterval < 5 || c.logInterval > 3600)) err = "Logging interval must be between 5 and 3600 seconds.";
  if (!err && (c.snoozeMin < 1 || c.snoozeMin > 120)) err = "Mute time must be between 1 and 120 minutes.";
  if (!err && (c.refuelRepeatMin < 1 || c.refuelRepeatMin > 120)) err = "Reminder repeat must be between 1 and 120 minutes.";
  if (!err && (c.refuelTemp < 30 || c.refuelTemp > 600)) err = "Add-fuel temperature must be between 30 and 600 C.";
  if (!err && !validHostname(c.hostname)) err = "Device name may only use letters, numbers and hyphens.";
  if (!err && (strlen(c.apName) == 0 || strlen(c.tz) == 0 || strlen(c.ntp) == 0)) err = "Hotspot name, time zone and time server can't be empty.";
  if (err) { server.send(400, "text/plain", err); return; }

  bool wifiChanged = strcmp(c.ssid, cfg.ssid) || strcmp(c.pass, cfg.pass) ||
                     strcmp(c.apName, cfg.apName) || strcmp(c.hostname, cfg.hostname);
  bool timeChanged = strcmp(c.tz, cfg.tz) || strcmp(c.ntp, cfg.ntp);
  cfg = c;
  saveConfig();

  JsonDocument out;
  configToJson(out);
  out["wifiChanged"] = wifiChanged;
  sendJson(out);

  if (timeChanged) { applyTimezone(); if (netMode == NET_STA) startNtp(); }
  if (wifiChanged) pendingWifiRestart = millis() + 1500;
}

void handleDefaults() {
  setTemperatureDefaults(cfg);   // keeps Wi-Fi and clock settings
  saveConfig();
  JsonDocument d;
  configToJson(d);
  sendJson(d);
}

void handleTime() {
  JsonDocument d;
  if (deserializeJson(d, server.arg("plain")) || d["epoch"].isNull()) { server.send(400, "text/plain", "Send the time as {\"epoch\": seconds}."); return; }
  long long e = d["epoch"].as<long long>();
  if (e < 1704067200LL || e > 4102444800LL) { server.send(400, "text/plain", "That date is out of range."); return; }
  struct timeval tv = { (time_t)e, 0 };
  settimeofday(&tv, nullptr);
  saveTimeToRtc();
  timeSource = "Set by hand";
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleMute() { muteAlarm(); server.send(200, "application/json", "{\"ok\":true}"); }

void handleTest() {
  JsonDocument d;
  deserializeJson(d, server.arg("plain"));
  String type = d["type"] | "alarm";
  if (type == "reminder") chirp();
  else {
    M5.Speaker.setVolume(cfg.alarmVolume);
    M5.Speaker.tone(2800, 350); delay(400);
    M5.Speaker.tone(2100, 350); delay(400);
    M5.Speaker.tone(2800, 350);
  }
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleRestart() {
  server.send(200, "application/json", "{\"ok\":true}");
  pendingReboot = millis() + 1000;
}

bool safeLogName(const String &n) {
  if (n.length() < 5 || n.length() > 40 || !n.endsWith(".csv") || n.indexOf("..") >= 0) return false;
  for (size_t i = 0; i < n.length(); i++) {
    char ch = n[i];
    if (!isalnum((unsigned char)ch) && ch != '-' && ch != '_' && ch != '.') return false;
  }
  return true;
}

void handleLogs() {
  JsonDocument d;
  d["sd"] = sdOk;
  JsonArray a = d["files"].to<JsonArray>();
  if (sdOk) {
    File dir = SD.open("/logs");
    if (dir) {
      File f = dir.openNextFile();
      while (f) {
        if (!f.isDirectory()) {
          String n = f.name();
          int slash = n.lastIndexOf('/');
          if (slash >= 0) n = n.substring(slash + 1);
          JsonObject o = a.add<JsonObject>();
          o["name"] = n;
          o["size"] = (uint32_t)f.size();
        }
        f.close();
        f = dir.openNextFile();
      }
      dir.close();
    }
  }
  sendJson(d);
}

void handleLogTail() {
  String n = server.arg("f");
  if (!sdOk || !safeLogName(n)) { server.send(400, "text/plain", "Unknown log file."); return; }
  int lines = server.hasArg("n") ? constrain(server.arg("n").toInt(), 10, 500) : 300;
  File f = SD.open("/logs/" + n);
  if (!f) { server.send(404, "text/plain", "Log file not found."); return; }
  size_t sz = f.size();
  size_t want = (size_t)lines * 90;
  size_t start = sz > want ? sz - want : 0;
  String out;
  out.reserve((sz > want ? want : sz) + 160);
  if (start > 0) {
    out = f.readStringUntil('\n');   // header line
    out += '\n';
    f.seek(start);
    f.readStringUntil('\n');         // skip partial line
  }
  uint8_t buf[512];
  int r;
  while ((r = f.read(buf, sizeof(buf))) > 0) out.concat((const char *)buf, r);
  f.close();
  server.send(200, "text/plain", out);
}

void handleDownload() {
  String n = server.arg("f");
  if (!sdOk || !safeLogName(n)) { server.send(400, "text/plain", "Unknown log file."); return; }
  File f = SD.open("/logs/" + n);
  if (!f) { server.send(404, "text/plain", "Log file not found."); return; }
  server.sendHeader("Content-Disposition", "attachment; filename=\"" + n + "\"");
  server.streamFile(f, "text/csv");
  f.close();
}

void handleNotFound() {
  if (apUp && server.hostHeader() != apIP.toString()) {
    server.sendHeader("Location", "http://" + apIP.toString() + "/", true);
    server.send(302, "text/plain", "");
    return;
  }
  server.send(404, "text/plain", "Not found");
}

void setupWeb() {
  server.on("/", HTTP_GET, [] { server.send_P(200, "text/html", INDEX_HTML); });
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/history", HTTP_GET, handleHistory);
  server.on("/api/config", HTTP_GET, handleConfigGet);
  server.on("/api/config", HTTP_POST, handleConfigPost);
  server.on("/api/defaults", HTTP_POST, handleDefaults);
  server.on("/api/time", HTTP_POST, handleTime);
  server.on("/api/mute", HTTP_POST, handleMute);
  server.on("/api/test", HTTP_POST, handleTest);
  server.on("/api/restart", HTTP_POST, handleRestart);
  server.on("/api/logs", HTTP_GET, handleLogs);
  server.on("/api/logtail", HTTP_GET, handleLogTail);
  server.on("/api/download", HTTP_GET, handleDownload);
  server.onNotFound(handleNotFound);
  server.begin();
}

// ======================= Setup / loop =======================
uint32_t tSensor = 0, tHist = 0, tLog = 0, tSdRetry = 0;

void setup() {
  auto mc = M5.config();
  mc.serial_baudrate = 115200;
  M5.begin(mc);

  loadConfig();
  applyTimezone();
  loadTimeFromRtc();
  sntp_set_time_sync_notification_cb(onNtpSync);

  selectPins();

  canvas.setPsram(true);
  canvas.setColorDepth(16);
  if (!canvas.createSprite(M5.Display.width(), M5.Display.height())) {
    canvas.setPsram(false);
    canvas.setColorDepth(8);
    canvas.createSprite(M5.Display.width(), M5.Display.height());
  }

  mountSD();

  WiFi.persistent(false);
  WiFi.setHostname(cfg.hostname);
  startSTA(false);
  setupWeb();

  manualWakeUntil = millis() + 30000;   // show the screen (and IP) for 30 s after start-up
  uint32_t now = millis();
  tHist = now - HIST_PERIOD_MS + 5000;  // first graph point after ~5 s
  tLog = now - cfg.logInterval * 1000UL + 5000;
}

void loop() {
  M5.update();
  server.handleClient();
  netLoop();
  handleButtons();

  uint32_t ms = millis();
  if (ms - tSensor >= 1000) {
    tSensor = ms;
    readSensors();
    updateAlarms();
    updateScreen();
  }
  if (ms - tHist >= HIST_PERIOD_MS) {
    tHist = ms;
    addHistory();
    checkRefuel();
  }
  if (ms - tLog >= cfg.logInterval * 1000UL) {
    tLog = ms;
    logSample();
  }
  if (!sdOk && ms - tSdRetry >= 30000) {
    tSdRetry = ms;
    mountSD();
  }
  if (ntpJustSynced) {
    ntpJustSynced = false;
    saveTimeToRtc();
    timeSource = "Internet time (NTP)";
  }
  if (pendingWifiRestart && (int32_t)(ms - pendingWifiRestart) >= 0) {
    pendingWifiRestart = 0;
    restartWifi();
  }
  if (pendingReboot && (int32_t)(ms - pendingReboot) >= 0) ESP.restart();
  delay(2);
}
