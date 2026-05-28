// ============================================================================
//  LilyGo T-Display S3 - ESP-NOW CAN Dashboard (v3)
//  ----------------------------------------------------------------------------
//  Receives CAN frames via ESP-NOW broadcast, decodes OBD-II PID responses,
//  and shows three live gauges on the built-in 170x320 ST7789 panel.
//
//  Changes vs v2:
//    - Scaled gauge maxes for cold-start playback: Speed 50, RPM 2000,
//      Coolant 100
//    - Uses Font 6 (48px 7-segment) for the big numeric value
//    - Added a "value / max" numerical readout under the unit label
//    - Updated color thresholds for the new ranges
//
//  Gauges:
//    - SPEED   (km/h)   0-50    from PID 0x0D  (cyan)
//    - RPM             0-2000   from PID 0x0C  (orange, red >1700)
//    - COOLANT (°C)    0-100    from PID 0x05  (green, yellow >80, red >95)
//
//  Hardware:
//    - LilyGo T-Display S3 (ESP32-S3, 170x320 ST7789 8-bit parallel TFT)
//
//  Expected ESP-NOW packet:
//    struct CanFrame { uint32_t id; uint8_t dlc; uint8_t data[8]; }
//
//  Required Arduino library:
//    - TFT_eSPI by Bodmer (with T-Display S3 User_Setup; ensure LOAD_FONT6)
// ============================================================================

#include <WiFi.h>
#include <esp_now.h>
#include <TFT_eSPI.h>

#define TFT_BL_PIN  38

TFT_eSPI tft = TFT_eSPI();

struct __attribute__((packed)) CanFrame {
    uint32_t id;
    uint8_t  dlc;
    uint8_t  data[8];
};

// Live decoded values
static int rpm = 0;
static int speed_kmh = 0;
static int coolant_c = 0;
static uint32_t rxCount = 0;

// Previous values for change-detection redraw
static int prevRpm = -1, prevSpeed = -1, prevCoolant = -1;
static uint32_t prevRxCount = 0;

// Color palette (RGB565)
#define COL_BG        0x10A2
#define COL_PANEL_BG  0x18C3
#define COL_DIV       0x4A69
#define COL_HEADER    0x2D5F
#define COL_HEADER_T  0xFFFF
#define COL_LABEL     0x9CB1
#define COL_UNIT      0xCE79
#define COL_SPEED     0x07FF
#define COL_RPM       0xFD20
#define COL_RPM_HI    0xF800
#define COL_COOL      0x07E0
#define COL_COOL_HOT  0xF800
#define COL_COOL_WARN 0xFCC0

#define PANEL_W   106
#define PANEL_H   142
#define PANEL_Y   28

uint16_t rpmColor(int v) {
    if (v >= 1700) return COL_RPM_HI;
    return COL_RPM;
}
uint16_t coolantColor(int v) {
    if (v >= 95) return COL_COOL_HOT;
    if (v >= 80) return COL_COOL_WARN;
    return COL_COOL;
}

void drawHeader() {
    tft.fillRect(0, 0, 320, 26, COL_HEADER);
    tft.setTextColor(COL_HEADER_T, COL_HEADER);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("ESP-NOW CAN Dashboard", 160, 13, 2);
    tft.setTextDatum(TL_DATUM);
}

void drawBar(int x, int y, int w, int h, int pct, uint16_t color) {
    if (pct < 0) pct = 0; if (pct > 100) pct = 100;
    tft.drawRect(x, y, w, h, COL_DIV);
    tft.fillRect(x + 1, y + 1, w - 2, h - 2, COL_BG);
    int fillW = (w - 2) * pct / 100;
    if (fillW > 0) tft.fillRect(x + 1, y + 1, fillW, h - 2, color);
}

void drawPanel(int colIdx, const char *title, int value, int maxVal,
               const char *unit, uint16_t valueColor) {
    int x = colIdx * PANEL_W;

    tft.fillRect(x, PANEL_Y, PANEL_W, PANEL_H, COL_PANEL_BG);
    if (colIdx > 0) tft.drawFastVLine(x, PANEL_Y, PANEL_H, COL_DIV);

    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(COL_LABEL, COL_PANEL_BG);
    tft.drawString(title, x + PANEL_W / 2, PANEL_Y + 12, 2);

    // Big numeric value (Font 6 = 48px 7-segment)
    char vbuf[12];
    snprintf(vbuf, sizeof(vbuf), "%d", value);
    tft.setTextColor(valueColor, COL_PANEL_BG);
    tft.drawString(vbuf, x + PANEL_W / 2, PANEL_Y + 56, 6);

    // Unit
    tft.setTextColor(COL_UNIT, COL_PANEL_BG);
    tft.drawString(unit, x + PANEL_W / 2, PANEL_Y + 92, 2);

    // Numerical "value / max" readout
    char scaleBuf[20];
    snprintf(scaleBuf, sizeof(scaleBuf), "%d / %d", value, maxVal);
    tft.setTextColor(COL_LABEL, COL_PANEL_BG);
    tft.drawString(scaleBuf, x + PANEL_W / 2, PANEL_Y + 110, 2);

    // Progress bar
    int pct = (maxVal > 0) ? (value * 100 / maxVal) : 0;
    drawBar(x + 8, PANEL_Y + 128, PANEL_W - 16, 10, pct, valueColor);

    tft.setTextDatum(TL_DATUM);
}

