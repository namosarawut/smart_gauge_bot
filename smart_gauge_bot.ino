/*
 * SMART GAUGE - B.O.T RACING edition
 * Board : ESP32-2432S028 (CYD, 320x240 ILI9341 + XPT2046 touch)
 * Bike  : Honda Wave 110i / 125i (K-Line, 10400 baud, via L9637D)
 * Pages : ENGINE, SENSOR, GRAPH, DTC, AFR, ECU, TRIP, SYS
 *
 * Hardware layer (pins, touch, K-Line, run modes) is identical to
 * smart_gauge_all.ino. Only the screens changed: light theme, splash logo,
 * SYS page.
 *
 * Libraries (Library Manager): TFT_eSPI (Bodmer), XPT2046_Touchscreen (Paul Stoffregen)
 * Use the SAME TFT_eSPI User_Setup.h as smart_gauge_all.ino (CYD pins).
 * It must also enable LOAD_GFXFF (free fonts) - the sketch stops with an
 * error message if it is missing.
 * Board    : ESP32 Dev Module
 * Keep splash_img.h in the same folder as this .ino.
 *
 * ===================== RUN MODES (KLINE_MODE) =====================
 *   1 = SNIFF  : LISTEN ONLY on K-Line. Never transmits. Raw bytes are
 *                printed to the Serial Monitor (115200). Use this first.
 *                The gauge shows "--" because nothing is decoded yet.
 *   2 = ACTIVE : sends wake-up + poll frames to the ECU and shows live data.
 *                !! Needs the PROTOCOL section below filled with values you
 *                !! VERIFIED by sniffing. The defaults are UNVERIFIED
 *                !! placeholders - do not use mode 2 on a real bike until
 *                !! you have replaced them.
 *
 * No simulated data in this version. Values show "--" until the ECU
 * answers and the MAP_* table below points at real bytes.
 * DTC read/clear, ECU-info read, WiFi setup and firmware update are NOT
 * implemented: those buttons show "NOT SUPPORTED YET". Nothing is sent.
 *
 * Wiring : GPIO22 (TX) -> L9637D TxD ; GPIO27 (RX) <- L9637D RxD
 *          (divider if RxD is 5V; check the L9637D datasheet)
 *          12V -> buck converter -> 5V board, fused + TVS. Common GND.
 */
#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include "splash_img.h"

#ifndef LOAD_GFXFF
#error "Enable '#define LOAD_GFXFF' in TFT_eSPI User_Setup.h (needed for the new fonts)"
#endif

// ===================== USER CONFIG =====================
#define KLINE_MODE   1          // 1=SNIFF (listen only), 2=ACTIVE
#define KLINE_TX     22
#define KLINE_RX     27
#define KLINE_BAUD   10400

#define RPM_MAX      12000.0f
#define OVERHEAT_C   120.0f     // TRIP overheat threshold
#define TEMP_WARN_C  110.0f     // temperature turns red above this

#define SPLASH_MS    2000       // splash logo time on power-up
#define BRAND_1      "B.O.T"    // header text (blue part)
#define BRAND_2      "RACING"   // header text (black part)

// ===================== K-LINE PROTOCOL (UNVERIFIED PLACEHOLDERS) =====================
// Replace after sniffing a real scan tool with KLINE_MODE 1.
const uint8_t  INIT_FRAME[] = {0xFE, 0x04, 0xFF, 0xFF};   // UNVERIFIED
const uint8_t  POLL_REQ[]   = {0x72, 0x05, 0x71, 0x00};   // UNVERIFIED (checksum added automatically)
const uint16_t WAKE_LOW_MS  = 70;                         // UNVERIFIED
const uint16_t WAKE_HIGH_MS = 130;                        // UNVERIFIED
const bool     ADD_CHECKSUM = true;                       // frame bytes sum to 0 (mod 256)

// Where each value sits inside the poll reply frame.
// idx = byte index (0 = first byte of reply), len = 1 or 2 (big-endian),
// value = raw * scale + offset.   idx < 0 -> field disabled (stays 0).
struct FieldMap { int8_t idx; uint8_t len; float scale; float offset; };
FieldMap MAP_RPM   = {-1, 2, 1.0f, 0.0f};
FieldMap MAP_SPEED = {-1, 1, 1.0f, 0.0f};
FieldMap MAP_TPS   = {-1, 1, 1.0f, 0.0f};
FieldMap MAP_TEMP  = {-1, 1, 1.0f, 0.0f};
FieldMap MAP_BAT   = {-1, 1, 0.1f, 0.0f};
FieldMap MAP_INJ   = {-1, 2, 0.01f, 0.0f};
FieldMap MAP_IGN   = {-1, 1, 1.0f, 0.0f};
FieldMap MAP_O2    = {-1, 1, 0.01f, 0.0f};   // O2 sensor volts (narrowband)
// ======================================================================================

// ---------- touch (separate SPI bus on CYD) ----------
#define T_CLK  25
#define T_MOSI 32
#define T_MISO 39
#define T_CS   33
#define T_IRQ  36
SPIClass touchSPI(VSPI);
XPT2046_Touchscreen ts(T_CS, T_IRQ);

TFT_eSPI tft;
TFT_eSprite spr(&tft);   // GRAPH page (flicker-free)

