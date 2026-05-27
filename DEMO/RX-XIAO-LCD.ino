#include <U8g2lib.h>
#include "driver/twai.h"

// XIAO ESP32-C3 CAN pins (swapped)
#define CAN_TX    21  // D6
#define CAN_RX    20  // D7

// ST7920 128x64 LCD (12864B-V2.3) in SPI mode (PSB=GND)
// Pin4(RS)=CS=GPIO4, Pin5(R/W)=SID=GPIO10, Pin6(E)=SCLK=GPIO8, Pin17(RST)=GPIO5
#define LCD_CLK   8   // D8 - Pin 6 (E/SCLK)
#define LCD_DATA  10  // D10 - Pin 5 (R/W/SID)
#define LCD_CS    4   // D2 - Pin 4 (RS/CS)
#define LCD_RST   5   // D3 - Pin 17 (RST)

U8G2_ST7920_128X64_F_SW_SPI u8g2(U8G2_R0, LCD_CLK, LCD_DATA, LCD_CS, LCD_RST);

static uint32_t rxCount = 0;
static uint32_t errCount = 0;

struct RxEntry {
    uint32_t id;
    uint8_t  data[8];
    uint8_t  dlc;
};
static const int MAX_ENTRIES = 5;
static RxEntry history[MAX_ENTRIES];
static int histIdx = 0;
static int histFill = 0;

void drawScreen() {
    u8g2.clearBuffer();

    u8g2.setFont(u8g2_font_6x10_tr);
    char header[32];
    snprintf(header, sizeof(header), "CAN RX #%lu E:%lu", (unsigned long)rxCount, (unsigned long)errCount);
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

void initCAN() {
    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(
        (gpio_num_t)CAN_TX, (gpio_num_t)CAN_RX, TWAI_MODE_NORMAL);
    twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    esp_err_t err = twai_driver_install(&g_config, &t_config, &f_config);
    if (err != ESP_OK) {
        Serial.printf("[CAN] Driver install FAILED: 0x%X\n", err);
        u8g2.clearBuffer();
        u8g2.drawStr(0, 30, "CAN INIT FAIL!");
        u8g2.sendBuffer();
        while (true) delay(1000);
    }

    err = twai_start();
    if (err != ESP_OK) {
        Serial.printf("[CAN] Start FAILED: 0x%X\n", err);
        u8g2.clearBuffer();
        u8g2.drawStr(0, 30, "CAN START FAIL!");
        u8g2.sendBuffer();
        while (true) delay(1000);
    }

    Serial.println("[CAN] Started @ 500 kbps — listening");
}

void setup() {
    Serial.begin(115200);
    delay(2000);
    Serial.println("\n=== XIAO ESP32-C3 - CAN RX Demo ===");
    Serial.printf("CAN TX=GPIO%d (D6)  RX=GPIO%d (D7)\n", CAN_TX, CAN_RX);

    u8g2.begin();
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(0, 20, "CAN RX Init...");
    u8g2.sendBuffer();

    initCAN();

    memset(history, 0, sizeof(history));
    drawScreen();
}

void loop() {
    twai_message_t msg;
    esp_err_t result = twai_receive(&msg, pdMS_TO_TICKS(100));

    if (result == ESP_OK) {
        if (msg.rtr) return;

        rxCount++;

        history[histIdx].id = msg.identifier;
        history[histIdx].dlc = msg.data_length_code;
        memcpy(history[histIdx].data, msg.data, 8);
        histIdx = (histIdx + 1) % MAX_ENTRIES;
        if (histFill < MAX_ENTRIES) histFill++;

        Serial.printf("[RX #%lu] ID=0x%03lX [%d] ",
                      (unsigned long)rxCount,
                      (unsigned long)msg.identifier,
                      msg.data_length_code);
        for (int i = 0; i < msg.data_length_code; i++) {
            Serial.printf("%02X ", msg.data[i]);
        }
        Serial.println();

        drawScreen();

    } else if (result != ESP_ERR_TIMEOUT) {
        errCount++;
        Serial.printf("[CAN] Receive error: 0x%X (total errs: %lu)\n",
                      result, (unsigned long)errCount);
        drawScreen();
    }
}
