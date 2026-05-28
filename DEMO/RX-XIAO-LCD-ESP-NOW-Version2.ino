#include <U8g2lib.h>
#include <WiFi.h>
#include <esp_now.h>

// ST7920 128x64 LCD (12864B-V2.3) in SPI mode (PSB=GND)
#define LCD_CLK   8   // D8 - Pin 6 (E/SCLK)
#define LCD_DATA  10  // D10 - Pin 5 (R/W/SID)
#define LCD_CS    4   // D2 - Pin 4 (RS/CS)
#define LCD_RST   5   // D3 - Pin 17 (RST)

U8G2_ST7920_128X64_F_SW_SPI u8g2(U8G2_R0, LCD_CLK, LCD_DATA, LCD_CS, LCD_RST);

struct __attribute__((packed)) CanFrame {
    uint32_t id;
    uint8_t  dlc;
    uint8_t  data[8];
};

static uint32_t rxCount = 0;
static uint32_t errCount = 0;

static const int MAX_ENTRIES = 5;
struct RxEntry {
    uint32_t id;
    uint8_t  data[8];
    uint8_t  dlc;
};
static RxEntry history[MAX_ENTRIES];
static int histIdx = 0;
static int histFill = 0;
static volatile bool newData = false;

void drawScreen() {
    u8g2.clearBuffer();

    u8g2.setFont(u8g2_font_6x10_tr);
    char header[32];
    snprintf(header, sizeof(header), "ESP-NOW RX #%lu", (unsigned long)rxCount);
    u8g2.drawStr(0, 9, header);
    u8g2.drawHLine(0, 11, 128);

    int count = histFill < MAX_ENTRIES ? histFill : MAX_ENTRIES;
    for (int i = 0; i < count; i++) {
        int idx = (histIdx - 1 - i + MAX_ENTRIES) % MAX_ENTRIES;
        RxEntry &e = history[idx];

        char line[32];
        int pos = snprintf(line, sizeof(line), "%03lX:", (unsigned long)e.id);
        for (int b = 0; b < e.dlc && pos < (int)sizeof(line) - 4; b++) {
            pos += snprintf(line + pos, sizeof(line) - pos, "%02X", e.data[b]);
        }
        u8g2.drawStr(0, 22 + i * 10, line);
    }

    u8g2.sendBuffer();
}

void onReceive(const uint8_t *mac, const uint8_t *data, int len) {
    if (len < (int)sizeof(CanFrame)) {
        errCount++;
        return;
    }

    CanFrame frame;
    memcpy(&frame, data, sizeof(CanFrame));

    rxCount++;

    history[histIdx].id = frame.id;
    history[histIdx].dlc = frame.dlc;
    memcpy(history[histIdx].data, frame.data, 8);
    histIdx = (histIdx + 1) % MAX_ENTRIES;
    if (histFill < MAX_ENTRIES) histFill++;

    Serial.printf("[RX #%lu] MAC=%02X:%02X:%02X:%02X:%02X:%02X ID=0x%03lX [%d] ",
                  (unsigned long)rxCount,
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
                  (unsigned long)frame.id, frame.dlc);
    for (int i = 0; i < frame.dlc; i++) {
        Serial.printf("%02X ", frame.data[i]);
    }
    Serial.println();

    newData = true;
}

void setup() {
    Serial.begin(115200);
    delay(2000);
    Serial.println("\n=== XIAO ESP32-C3 - ESP-NOW CAN RX ===");

    u8g2.begin();
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(0, 20, "ESP-NOW Init...");
    u8g2.sendBuffer();

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    Serial.printf("MAC: %s\n", WiFi.macAddress().c_str());

    if (esp_now_init() != ESP_OK) {
        Serial.println("[ESP-NOW] Init FAILED!");
        u8g2.clearBuffer();
        u8g2.drawStr(0, 30, "ESP-NOW FAIL!");
        u8g2.sendBuffer();
        while (true) delay(1000);
    }

    esp_now_register_recv_cb(onReceive);
    Serial.println("[ESP-NOW] Ready — waiting for packets");

    memset(history, 0, sizeof(history));

    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(0, 9, "ESP-NOW RX #0");
    u8g2.drawHLine(0, 11, 128);
    char macLine[32];
    snprintf(macLine, sizeof(macLine), "%s", WiFi.macAddress().c_str());
    u8g2.drawStr(0, 30, macLine);
    u8g2.drawStr(0, 45, "Waiting...");
    u8g2.sendBuffer();
}

void loop() {
    if (newData) {
        newData = false;
        drawScreen();
    }
    delay(10);
}