// ---------- theme colors (RGB565) ----------
#define C_BG      0xEF7D  // page background (soft grey-white)
#define C_CARD    0xF7BE  // card fill
#define C_BORDER  0xBE19  // card border grey
#define C_TRACK   0xDEFB  // empty bar / grid
#define C_TXT     0x18E3  // main text (near black)
#define C_LBL     0x6B8F  // small labels (grey)
#define C_AMBER   0xE5A5
#define C_RED     0xE1E7
#define C_GREEN   0x6E46
#define C_BLUE    0x64BB  // soft blue border
#define C_ACC     0x2B7F  // strong blue accent
#define C_WHITE   0xFFFF

#define F_HEAD  &FreeSansBoldOblique9pt7b
#define F_VAL   &FreeSansBoldOblique12pt7b
#define F_MID   &FreeSansBoldOblique18pt7b

// ---------- shared data ----------
struct GaugeData {
  float rpm = 0, speed = 0, tps = 0, temp = 0, afr = 14.7, o2 = 0, bat = 0;
  float inj = 0, ign = 0;
  int mode = 1;
};
GaugeData g;
bool hasWideband = false;                 // set true when a wideband AFR is wired in
volatile bool g_link = false;             // ECU answering (ACTIVE mode)
volatile uint32_t g_rxBytes = 0;

bool afrValid()  { return hasWideband; }                 // narrowband O2 cannot give real AFR
bool dataValid() { return KLINE_MODE == 2 && g_link; }   // live data only when ECU answers

const char *modeLabel() {
  if (KLINE_MODE == 1) return "SNIFF";
  return g_link ? "LIVE" : "NO LINK";
}
uint16_t modeColor() {
  if (KLINE_MODE == 1) return C_ACC;
  return g_link ? C_GREEN : C_RED;
}

// SYS page status pills (real state, no fake values)
const char *ecuStatus()      { return dataValid() ? "ONLINE" : "OFFLINE"; }
uint16_t    ecuStatusCol()   { return dataValid() ? C_GREEN : C_AMBER; }
const char *klineStatus() {
  if (KLINE_MODE == 1) return "SNIFF";
  return g_link ? "OK" : "OFF";
}
uint16_t klineStatusCol() {
  if (KLINE_MODE == 1) return C_AMBER;
  return g_link ? C_GREEN : C_RED;
}

struct TripData {
  float maxRpm = 0, maxTemp = 0, minBat = 99, topSpeed = 0;
  uint32_t overMs = 0;
  int events = 0;
  bool over = false;
  bool has = false;            // true once real data has been recorded
};
TripData trip;

// ---------- pages ----------
enum Page { P_ENGINE, P_SENSOR, P_GRAPH, P_DTC, P_AFR, P_ECU, P_TRIP, P_SYSTEM, P_COUNT };
const char *PAGE_NAME[P_COUNT] = {"ENG", "SENS", "GRPH", "DTC", "AFR", "ECU", "TRIP", "SYS"};
Page page = P_ENGINE;
const int TAB_Y = 217, TAB_H = 22;
const int TAB_W = 320 / P_COUNT;

// =====================================================================
//                              K-LINE
// =====================================================================
HardwareSerial KL(2);

uint8_t checksum8(const uint8_t *d, size_t n) {
  uint8_t s = 0;
  for (size_t i = 0; i < n; i++) s += d[i];
  return (uint8_t)(0x100 - s);
}

void klineBegin() { KL.begin(KLINE_BAUD, SERIAL_8N1, KLINE_RX, KLINE_TX); }

void klineWakeUp() {
  KL.end();
  pinMode(KLINE_TX, OUTPUT);
  digitalWrite(KLINE_TX, LOW);
  delay(WAKE_LOW_MS);
  digitalWrite(KLINE_TX, HIGH);
  delay(WAKE_HIGH_MS);
  klineBegin();
}

// K-Line is half-duplex: the transceiver echoes TX back to RX -> drain it.
void klineSend(const uint8_t *d, size_t n) {
  uint8_t buf[32];
  if (n > 30) return;
  memcpy(buf, d, n);
  if (ADD_CHECKSUM) { buf[n] = checksum8(buf, n); n++; }
  while (KL.available()) KL.read();
  for (size_t i = 0; i < n; i++) { KL.write(buf[i]); delay(2); }
  KL.flush();
  uint32_t t0 = millis();
  size_t echoed = 0;
  while (echoed < n && millis() - t0 < 100) {
    if (KL.available()) { KL.read(); echoed++; }
  }
}

size_t klineRead(uint8_t *out, size_t maxn, uint32_t quietMs = 150) {
  size_t n = 0;
  uint32_t last = millis();
  while (millis() - last < quietMs && n < maxn) {
    if (KL.available()) { out[n++] = KL.read(); last = millis(); }
    else vTaskDelay(1);
  }
  return n;
}

bool mapValue(const FieldMap &m, const uint8_t *f, size_t n, float &out) {
  if (m.idx < 0 || (size_t)(m.idx + m.len) > n) return false;
  uint32_t raw = (m.len == 2) ? (((uint32_t)f[m.idx] << 8) | f[m.idx + 1]) : f[m.idx];
  out = raw * m.scale + m.offset;
  return true;
}

void klineParse(const uint8_t *f, size_t n) {
  float v;
  if (mapValue(MAP_RPM, f, n, v))   g.rpm = v;
  if (mapValue(MAP_SPEED, f, n, v)) g.speed = v;
  if (mapValue(MAP_TPS, f, n, v))   g.tps = v;
  if (mapValue(MAP_TEMP, f, n, v))  g.temp = v;
  if (mapValue(MAP_BAT, f, n, v))   g.bat = v;
  if (mapValue(MAP_INJ, f, n, v))   g.inj = v;
  if (mapValue(MAP_IGN, f, n, v))   g.ign = v;
  if (mapValue(MAP_O2, f, n, v))    g.o2 = v;
}

