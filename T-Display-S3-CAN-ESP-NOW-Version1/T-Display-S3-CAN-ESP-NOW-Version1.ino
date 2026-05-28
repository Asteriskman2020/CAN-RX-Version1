// ============================================================================
//  LilyGo T-Display S3 - ESP-NOW CAN Receiver with TFT display
//  ----------------------------------------------------------------------------
//  Receives CAN frames forwarded via ESP-NOW broadcast (from a paired
//  CAN-to-ESP-NOW bridge such as ESP32-C3 SuperMini) and shows the last
//  12 frames live on the built-in 1.9" 170x320 ST7789 panel.
//
//  Hardware:
//    - LilyGo T-Display S3 (ESP32-S3, 170x320 ST7789 8-bit parallel TFT)
//
//  Required ESP-NOW packet (from the bridge):
//    struct CanFrame { uint32_t id; uint8_t dlc; uint8_t data[8]; }
//
//  Required library (Arduino IDE):
//    - TFT_eSPI by Bodmer
//    Configure User_Setup_Select.h to use the LilyGo T-Display S3 setup
//    (Setup206_LilyGo_T_Display_S3.h) or define the equivalent build flags.
//
//  Display layout (landscape):
//    +------------------------- ESP-NOW CAN RX -------------------------+
//    | RX: 123   ERR: 0                         AC:A7:04:05:97:88      |
//    |------------------------------------------------------------------|
//    | 100  00 11 22 33 44 55 66 77                              12ms   |
//    | 200  ...                                                  120ms  |
//    | ...                                                              |
//    +------------------------------------------------------------------+
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

static const int MAX_ENTRIES = 12;
struct RxEntry {
    uint32_t id;
    uint8_t  dlc;
    uint8_t  data[8];
    uint32_t ts;
};
static RxEntry history[MAX_ENTRIES];
static int histIdx = 0;
static int histFill = 0;

static uint32_t rxCount = 0;
static uint32_t errCount = 0;
static volatile bool dirty = false;

// Color palette (RGB565)
#define COL_BG       0x10A2
#define COL_HEADER   0x2D5F
#define COL_HEADER_T 0xFFFF
#define COL_DIV      0x4A69
#define COL_ID       0xFD20
#define COL_DATA     0xFFFF
#define COL_LABEL    0x9CB1
#define COL_STAT     0x07E0

void drawHeader() {
    tft.fillRect(0, 0, 320, 28, COL_HEADER);
    tft.setTextColor(COL_HEADER_T, COL_HEADER);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("ESP-NOW CAN RX", 160, 14, 4);
    tft.setTextDatum(TL_DATUM);
}

void drawStats() {
    tft.fillRect(0, 28, 320, 18, COL_BG);
    tft.setTextColor(COL_STAT, COL_BG);
    char buf[40];
    snprintf(buf, sizeof(buf), "RX: %lu   ERR: %lu",
             (unsigned long)rxCount, (unsigned long)errCount);
    tft.drawString(buf, 4, 30, 2);

    tft.setTextColor(COL_LABEL, COL_BG);
    String mac = WiFi.macAddress();
    tft.drawString(mac.c_str(), 320 - 6 * 17, 30, 2);

    tft.drawFastHLine(0, 46, 320, COL_DIV);
}

void drawFrames() {
    tft.fillRect(0, 50, 320, 170 - 50, COL_BG);

    int count = histFill < MAX_ENTRIES ? histFill : MAX_ENTRIES;
    int rowH = 10;
    int yStart = 52;

    for (int i = 0; i < count; i++) {
        int idx = (histIdx - 1 - i + MAX_ENTRIES) % MAX_ENTRIES;
        RxEntry &e = history[idx];

        int y = yStart + i * rowH;
        if (y > 170 - rowH) break;

        char idBuf[12];
        snprintf(idBuf, sizeof(idBuf), "%03lX", (unsigned long)e.id);
        tft.setTextColor(COL_ID, COL_BG);
        tft.drawString(idBuf, 4, y, 1);

        char dataBuf[40] = "";
        int pos = 0;
        for (int b = 0; b < e.dlc && pos < (int)sizeof(dataBuf) - 4; b++) {
            pos += snprintf(dataBuf + pos, sizeof(dataBuf) - pos, "%02X ", e.data[b]);
        }
        tft.setTextColor(COL_DATA, COL_BG);
        tft.drawString(dataBuf, 30, y, 1);

        uint32_t age = millis() - e.ts;
        char ageBuf[12];
        if (age < 1000) snprintf(ageBuf, sizeof(ageBuf), "%lums", (unsigned long)age);
        else snprintf(ageBuf, sizeof(ageBuf), "%lus", (unsigned long)(age / 1000));
        tft.setTextColor(COL_LABEL, COL_BG);
        tft.drawString(ageBuf, 270, y, 1);
    }
}

void onReceive(const uint8_t *mac, const uint8_t *data, int len) {
    if (len < (int)sizeof(CanFrame)) {
        errCount++;
        return;
    }
    CanFrame frame;
    memcpy(&frame, data, sizeof(CanFrame));

    history[histIdx].id = frame.id;
    history[histIdx].dlc = frame.dlc;
    memcpy(history[histIdx].data, frame.data, 8);
    history[histIdx].ts = millis();
    histIdx = (histIdx + 1) % MAX_ENTRIES;
    if (histFill < MAX_ENTRIES) histFill++;

    rxCount++;

    Serial.printf("[RX #%lu] ID=0x%03lX [%d] ",
                  (unsigned long)rxCount, (unsigned long)frame.id, frame.dlc);
    for (int i = 0; i < frame.dlc; i++) Serial.printf("%02X ", frame.data[i]);
    Serial.println();

    dirty = true;
}

void setup() {
    Serial.begin(115200);
    delay(2000);
    Serial.println("\n=== LilyGo T-Display S3 - ESP-NOW CAN RX ===");

    pinMode(TFT_BL_PIN, OUTPUT);
    digitalWrite(TFT_BL_PIN, HIGH);

    tft.init();
    tft.setRotation(1);
    tft.fillScreen(COL_BG);

    drawHeader();
    tft.setTextColor(COL_LABEL, COL_BG);
    tft.drawString("Initializing ESP-NOW...", 4, 60, 2);

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    Serial.printf("MAC: %s\n", WiFi.macAddress().c_str());

    if (esp_now_init() != ESP_OK) {
        Serial.println("[ESP-NOW] Init FAILED!");
        tft.fillScreen(TFT_RED);
        tft.setTextColor(TFT_WHITE, TFT_RED);
        tft.setTextDatum(MC_DATUM);
        tft.drawString("ESP-NOW FAIL", 160, 85, 4);
        while (true) delay(1000);
    }
    esp_now_register_recv_cb(onReceive);

    memset(history, 0, sizeof(history));
    drawStats();
    drawFrames();

    Serial.println("[ESP-NOW] Ready - waiting for packets");
}

static uint32_t lastRefresh = 0;
void loop() {
    uint32_t now = millis();
    if (dirty || (now - lastRefresh) >= 500) {
        dirty = false;
        lastRefresh = now;
        drawStats();
        drawFrames();
    }
    delay(20);
}