void drawAll(bool force = false) {
    if (force) {
        tft.fillScreen(COL_BG);
        drawHeader();
    }

    if (force || prevRxCount != rxCount) {
        prevRxCount = rxCount;
        char buf[20];
        snprintf(buf, sizeof(buf), "#%lu", (unsigned long)rxCount);
        tft.fillRect(260, 0, 60, 26, COL_HEADER);
        tft.setTextDatum(MR_DATUM);
        tft.setTextColor(COL_HEADER_T, COL_HEADER);
        tft.drawString(buf, 316, 13, 2);
        tft.setTextDatum(TL_DATUM);
    }

    if (force || prevSpeed != speed_kmh) {
        prevSpeed = speed_kmh;
        drawPanel(0, "SPEED", speed_kmh, 50, "km/h", COL_SPEED);
    }
    if (force || prevRpm != rpm) {
        prevRpm = rpm;
        drawPanel(1, "RPM", rpm, 2000, "rpm", rpmColor(rpm));
    }
    if (force || prevCoolant != coolant_c) {
        prevCoolant = coolant_c;
        drawPanel(2, "COOLANT", coolant_c, 100, "deg C", coolantColor(coolant_c));
    }
}

// Decode OBD-II Mode 1 responses on 0x7E8 (ECM) and 0x7E9 (TCM)
void decodeFrame(const CanFrame &f) {
    if (f.id != 0x7E8 && f.id != 0x7E9) return;
    if (f.dlc < 3 || f.data[1] != 0x41) return;

    uint8_t pid = f.data[2];
    uint8_t A = f.data[3];
    uint8_t B = f.dlc >= 5 ? f.data[4] : 0;

    switch (pid) {
        case 0x05: coolant_c = (int)A - 40; break;
        case 0x0C: rpm = ((int)A * 256 + B) / 4; break;
        case 0x0D: speed_kmh = A; break;
        default: break;
    }
}

void onReceive(const uint8_t *mac, const uint8_t *data, int len) {
    if (len < (int)sizeof(CanFrame)) return;
    CanFrame frame;
    memcpy(&frame, data, sizeof(CanFrame));

    rxCount++;
    decodeFrame(frame);

    Serial.printf("[RX #%lu] ID=0x%03lX [%d]  SPD=%d RPM=%d COOL=%d\n",
                  (unsigned long)rxCount, (unsigned long)frame.id, frame.dlc,
                  speed_kmh, rpm, coolant_c);
}

void setup() {
    Serial.begin(115200);
    delay(1500);
    Serial.println("\n=== LilyGo T-Display S3 - ESP-NOW CAN Dashboard (v3) ===");

    pinMode(TFT_BL_PIN, OUTPUT);
    digitalWrite(TFT_BL_PIN, HIGH);

    tft.init();
    tft.setRotation(1);
    tft.fillScreen(COL_BG);
    drawHeader();
    tft.setTextColor(COL_LABEL, COL_BG);
    tft.drawString("Init ESP-NOW...", 4, 60, 2);

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    Serial.printf("MAC: %s\n", WiFi.macAddress().c_str());

    if (esp_now_init() != ESP_OK) {
        tft.fillScreen(TFT_RED);
        tft.setTextColor(TFT_WHITE, TFT_RED);
        tft.setTextDatum(MC_DATUM);
        tft.drawString("ESP-NOW FAIL", 160, 85, 4);
        Serial.println("[ESP-NOW] Init FAILED!");
        while (true) delay(1000);
    }
    esp_now_register_recv_cb(onReceive);

    drawAll(true);
    Serial.println("[ESP-NOW] Ready - waiting for OBD frames");
}

static uint32_t lastDraw = 0;
void loop() {
    uint32_t now = millis();
    if (now - lastDraw >= 100) {
        lastDraw = now;
        drawAll(false);
    }
    delay(10);
}