void klineTask(void *) {
  klineBegin();
  uint8_t rx[64];
  uint8_t miss = 0;
  uint32_t lastByteMs = 0;
  for (;;) {
    if (KLINE_MODE == 1) {                         // ---- SNIFF: listen only ----
      while (KL.available()) {
        uint32_t now = millis();
        if (now - lastByteMs > 20) Serial.printf("\n[%lu] ", (unsigned long)now);
        Serial.printf("%02X ", KL.read());
        lastByteMs = now;
        g_rxBytes++;
      }
      vTaskDelay(1);
      continue;
    }
    // ---- ACTIVE ----
    if (!g_link) {
      klineWakeUp();
      klineSend(INIT_FRAME, sizeof(INIT_FRAME));
      size_t n = klineRead(rx, sizeof(rx), 300);
      if (n > 0) { g_link = true; miss = 0; g_rxBytes += n; }
      else { vTaskDelay(pdMS_TO_TICKS(1000)); continue; }
    }
    klineSend(POLL_REQ, sizeof(POLL_REQ));
    size_t n = klineRead(rx, sizeof(rx), 100);
    bool ok = n > 0;
    if (ok && ADD_CHECKSUM) {                      // sum of all bytes must be 0
      uint8_t s = 0;
      for (size_t i = 0; i < n; i++) s += rx[i];
      ok = (s == 0);
    }
    if (ok) { miss = 0; g_rxBytes += n; klineParse(rx, n); }
    else if (++miss > 5) { g_link = false; miss = 0; }
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

// =====================================================================
//                         TRIP / HISTORY
// =====================================================================
void tripUpdate(uint32_t dtMs) {
  trip.has = true;
  trip.maxRpm   = fmaxf(trip.maxRpm, g.rpm);
  trip.maxTemp  = fmaxf(trip.maxTemp, g.temp);
  trip.topSpeed = fmaxf(trip.topSpeed, g.speed);
  if (g.bat > 1 && g.bat < trip.minBat) trip.minBat = g.bat;
  bool o = g.temp > OVERHEAT_C;
  if (o) {
    trip.overMs += dtMs;
    if (!trip.over) trip.events++;
  }
  trip.over = o;
}

#define GW 270
#define GH 120
#define GX 36
#define GY 72
#define HN 270
float rpmHist[HN], afrHist[HN];
int hHead = 0;
void histPush() {
  rpmHist[hHead] = g.rpm;
  afrHist[hHead] = g.afr;
  hHead = (hHead + 1) % HN;
}

// AFR capture table: 12 RPM rows (x1000) x 6 TPS columns
#define AFR_ROWS 12
#define AFR_COLS 6
float    afrSum[AFR_ROWS][AFR_COLS];
uint16_t afrCnt[AFR_ROWS][AFR_COLS];
const int TPS_EDGE[AFR_COLS + 1] = {0, 5, 20, 40, 60, 80, 100};
const char *TPS_LBL[AFR_COLS] = {"0-5", "5-20", "20-40", "40-60", "60-80", "80+"};
int curRow = -1, curCol = -1;

void afrSample() {
  if (g.rpm < 800 || !afrValid()) return;
  int r = constrain((int)(g.rpm / 1000), 0, AFR_ROWS - 1);
  int c = AFR_COLS - 1;
  for (int i = 0; i < AFR_COLS; i++) if (g.tps < TPS_EDGE[i + 1]) { c = i; break; }
  if (afrCnt[r][c] < 65000) { afrSum[r][c] += g.afr; afrCnt[r][c]++; }
  curRow = r; curCol = c;
}

// DTC / ECU info state
int  dtcCount = -1;                  // -1 = not read yet
char dtcCode[3][8];
const char *dtcMsg = "";
bool dtcDirty = true;
char ecuId[20] = "--", ecuModel[24] = "--", ecuFlash[12] = "--";
const char *ecuMsg = "";

// =====================================================================
//                            DRAWING HELPERS
// =====================================================================
void fmtNum(char *b, size_t n, const char *fmt, float v, bool ok) {
  if (ok) snprintf(b, n, fmt, v); else snprintf(b, n, "--");
}

void fmtUptime(char *b, size_t n) {
  uint32_t s = millis() / 1000;
  snprintf(b, n, "%02lu:%02lu:%02lu", (unsigned long)s / 3600, (unsigned long)(s % 3600) / 60, (unsigned long)s % 60);
}

void card(int x, int y, int w, int h, uint16_t border = C_BORDER, bool thick = false) {
  tft.fillRoundRect(x, y, w, h, 7, C_CARD);
  tft.drawRoundRect(x, y, w, h, 7, border);
  if (thick) tft.drawRoundRect(x + 1, y + 1, w - 2, h - 2, 6, border);
}

// Free-font text, transparent background
void ffText(const char *s, int x, int y, const GFXfont *f, uint16_t col, uint8_t datum = MC_DATUM) {
  tft.setFreeFont(f);
  tft.setTextDatum(datum);
  tft.setTextPadding(0);
  tft.setTextColor(col);
  tft.drawString(s, x, y);
  // TFT_eSPI keeps using the free font for font 1 unless it is cleared
  tft.setFreeFont(NULL);
}

// Built-in font text with a background colour
void lblText(const char *s, int x, int y, uint16_t col, uint8_t datum = MC_DATUM, int font = 2, uint16_t bg = C_CARD) {
  tft.setTextDatum(datum);
  tft.setTextPadding(0);
  tft.setTextColor(col, bg);
  tft.drawString(s, x, y, font);
}

// Clear an area of a card, then draw a free-font value
void ffValue(int cx, int cy, int clrW, int clrH, const char *s, const GFXfont *f, uint16_t col) {
  tft.fillRect(cx - clrW / 2, cy - clrH / 2, clrW, clrH, C_CARD);
  ffText(s, cx, cy, f, col);
}

void drawHeader() {
  tft.fillRect(0, 0, 320, 30, C_CARD);
  tft.drawFastHLine(0, 30, 320, C_BORDER);
  ffText(BRAND_1, 6, 15, F_HEAD, C_ACC, ML_DATUM);
  ffText(BRAND_2, 62, 15, F_HEAD, C_TXT, ML_DATUM);
  tft.fillRect(220, 4, 98, 22, C_CARD);
  ffText(modeLabel(), 314, 15, F_HEAD, modeColor(), MR_DATUM);
}

void headerRefresh() {                 // redraw header only if mode text changed
  static const char *last = nullptr;
  if (page == P_SYSTEM) { last = nullptr; return; }   // SYS page has no header
  const char *now = modeLabel();
  if (now != last) { last = now; drawHeader(); }
}

void drawTabs() {
  tft.fillRect(0, TAB_Y - 1, 320, 240 - TAB_Y + 1, C_BG);
  for (int i = 0; i < P_COUNT; i++) {
    int x = i * TAB_W + 2, w = TAB_W - 4;
    bool act = (i == page);
    uint16_t bg = act ? C_ACC : C_CARD;
    tft.fillRoundRect(x, TAB_Y, w, TAB_H, 5, bg);
    tft.drawRoundRect(x, TAB_Y, w, TAB_H, 5, act ? C_ACC : C_BORDER);
    lblText(PAGE_NAME[i], x + w / 2, TAB_Y + TAB_H / 2 + 1, act ? C_WHITE : C_LBL, MC_DATUM, 2, bg);
  }
}

void box(int x, int y, int w, int h, const char *label, uint16_t labelCol) {
  card(x, y, w, h);
  lblText(label, x + 7, y + 5, labelCol, TL_DATUM, 2);
}

void boxValue(int x, int y, int w, int h, const char *val, uint16_t col) {
  tft.setTextDatum(MC_DATUM);
  tft.setTextPadding(w - 12);
  tft.setTextColor(col, C_CARD);
  tft.drawString(val, x + w / 2, y + h / 2 + 8, 4);
}

void button(int x, int y, int w, int h, const char *label, uint16_t col) {
  card(x, y, w, h, col, true);
  ffText(label, x + w / 2, y + h / 2, F_HEAD, col);
}

// ============================ SPLASH ============================
void drawSplash() {
  tft.setSwapBytes(true);
  tft.pushImage(0, 0, SPLASH_W, SPLASH_H, splash_img);
  tft.setSwapBytes(false);
  const int bx = 60, bw = 200, by = 235, bh = 3;   // loading bar on the logo's black edge
  tft.fillRoundRect(bx, by, bw, bh, 1, 0x2104);
  const int steps = 40;
  for (int i = 1; i <= steps; i++) {
    tft.fillRoundRect(bx, by, bw * i / steps, bh, 1, C_ACC);
    delay(SPLASH_MS / steps);
  }
}

// ============================ ENGINE ============================
const int BAR_SEGS = 24, BAR_X = 4, BAR_Y = 36, BAR_H = 16;
int lastSeg = -1;

void engineStatic() {
  tft.fillScreen(C_BG);
  drawHeader();
  lblText("RPM", 6, 108, C_LBL, TL_DATUM, 2, C_BG);
  lblText("km/h", 314, 108, C_LBL, TR_DATUM, 2, C_BG);
  box(4, 138, 100, 72, "TPS %", C_LBL);
  box(110, 138, 100, 72, "TEMP C", C_LBL);
  box(216, 138, 100, 72, afrValid() ? "AFR" : "O2 V", C_AMBER);
  drawTabs();
  lastSeg = -1;
}

void engineUpdate() {
  char b[16];
  bool ok = dataValid();
  int seg = ok ? constrain((int)(g.rpm / RPM_MAX * BAR_SEGS), 0, BAR_SEGS) : 0;
  if (seg != lastSeg) {
    const int sw = 312 / BAR_SEGS;
    for (int i = 0; i < BAR_SEGS; i++) {
      uint16_t c = (i >= 20) ? C_RED : (i >= 16 ? C_AMBER : C_ACC);
      tft.fillRoundRect(BAR_X + i * sw, BAR_Y, sw - 2, BAR_H, 2, i < seg ? c : C_TRACK);
    }
    lastSeg = seg;
  }
  tft.setTextColor(C_TXT, C_BG);
  tft.setTextDatum(TL_DATUM);
  tft.setTextPadding(170);
  fmtNum(b, sizeof b, "%.0f", g.rpm, ok);
  tft.drawString(b, 6, 56, 7);
  tft.setTextDatum(TR_DATUM);
  tft.setTextPadding(110);
  fmtNum(b, sizeof b, "%.0f", g.speed, ok);
  tft.drawString(b, 314, 56, 7);
  fmtNum(b, sizeof b, "%.0f", g.tps, ok);
  boxValue(4, 138, 100, 72, b, C_TXT);
  fmtNum(b, sizeof b, "%.0f", g.temp, ok);
  boxValue(110, 138, 100, 72, b, (ok && g.temp > TEMP_WARN_C) ? C_RED : C_TXT);
  if (afrValid()) fmtNum(b, sizeof b, "%.1f", g.afr, ok);
  else            fmtNum(b, sizeof b, "%.2f", g.o2, ok);
  boxValue(216, 138, 100, 72, b, C_AMBER);
}

// ============================ SENSOR ============================
const int CELL_X[3] = {4, 110, 216};
const int CELL_Y[3] = {36, 96, 156};
const int CELL_W = 100, CELL_H = 56;

void sensorStatic() {
  tft.fillScreen(C_BG);
  drawHeader();
  const char *L[9] = {"TPS %", "MODE", "km/h", "TEMP C", "AFR", "O2 V", "INJ ms", "IGN deg", "BAT V"};
  for (int i = 0; i < 9; i++)
    box(CELL_X[i % 3], CELL_Y[i / 3], CELL_W, CELL_H, L[i], i == 4 ? C_AMBER : C_LBL);
  drawTabs();
}

void sensorUpdate() {
  char v[9][10];
  bool ok = dataValid();
  fmtNum(v[0], 10, "%.0f", g.tps, ok);
  snprintf(v[1], 10, "--");                         // MODE: no source yet
  fmtNum(v[2], 10, "%.0f", g.speed, ok);
  fmtNum(v[3], 10, "%.0f", g.temp, ok);
  fmtNum(v[4], 10, "%.1f", g.afr, ok && afrValid());
  fmtNum(v[5], 10, "%.2f", g.o2, ok);
  fmtNum(v[6], 10, "%.1f", g.inj, ok);
  fmtNum(v[7], 10, "%.0f", g.ign, ok);
  fmtNum(v[8], 10, "%.1f", g.bat, ok);
  for (int i = 0; i < 9; i++) {
    uint16_t col = (i == 4) ? C_AMBER : ((i == 3 && ok && g.temp > TEMP_WARN_C) ? C_RED : C_TXT);
    boxValue(CELL_X[i % 3], CELL_Y[i / 3], CELL_W, CELL_H, v[i], col);
  }
}

// ============================ GRAPH ============================
void graphStatic() {
  tft.fillScreen(C_BG);
  drawHeader();
  lblText("RPM", 6, 40, C_ACC, TL_DATUM, 2, C_BG);
  lblText("AFR", 190, 40, C_AMBER, TL_DATUM, 2, C_BG);
  tft.drawRoundRect(GX - 2, GY - 2, GW + 4, GH + 4, 4, C_BORDER);
  lblText("12k", GX - 3, GY, C_ACC, TR_DATUM, 1, C_BG);
  lblText("0", GX - 3, GY + GH - 8, C_ACC, TR_DATUM, 1, C_BG);
  lblText("20", GX + GW + 3, GY, C_AMBER, TL_DATUM, 1, C_BG);
  lblText("8", GX + GW + 3, GY + GH - 8, C_AMBER, TL_DATUM, 1, C_BG);
  lblText("-30s", GX, GY + GH + 4, C_LBL, TL_DATUM, 1, C_BG);
  lblText("0s", GX + GW, GY + GH + 4, C_LBL, TR_DATUM, 1, C_BG);
  drawTabs();
}

void graphUpdate() {
  char b[16];
  bool ok = dataValid();
  tft.setTextDatum(TL_DATUM);
  tft.setTextPadding(90);
  tft.setTextColor(C_TXT, C_BG);
  fmtNum(b, sizeof b, "%.0f", g.rpm, ok);
  tft.drawString(b, 46, 36, 4);
  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(C_AMBER, C_BG);
  fmtNum(b, sizeof b, "%.1f", g.afr, ok && afrValid());
  tft.setTextPadding(70);
  tft.drawString(b, 314, 36, 4);

  spr.fillSprite(C_CARD);
  for (int k = 1; k < 4; k++) spr.drawFastHLine(0, k * GH / 4, GW, C_TRACK);
  for (int k = 1; k < 3; k++) spr.drawFastVLine(k * GW / 3, 0, GH, C_TRACK);
  int pr = 0, pa = 0;
  for (int i = 0; i < HN; i++) {
    int idx = (hHead + i) % HN;
    float r = constrain(rpmHist[idx] / RPM_MAX, 0.0f, 1.0f);
    float a = constrain((afrHist[idx] - 8.0f) / 12.0f, 0.0f, 1.0f);
    int yr = GH - 1 - (int)(r * (GH - 1));
    int ya = GH - 1 - (int)(a * (GH - 1));
    if (i > 0 && ok) {
      spr.drawLine(i - 1, pr, i, yr, C_ACC);
      if (afrValid()) spr.drawLine(i - 1, pa, i, ya, C_AMBER);
    }
    pr = yr; pa = ya;
  }
  spr.pushSprite(GX, GY);
}

// ============================ DTC ============================
const int BTN_Y = 160, BTN_H = 44;

void dtcStatic() {
  tft.fillScreen(C_BG);
  drawHeader();
  ffText("ECU DIAGNOSTICS", 6, 44, F_HEAD, C_TXT, ML_DATUM);
  button(4, BTN_Y, 154, BTN_H, "READ DTC", C_ACC);
  button(162, BTN_Y, 154, BTN_H, "CLEAR DTC", C_AMBER);
  drawTabs();
  dtcDirty = true;
}

void dtcDraw() {
  tft.fillRect(0, 56, 320, 100, C_BG);
  const char *st; uint16_t col;
  char b[24];
  if (dtcCount < 0)       { st = "NOT READ"; col = C_LBL; }
  else if (dtcCount == 0) { st = "NO DTC";   col = C_GREEN; }
  else { snprintf(b, sizeof b, "%d CODE%s", dtcCount, dtcCount > 1 ? "S" : ""); st = b; col = C_RED; }
  ffText(st, 160, 80, F_MID, col);
  for (int i = 0; i < dtcCount && i < 3; i++) lblText(dtcCode[i], 160, 112 + i * 16, C_TXT, MC_DATUM, 2, C_BG);
  tft.fillRect(0, 206, 320, 10, C_BG);
  lblText(dtcMsg, 160, 210, C_AMBER, MC_DATUM, 1, C_BG);
  dtcDirty = false;
}

void dtcDoRead() {
  dtcMsg = "NOT SUPPORTED YET";
  dtcDirty = true;
}

void dtcDoClear() {
  dtcMsg = "NOT SUPPORTED YET";
  dtcDirty = true;
}

void dtcUpdate() {
  if (dtcDirty) dtcDraw();
}

// ============================ AFR table ============================
const int AFR_X0 = 42, AFR_CW = 46, AFR_Y0 = 55, AFR_RH = 12;

void afrStatic() {
  tft.fillScreen(C_BG);
  drawHeader();
  lblText("AFR CAPTURE   RPM x1000 / TPS %", 6, 34, C_TXT, TL_DATUM, 1, C_BG);
  for (int c = 0; c < AFR_COLS; c++)
    lblText(TPS_LBL[c], AFR_X0 + c * AFR_CW + AFR_CW / 2, 49, C_ACC, MC_DATUM, 1, C_BG);
  char b[8];
  for (int r = 0; r < AFR_ROWS; r++) {
    snprintf(b, sizeof b, "%d-%d", r, r + 1);
    lblText(b, 4, AFR_Y0 + r * AFR_RH + 2, C_ACC, TL_DATUM, 1, C_BG);
  }
  lblText("DATA CLEARS ON POWER OFF", 6, 202, C_LBL, TL_DATUM, 1, C_BG);
  tft.drawRoundRect(252, 199, 64, 13, 3, C_AMBER);
  lblText("CLEAR", 284, 205, C_AMBER, MC_DATUM, 1, C_BG);
  drawTabs();
}

void afrUpdate() {
  static uint32_t last = 0;
  if (millis() - last < 300) return;
  last = millis();
  char b[8];
  tft.setTextDatum(MC_DATUM);
  tft.setTextPadding(AFR_CW - 2);
  for (int r = 0; r < AFR_ROWS; r++) {
    for (int c = 0; c < AFR_COLS; c++) {
      bool cur = (r == curRow && c == curCol);
      uint16_t fg, bg = cur ? C_ACC : C_BG;
      if (afrCnt[r][c] == 0) { snprintf(b, sizeof b, "--"); fg = C_LBL; }
      else {
        float a = afrSum[r][c] / afrCnt[r][c];
        snprintf(b, sizeof b, "%.1f", a);
        fg = (a < 13.5f) ? C_AMBER : (a > 15.5f ? C_RED : C_TXT);
      }
      if (cur) fg = C_WHITE;
      tft.setTextColor(fg, bg);
      tft.drawString(b, AFR_X0 + c * AFR_CW + AFR_CW / 2, AFR_Y0 + r * AFR_RH + 6, 1);
    }
  }
  tft.setTextPadding(0);
}

// ============================ ECU info ============================
void ecuStatic() {
  tft.fillScreen(C_BG);
  drawHeader();
  ffText("ECU INFO", 6, 44, F_HEAD, C_TXT, ML_DATUM);
  const char *L[5] = {"ECU ID", "MODEL", "FLASH COUNT", "LINK", "RX BYTES"};
  for (int i = 0; i < 5; i++) {
    lblText(L[i], 10, 58 + i * 20, C_LBL, TL_DATUM, 2, C_BG);
    tft.drawFastHLine(6, 76 + i * 20, 308, C_BORDER);
  }
  button(4, 168, 312, 34, "READ ECU INFO", C_ACC);
  drawTabs();
}

void ecuUpdate() {
  char b[16];
  const char *v[5];
  v[0] = ecuId; v[1] = ecuModel; v[2] = ecuFlash;
  v[3] = (KLINE_MODE == 1) ? "SNIFF" : (g_link ? "OK" : "NO LINK");
  snprintf(b, sizeof b, "%lu", (unsigned long)g_rxBytes);
  v[4] = b;
  tft.setTextDatum(TR_DATUM);
  tft.setTextPadding(170);
  tft.setTextColor(C_TXT, C_BG);
  for (int i = 0; i < 5; i++) tft.drawString(v[i], 312, 58 + i * 20, 2);
  tft.setTextDatum(MC_DATUM);
  tft.setTextPadding(300);
  tft.setTextColor(C_AMBER, C_BG);
  tft.drawString(ecuMsg, 160, 210, 1);
  tft.setTextPadding(0);
}

void ecuDoRead() {
  ecuMsg = "NOT SUPPORTED YET";
}

// ============================ TRIP ============================
void tripStatic() {
  tft.fillScreen(C_BG);
  drawHeader();
  ffText("TRIP SUMMARY", 6, 44, F_HEAD, C_TXT, ML_DATUM);
  box(4, 56, 154, 56, "MAX RPM", C_LBL);
  box(162, 56, 154, 56, "MAX TEMP C", C_LBL);
  box(4, 116, 154, 56, "MIN BAT V", C_LBL);
  box(162, 116, 154, 56, "TOP km/h", C_LBL);
  lblText("RESET ON RESTART", 6, 204, C_LBL, TL_DATUM, 1, C_BG);
  drawTabs();
}

void tripDraw() {
  char b[40];
  bool ok = trip.has;
  fmtNum(b, sizeof b, "%.0f", trip.maxRpm, ok);
  boxValue(4, 56, 154, 56, b, C_TXT);
  fmtNum(b, sizeof b, "%.0f", trip.maxTemp, ok);
  boxValue(162, 56, 154, 56, b, (ok && trip.maxTemp > OVERHEAT_C) ? C_RED : C_TXT);
  fmtNum(b, sizeof b, "%.1f", trip.minBat, ok && trip.minBat < 90);
  boxValue(4, 116, 154, 56, b, C_TXT);
  fmtNum(b, sizeof b, "%.0f", trip.topSpeed, ok);
  boxValue(162, 116, 154, 56, b, C_TXT);
  if (ok) snprintf(b, sizeof b, "OVER %d C   %lu s / %d events", (int)OVERHEAT_C,
                   (unsigned long)(trip.overMs / 1000), trip.events);
  else    snprintf(b, sizeof b, "OVER %d C   --", (int)OVERHEAT_C);
  tft.setTextDatum(TL_DATUM);
  tft.setTextPadding(312);
  tft.setTextColor(trip.events ? C_RED : C_ACC, C_BG);
  tft.drawString(b, 6, 180, 2);
  tft.setTextPadding(0);
}

// ============================ SYS ============================
const int R1Y = 4,   R1H = 42;    // ECU / GPS / K-LINE
const int HPY = 51,  HPH = 58;    // HEAP MONITOR
const int R3Y = 114, R3H = 48;    // UPTIME / OTHER
const int R4Y = 166, R4H = 48;    // SET WIFI / UPDATE

char sysCache[6][14];
uint32_t sysLast = 0;
uint32_t sysMsgAt = 0;            // when a SYS message was shown (0 = none)

void sysField(int slot, int cx, int cy, int clrW, const char *s, uint16_t col) {
  if (strcmp(sysCache[slot], s) == 0) return;
  strncpy(sysCache[slot], s, sizeof sysCache[slot] - 1);
  ffValue(cx, cy, clrW, 22, s, F_VAL, col);
}

void systemStatic() {
  tft.fillScreen(C_BG);
  memset(sysCache, 0, sizeof sysCache);
  sysLast = 0;
  sysMsgAt = 0;

  card(4,   R1Y, 116, R1H);
  card(125, R1Y, 116, R1H);
  card(246, R1Y, 70,  R1H);
  lblText("ECU",    62,  R1Y + 32, C_LBL);
  lblText("GPS",    183, R1Y + 32, C_LBL);
  lblText("K-LINE", 281, R1Y + 32, C_LBL);
  ffText("N/A", 183, R1Y + 14, F_VAL, C_LBL);        // no GPS module on this build

  card(4, HPY, 312, HPH);
  ffText("HEAP MONITOR", 160, HPY + 11, F_HEAD, C_GREEN);
  lblText("FREE",     12,  HPY + 48, C_LBL, ML_DATUM);
  lblText("MIN FREE", 160, HPY + 48, C_LBL, MC_DATUM);
  lblText("LARGEST",  308, HPY + 48, C_LBL, MR_DATUM);

  card(4,   R3Y, 154, R3H);
  card(162, R3Y, 154, R3H);
  ffText("UPTIME", 81,  R3Y + 35, F_HEAD, C_TXT);
  ffText("OTHER",  239, R3Y + 15, F_VAL,  C_AMBER);
  ffText("RESET",  239, R3Y + 35, F_HEAD, C_TXT);

  card(4,   R4Y, 154, R4H, C_BLUE,  true);
  card(162, R4Y, 154, R4H, C_GREEN, true);
  ffText("SET WIFI", 81,  R4Y + 15, F_VAL,  C_AMBER);
  ffText("WIFI",     81,  R4Y + 35, F_HEAD, C_TXT);
  ffText("UPDATE",   239, R4Y + 15, F_VAL,  C_GREEN);
  ffText("FW v1.0",  239, R4Y + 35, F_HEAD, C_TXT);

  drawTabs();
}

void systemUpdate() {
  if (sysMsgAt) {                                       // message in the heap card
    if (millis() - sysMsgAt < 2000) return;
    sysMsgAt = 0;
    tft.fillRect(8, HPY + 19, 304, 22, C_CARD);
    memset(sysCache[2], 0, sizeof sysCache[2]);
    memset(sysCache[3], 0, sizeof sysCache[3]);
    memset(sysCache[4], 0, sizeof sysCache[4]);
  }
  if (sysLast && millis() - sysLast < 500) return;
  sysLast = millis();

  sysField(0, 62,  R1Y + 14, 108, ecuStatus(),   ecuStatusCol());
  sysField(1, 281, R1Y + 14, 62,  klineStatus(), klineStatusCol());

  char b[14];
  snprintf(b, sizeof b, "%lu K", (unsigned long)(esp_get_free_heap_size() / 1024));
  sysField(2, 56, HPY + 30, 96, b, C_TXT);
  snprintf(b, sizeof b, "%lu K", (unsigned long)(esp_get_minimum_free_heap_size() / 1024));
  sysField(3, 160, HPY + 30, 96, b, C_TXT);
  snprintf(b, sizeof b, "%lu K", (unsigned long)(heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT) / 1024));
  sysField(4, 264, HPY + 30, 96, b, C_TXT);

  fmtUptime(b, sizeof b);
  sysField(5, 81, R3Y + 15, 140, b, C_TXT);
}

void sysShowMsg(const char *m) {                         // shown in the heap card for 2 s
  tft.fillRect(8, HPY + 19, 304, 22, C_CARD);
  ffText(m, 160, HPY + 30, F_HEAD, C_AMBER);
  sysMsgAt = millis();
}

// ============================ dispatch ============================
void pageStatic() {
  switch (page) {
    case P_ENGINE: engineStatic(); break;
    case P_SENSOR: sensorStatic(); break;
    case P_GRAPH:  graphStatic();  break;
    case P_DTC:    dtcStatic();    break;
    case P_AFR:    afrStatic();    break;
    case P_ECU:    ecuStatic();    break;
    case P_TRIP:   tripStatic();   break;
    case P_SYSTEM: systemStatic(); break;
    default: break;
  }
}

void pageUpdate() {
  headerRefresh();
  switch (page) {
    case P_ENGINE: engineUpdate(); break;
    case P_SENSOR: sensorUpdate(); break;
    case P_GRAPH:  graphUpdate();  break;
    case P_DTC:    dtcUpdate();    break;
    case P_AFR:    afrUpdate();    break;
    case P_ECU:    ecuUpdate();    break;
    case P_TRIP:   tripDraw();     break;
    case P_SYSTEM: systemUpdate(); break;
    default: break;
  }
}

// ============================ touch ============================
bool inRect(int x, int y, int rx, int ry, int rw, int rh) {
  return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

void handlePageTap(int x, int y) {
  if (page == P_DTC) {
    if (inRect(x, y, 4, BTN_Y, 154, BTN_H))   dtcDoRead();
    if (inRect(x, y, 162, BTN_Y, 154, BTN_H)) dtcDoClear();
  } else if (page == P_ECU) {
    if (inRect(x, y, 4, 168, 312, 34)) ecuDoRead();
  } else if (page == P_AFR) {
    if (inRect(x, y, 240, 196, 80, 18)) {
      memset(afrSum, 0, sizeof afrSum);
      memset(afrCnt, 0, sizeof afrCnt);
      curRow = curCol = -1;
      afrStatic();
    }
  } else if (page == P_SYSTEM) {
    if (inRect(x, y, 162, R3Y, 154, R3H)) {             // OTHER / RESET -> clear trip summary
      trip = TripData();
      sysShowMsg("TRIP DATA RESET");
    }
    if (inRect(x, y, 4, R4Y, 154, R4H))   sysShowMsg("WIFI: NOT SUPPORTED YET");
    if (inRect(x, y, 162, R4Y, 154, R4H)) sysShowMsg("UPDATE: NOT SUPPORTED YET");
  }
}

void handleTouch() {
  static uint32_t lastTouch = 0;
  if (!ts.touched() || millis() - lastTouch < 300) return;
  TS_Point p = ts.getPoint();
  // raw -> screen. Rough CYD landscape values; tune with the serial print.
  int x = map(p.x, 200, 3700, 0, 320);
  int y = map(p.y, 240, 3800, 0, 240);
  x = constrain(x, 0, 319);
  y = constrain(y, 0, 239);
  Serial.printf("touch raw %d,%d -> %d,%d\n", p.x, p.y, x, y);
  lastTouch = millis();
  if (y >= TAB_Y - 6) {
    Page np = (Page)constrain(x / TAB_W, 0, P_COUNT - 1);
    if (np != page) { page = np; pageStatic(); pageUpdate(); }
  } else {
    handlePageTap(x, y);
  }
}

// ============================ setup / loop ============================
void setup() {
  Serial.begin(115200);
  pinMode(21, OUTPUT);
  digitalWrite(21, HIGH);                 // backlight
  tft.init();
  tft.setRotation(1);                     // landscape 320x240
  touchSPI.begin(T_CLK, T_MISO, T_MOSI, T_CS);
  ts.begin(touchSPI);
  ts.setRotation(1);

  spr.setColorDepth(16);
  if (!spr.createSprite(GW, GH)) Serial.println("sprite alloc failed");
  for (int i = 0; i < HN; i++) { rpmHist[i] = 0; afrHist[i] = 14.7f; }
  memset(afrSum, 0, sizeof afrSum);
  memset(afrCnt, 0, sizeof afrCnt);

  if (KLINE_MODE != 0)
    xTaskCreatePinnedToCore(klineTask, "kline", 6144, NULL, 1, NULL, 0);

  drawSplash();
  pageStatic();
}

void loop() {
  static uint32_t lastUpd = 0, lastHist = 0;
  handleTouch();

  uint32_t now = millis();
  if (now - lastHist >= 111) {            // ~9 Hz -> 270 samples = 30 s
    lastHist = now;
    if (dataValid()) histPush();
  }
  if (now - lastUpd >= 100) {             // 10 Hz refresh
    lastUpd = now;
    if (dataValid()) { tripUpdate(100); afrSample(); }
    pageUpdate();
  }
}
